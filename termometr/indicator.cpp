/***********************************************************************
 *  Работа с 4-знаковым семисегментным индикатором, подключенным
 *  напрямую к МК к портам:
 *  
 *  B0-B7 - аноды индикатора в последовательности B-G-C-Dp-D-E-A-F
 *          (PORTB: F-A-E-D-Dp-C-G-B)
 *  C2-C5 - катоды индикаторы в последовательности D4-D3-D2-D1
 *          (PORTC: x-x-D1-D2-D3-D4-x-x)
 */
#include <Arduino.h>
#include "indicator.h"


/***********************************************************************
 *  Структура для настройки режимов яркости с помощью динамической
 *  индикации:
 *    - on/off - соответственно, режимы индицации и отключения
 *      индикатора (паузы). Уровень яркости регулируется через
 *      пропорцилнальное уменьшение времени пребывания в одном
 *      и увеличения в другом.
 *      
 *    - prescaler - предделитель для TIMER2:
 *          1: 1 такт
 *          2: 8 тактов
 *          3: 32 такта
 *          4: 64 такта
 *          5: 128 тактов
 *          6: 256 тактов
 *          7: 1024 такта.
 *          
 *    - count - количество запусков обработчика до перехода
 *      к следующему знаку. Необходимо для восполнения резких
 *      переходов (в количестве тактов) между предделителями
 *      для плавных изменений яркости.
 *      
 *  Расчёт на примере 8-го уровня яркости:
 *  Параметры:
 *      {2, 1, 2, 6}
 *  Индикация:
 *      пределитель: 2
 *      кол-во запусков обработчика: 1
 *      => кол-во тактов в предделителе: 8
 *      => кол-во тактов на отображение одного знака: 8*256 = 2048
 *      => время вывода одного знака: 2048 / 8000000 = 256мкс
 *  Пауза:
 *      предделитель: 2
 *      кол-во запусков обработчика: 6
 *      => кол-во тактов на отображение одного знака:
 *          8*6*256 = 12288
 *      => время паузы: 12288 / 8000000 = 1536мкс
 *  Итого:
 *      Полный цикл отображения знаков вместе с паузой займёт:
 *          4 * 256мкс + 1536мкс = 2560мкс
 *      => Отображению одного знака в этом цикле будет уделено:
 *          256мкс / 2560мкс = 10% времени
 *          
 *  При максимальной яркости на один знак будет тратиться 25% времени.
 */

struct indicator_mode_t {
    uint8_t prescaler_on; /* Предделитель для режима горения */
    uint8_t on_count; /* Количество запусков-пропусков */
    uint8_t prescaler_off; /* Предделитель для паузы */
    uint8_t off_count;
};

static const indicator_mode_t c_indicator_modes[] = {
    {4, 1, 4, 1}, /*  0 -  0.0% */
    {1, 1, 5, 1}, /*  1 -  0.8% */
    {1, 1, 4, 1}, /*  2 -  1.5% */
    {1, 2, 4, 1}, /*  3 -  2.8% */
    {1, 3, 4, 1}, /*  4 -  3.9% */
    {1, 4, 4, 1}, /*  5 -  5.0% */
    {1, 5, 4, 1}, /*  6 -  6.0% */
    {2, 1, 4, 1}, /*  7 -  8.3% */
    {2, 1, 2, 6}, /*  8 - 10.0% */
    {2, 1, 3, 1}, /*  9 - 12.5% */
    {2, 3, 4, 1}, /* 10 - 15.0% */
    {2, 2, 3, 1}, /* 11 - 16.7% */
    {2, 3, 3, 1}, /* 12 - 18.8% */
    {3, 1, 3, 1}, /* 13 - 20.0% */
    {3, 1, 2, 2}, /* 14 - 22.0% */
    {3, 1, 0, 1}  /* 15 - 25.0% */
};

static const uint8_t c_max_brightness =
    sizeof(c_indicator_modes) / sizeof(*c_indicator_modes) - 1;

/* Массив изображений цифр для индикатора */
static const uint8_t c_digits0_9[10] = {
    DIGIT_0, DIGIT_1, DIGIT_2, DIGIT_3, DIGIT_4,
    DIGIT_5, DIGIT_6, DIGIT_7, DIGIT_8, DIGIT_9};

static indicator_t *g_one_indicator;

/***********************************************************************
 *  Плавное мигание
 *  Параметры:
 *  - step - значение кратные 0..31 (0 и 31 - максимальная яркость,
 *    15,16 - минимальная яркость)
 */
void screen_t::blink(uint8_t step)
{
    step %= 32;
    brightness_ = (step <= 15 ? 15 - step : step - 16);
}

/***********************************************************************
 * Яркость
 */
void screen_t::set_brightness(int8_t brightness)
{
    if (brightness < 0)
        brightness_ = 0;
    else if (brightness > c_max_brightness)
        brightness_ = c_max_brightness;
    else
        brightness_ = brightness;
}

/***********************************************************************
 *  Вывод числа с фиксированной запятой в память
 *  Параметры:
 *  - num - выводимое число;
 *  - decimals - кол-во знаков после запятой;
 *  - begin, end - начальная и конечная позициия для вывода числа
 *    (от 1 до DIG_COUNT);
 *  - space - заполняющий символ вместо пробелов.
 */
bool screen_t::print_fix(
        int num, uint8_t decimals,
        uint8_t dig_first, uint8_t dig_last,
        uint8_t space)
{
    bool negative = false;
    int dig_with_dp = dig_last - decimals;
  
    /*  Для отрицательного числа выделяем положительную часть,
        а минус запоминаем */
    if (num < 0) {
        negative = true;
        num = -num;
    }
  
    /*  Выводим число поразрядно - от единиц и далее - пока число
        не "закончится", либо пока не закончится место для числа */
    for (int i = dig_last; i >= dig_first; i--) {
        /* В конце выводим минус */
        if (num == 0 && i < dig_with_dp && negative) {
            digits_[i - 1] = SIGN_MINUS; /* Минус */
            negative = false;
        }
        /* Выводим числа поразрядно. Вместо ведущих нулей - пробелы */
        else {
            uint8_t n = (num > 0 || i >= dig_with_dp ? c_digits0_9[num % 10] : space);
            if (decimals != 0 && i == dig_with_dp) n |= SIGN_DP; /* Точка */
            digits_[i - 1] = n;
        }
        num /= 10;
    }

    /* Если число не вместилось, сигнализируем об ошибке */
    return (num != 0 || negative == true ? false : true);
}

/***********************************************************************
 *  Вспомогательная функция для анимации - "отправляем" знак вверх
 *  Для следующего шага надо заново запустить функцию, передав ей
 *  полученный результат. Всего шагов - 2. Третий шаг приведёт
 *  к пустоте.
 */
uint8_t screen_t::anim_send_up(uint8_t digit)
{
    uint8_t res = 0;

    /* Точка просто исчезает */
    if (digit & 0b00100000) res |= 0b10000000;
    if (digit & 0b00010000) res |= 0b00000010;
    if (digit & 0b00000100) res |= 0b00000001;
    if (digit & 0b00000010) res |= 0b01000000;

    return res;
}

/***********************************************************************
 *  Вспомогательная функция для анимации - "принимаем" знак снизу
 *  Параметры:
 *  - digit - знак;
 *  - step - номер шага: 
 *    - 0 - пусто;
 *    - 1, 2 - промежуточные шаги;
 *    - 3 - сам знак.
 */
uint8_t screen_t::anim_take_from_bottom(uint8_t digit, uint8_t step)
{
    uint8_t res = 0;
  
    /* F-A-E-D-Dp-C-G-B
     *     A(6)
     *  F(7)  B(0)
     *     G(1)
     *  E(5)  C(2)   
     *     D(4)  Dp(3)
     */
    switch (step) {
    case 0:
        res = 0;
        break;
      
    case 1:
        if (digit & 0b01000000) res |= 0b00010000;
        break;

    case 2:
        if (digit & 0b10000000) res |= 0b00100000;
        if (digit & 0b01000000) res |= 0b00000010;
        if (digit & 0b00000010) res |= 0b00010000;
        if (digit & 0b00000001) res |= 0b00000100;
        break;

    default:
        res = digit;
    }

    return res;
}

/***********************************************************************
 *  Вспомогательная функция для анимации - "отправляем" знак вниз
 *  Для следующего шага надо заново запустить функцию, передав ей
 *  полученный результат. Всего шагов - 2. Третий шаг приведёт
 *  к пустоте.
 */
uint8_t screen_t::anim_send_down(uint8_t digit)
{
    uint8_t res = 0;

    /* Точка просто исчезает */
    if (digit & 0b10000000) res |= 0b00100000;
    if (digit & 0b01000000) res |= 0b00000010;
    if (digit & 0b00000010) res |= 0b00010000;
    if (digit & 0b00000001) res |= 0b00000100;

    return res;
}

/***********************************************************************
 *  Вспомогательная функция для анимации - "принимаем" знак сверху
 *  Параметры:
 *  - digit - знак;
 *  - step - номер шага: 
 *    - 0 - пусто;
 *    - 1, 2 - промежуточные шаги;
 *    - 3 - сам знак.
 */
uint8_t screen_t::anim_take_from_above(uint8_t digit, uint8_t step)
{
    uint8_t res = 0;
  
    /* F-A-E-D-Dp-C-G-B
     *     A(6)
     *  F(7)  B(0)
     *     G(1)
     *  E(5)  C(2)   
     *     D(4)  Dp(3)
     */
    switch (step) {
    case 0:
        res = 0;
        break;
      
    case 1:
        if (digit & 0b00010000) res |= 0b01000000;
        break;

    case 2:
        if (digit & 0b00100000) res |= 0b10000000;
        if (digit & 0b00010000) res |= 0b00000010;
        if (digit & 0b00000100) res |= 0b00000001;
        if (digit & 0b00000010) res |= 0b01000000;
        break;

    default:
        res = digit;
    }

    return res;
}

/***********************************************************************
 *  Анимированный "уход"
 *  Параметры:
 *  - anim_type - вид анимации.
 *  
 *  Работа идёт только с текущим экраном.
 */
anim_state_t screen_t::anim_leave(anim_t anim_type)
{
    switch (anim_type) {
    case ANIM_GOLEFT:
        digits_[3] = digits_[2];
        digits_[2] = digits_[1];
        digits_[1] = digits_[0];
        digits_[0] = 0;
        break;

    case ANIM_GORIGHT:
        digits_[0] = digits_[1];
        digits_[1] = digits_[2];
        digits_[2] = digits_[3];
        digits_[3] = 0;
        break;
    
    case ANIM_GODOWN:
        for (int i = 0; i < DIG_COUNT; i++)
            digits_[i] = anim_send_up( digits_[i]);
        break;
    
    case ANIM_GOUP:
        for (int i = 0; i < DIG_COUNT; i++)
            digits_[i] = anim_send_down( digits_[i]);
        break;
    } /* switch (anim_type) */

    return is_empty() ? ANIM_LEAVE_STOP : ANIM_LEAVE;
}

/***********************************************************************
 *  Анимированный "приход"
 *  Параметры:
 *  - anim_type - вид анимации;
 *  - p_step - номер шага.
 */
anim_state_t screen_t::anim_come(
    anim_t anim_type, const screen_t &new_screen, uint8_t *p_step)
{
    anim_state_t anim_state = ANIM_COME;
    uint8_t step = *p_step;
    
    switch (anim_type) {
    case ANIM_GOLEFT:
        /* Пропускаем пустые символы */
        if (step == 0)
            for (; step < DIG_COUNT; step++)
                if (new_screen.digits_[DIG_COUNT - 1 - step] != 0)
                    break;

        /*  Шаг 0 - появляется первый (крайний правый) символ
            Шаг 3 - появляется последний (крайний левый) символ */
        if (step >= 3) {
            step = 3;
            anim_state = ANIM_STOP;
        }

        for (int8_t i = 0; i <= step; i++)
            digits_[i] = new_screen.digits_[DIG_COUNT - 1 - step + i];
        break;

    case ANIM_GORIGHT:
        /* Пропускаем пустые символы */
        if (step == 0)
            for (; step < DIG_COUNT; step++)
                if (new_screen.digits_[step] != 0)
                    break;

        /*  Шаг 0 - появляется первый (крайний левый) символ
            Шаг 3 - появляется последний (крайний правый) символ */
        if (step >= 3) {
            step = 3;
            anim_state = ANIM_STOP;
        }

        for (int8_t i = 0; i <= step; i++)
            digits_[DIG_COUNT - 1 - step + i] = new_screen.digits_[i];
        break;
    
    case ANIM_GODOWN:
        /*  Шаг 0 - появляются верхние планки символов
            Шаг 2 - появляется символ целиком */
        if (step >= 2) {
            step = 2;
            anim_state = ANIM_STOP;
        }

        for (int i = 0; i < 4; i++)
            digits_[i] = anim_take_from_bottom(
                new_screen.digits_[i], step + 1);
        break;
      
    case ANIM_GOUP:
        /*  Шаг 0 - появляются нижние планки символов
            Шаг 2 - появляется символ целиком */
        if (step >= 2) {
            step = 2;
            anim_state = ANIM_STOP;
        }

        for (int i = 0; i < 4; i++)
            digits_[i] = anim_take_from_above(
                new_screen.digits_[i], step + 1);
        break;
    
    default:
        copy( new_screen);
        anim_state = ANIM_STOP;

    } /* switch (anim_type) */

    *p_step = step + 1;
    
    return anim_state;
}

/***********************************************************************
 *  Анимация
 *  Параметры:
 *  - new_screen - массив новые значений индикатора;
 *  - anim_type - вид анимации;
 *  - step_delay - задержка между шагами анимации.
 */
void screen_t::anim(
    const screen_t &new_screen, anim_t anim_type, uint16_t step_delay)
{
    const uint8_t *new_digits = new_screen.digits_;
    
    /* Уходим */
    while (anim_leave(anim_type) != ANIM_LEAVE_STOP)
        delay(step_delay);

    set_brightness( new_screen.brightness_);

    /* Приходим */
    uint8_t anim_step = 0;
    do {
        delay(step_delay);
    } while (anim_come( anim_type, new_screen, &anim_step) !=
        ANIM_STOP);
}

/***********************************************************************
 * Инициализация индикатора
 */
indicator_t::indicator_t()
{
    g_one_indicator = this;

    /* B0-B7 - аноды индикаторы */
    DDRB   = 0b11111111; /* output */
    PORTB  = 0b00000000; /* low */
    
    /* C2-C5 - катоды индикаторы */
    DDRC  |= 0b00111100; /* output */
    PORTC |= 0b00111100; /* high */
   
    /* Настраиваем таймер TIMER2 для динамической индикации */
    TCCR2A = 0; /* Используем обычный (Normal) режим работы таймера */
    TCCR2B = 1; /* Устанавливаем предделитель (для первого запуска
        самый минимальный) */
    TCNT2 = 0;

    start();
}

/***********************************************************************
 * Обработка переполнения счётчика TIMER2
 */
ISR(TIMER2_OVF_vect)
{  
    if (g_one_indicator)
        g_one_indicator->timer_processing();
}

/***********************************************************************
 * Динамическая индикация
 */
void indicator_t::timer_processing()
{
    if (--repeat_counter_) return;

    /* Отключаем индикаторы (катоды к питанию) */
    PORTC |= 0b00111100;

    if (p_copy_screen_)
        brightness_ = p_copy_screen_->get_brightness();

    const indicator_mode_t &mode = c_indicator_modes[brightness_];

    /* В самом ярком режиме пауза не используется */
    if (digits_n_ == 4 && mode.prescaler_off == 0)
        digits_n_ = 0;

    if (digits_n_ < 4) {
        
        /* Анимация экрана */
        if (digits_n_ == 0) {
            if (anim_state_ != ANIM_STOP) {
                /* Шаги анимации */
                if (millis() - anim_timestamp_ >= step_delay_) {
                    anim_state_ = (anim_state_ == ANIM_LEAVE ?
                        anim_leave(anim_type_) : anim_come(
                            anim_type_, *p_copy_screen_, &anim_step_));
                    
                    if (anim_state_ == ANIM_LEAVE_STOP)
                        p_copy_screen_ = p_new_copy_screen_;

                    anim_timestamp_ = millis();
                }
            }
            /*  Обновляем значения экрана, если задан экран
                для копирования */
            else if (p_copy_screen_)
                copy(*p_copy_screen_);
        }

        /* Режим горения. Поочерёдно "зажигаем" знаки */
        TCCR2B = mode.prescaler_on;
        repeat_counter_ = mode.on_count;

        if (brightness_) {
            PORTB = digits_[digits_n_];
            PORTC &= ~(1 << (5 - digits_n_)) | 0b11000011; /* Нужный
                катод на землю*/
        }
            
        digits_n_++;
    }
    else {
        /*  Режим паузы. На время выключаем экран,
            чтобы уменьшить яркость */
        TCCR2B = mode.prescaler_off;
        repeat_counter_ = mode.off_count;
    
        digits_n_ = 0;
    }
}

