/***********************************************************************
 *  Работа с кнопками
 *  
 *  Перед применением необходимо в заголовочном файле настроить
 *  макросы:
 *  - BUTTONS_COUNT - кол-во кнопок
 *  - GET_BUTTONS_HARD_STATE(var) - макрос для сохранения состояния
 *    кнопок в переменной var так, чтобы нумерация кнопок шла от
 *    младшего бита к старшему, т.е.: [8]-[7]-[6]-[5]-[4]-[3]-[2]-[1])
 *    
 *    Т.е. для восьми кнопок, подключенных последовательнок к порту D:
 *    
 *    #define GET_BUTTONS_HARD_STATE(var) var = PIND
 *    
 *    Для четырёх кнопок, подключенных последовательно к пинам D0-D3:
 *    
 *    #define GET_BUTTONS_HARD_STATE(var) var = PIND & 0b1111
 *    
 *    В сложных случаях (при подключении к разным портам), надо
 *    самостоятельно собрать данные побитово в один байт. Например,
 *    4 кнопки подключены к пинам [1]-D1, [2]-D7, [3]-C0, [4]-B3:
 *    
 *    #define GET_BUTTONS_HARD_STATE(var) \
 *        var = ((PIND & 0b00000010) >> 1) | \
 *              ((PIND & 0b10000000) >> 6) | \
 *              ((PINC & 0b00000001) << 2) | \
 *               (PINB & 0b1000)
 *
 *  Соответственно, в данном варианте можно использовать
 *  максимально 8 кнопок.
 */
 
#include <Arduino.h>
#include "buttons.h"

/* Фактическое состояние кнопок ("железа") для определения
    нажатий/отжатий: 0 - нажата, 1 - отжата */
static uint8_t buttons_hard_state_ = 0b1111;
/* Состояние кнопок, используемых для комбинаций (т.н. контрольные
    кнопки - по аналогии с кнопкой Ctrl на ПК): 1 - нажата, 0 - отжата */
static uint8_t buttons_ctrl_state_ = 0b0000;
/* Номер нажатой кнопки - последней нажатой кнопки, т.е. той кнопки,
    на которую будет реакция программы: 0 - нет нажатой кнопки,
    1-4 - номер кнопки (слева направо) */
static uint8_t pressed_button_ = 0;
/* Штамп времени нажатия или последней обработки кнопки (для режима
    многократных повторов) */
static unsigned long pressed_timestamp_ = 0;
/* Первый штамп (многократные повторы начинаются не сразу) */
static bool pressed_timestamp_first_ = false;

/***********************************************************************
 *  Опрос состояния кнопок
 *  p_ctrl_state  - состояние кнопок ([4]-[3]-[2]-[1],
 *      0 - не нажата, 1 - нажата).
 *  Возврат: номер кнопки, требующей обработки (1..4).
 */
uint8_t test_buttons(uint8_t *p_ctrl_state)
{
    uint8_t signaled_button = 0;
    uint8_t pressed_button = 0;

    uint8_t buttons_hard_state;
    
    GET_BUTTONS_HARD_STATE(buttons_hard_state);

    if (buttons_hard_state != buttons_hard_state_) {
        delay(50); /* Ждём завершения дребезга контактов. Не самое
            лучшее решение, но ограничимся им */
        GET_BUTTONS_HARD_STATE(buttons_hard_state);
    }

    /* Проверяем все кнопки по очереди */
    for (int i = 0; i < BUTTONS_COUNT; i++) {
        uint8_t mask = (1 << i);

        if ((buttons_hard_state & mask)
                != (buttons_hard_state_ & mask)) {
            /* Была нажата кнопка */
            if ((buttons_hard_state & mask) == 0) {
                buttons_ctrl_state_ |= mask; /* Сохраняем в состоянии
                    контрольных кнопок */
                pressed_button = i + 1; /* Запоминаем номер нажатой
                    кнопки. При одновременном нажатии кнопок приоритет
                    за той, что имеет больший номер */
            }
            /* Была отпущена кнопка */
            else {
                /*  Если отпущена "нажатая" кнопка,
                    сигнализируем об этом */
                if (i == pressed_button_ - 1) {
                    /*  Сохраняем состояние контрольных кнопок на
                        случай, если с "нажатой" кнопкой одновременно
                        были отпущены и контрольные. Если этого не
                        сделать, то для следующих проверяемых кнопок
                        "нажатая" кнопка уже будет отсутствовать и их
                        состояние может быть сброшено */
                    *p_ctrl_state = buttons_ctrl_state_ & ~mask;
                    /*  Приводим состояние контрльных кнопок
                        в соответствие с фактическим положением дел */
                    buttons_ctrl_state_ = ~buttons_hard_state & 0x0F;
                    pressed_button_ = 0;
                    signaled_button = i + 1;
                }
                /*  Если есть "нажатая" кнопка, то отпускание
                    проверяемой кнопки НЕ приводит к исключению её из
                    состояния контрольных кнопок. Т.е. если были нажаты
                    последовательно кнопки [1] и [2], то вне зависимости
                    от порядка их отпускания программой будет выполнена
                    комбинация [1]+[2] (не [2] и не [2]+[1]). Если же
                    "нажатой" кнопки нет (была уже отпущена, или были
                    одновременно нажаты несколько кнопок), то кнопка
                    спокойно из списка контрольных кнопок исключается */
                else if (pressed_button_ == 0) buttons_ctrl_state_ &= ~mask;
            }
        } /* if ((buttons_hard_state & mask)
                     != (buttons_hard_state_ & mask)) */
    } /* for (int i = 0; i < BUTTONS_COUNT; i++) */
  
    /*  Сохраняем номер "нажатой" кнопки, начинаем отсчёт времени
        удержания кнопки */
    if (pressed_button) {
        pressed_button_ = pressed_button;
        pressed_timestamp_ = millis();
        pressed_timestamp_first_ = true;
    }

    buttons_hard_state_ = buttons_hard_state;

    /*  Иммитация многократного нажатия при удержании кнопок более
        1 секунды (но только для тех сочетаний, где это имеет смысл!) */
    if (pressed_button_ &&
            (buttons_ctrl_state_ == 0b0001
            || buttons_ctrl_state_ == 0b0010
            || buttons_ctrl_state_ == 0b0100
            || buttons_ctrl_state_ == 0b1000)) {

        unsigned long timestamp = millis();
        unsigned elapsed = (unsigned)(timestamp - pressed_timestamp_);
    
        if (pressed_timestamp_first_ && elapsed >= 1000
                || !pressed_timestamp_first_ && elapsed >= 200) {
  
            *p_ctrl_state = buttons_ctrl_state_ & ~(1 << (pressed_button_ - 1));
            signaled_button = pressed_button_;
            pressed_timestamp_ = timestamp;
            pressed_timestamp_first_ = false;
        }
    }

    return signaled_button;
}

