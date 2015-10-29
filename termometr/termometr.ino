/***********************************************************************************************************
 * Термометр, вариант из общего шаблона:
 *  - индикатор подключен напрямую к Atmega328p;
 *  - два датчика - внутренний и внешний DS18B20;
 *  - четыре кнопки.
 *
 *  (c) Дунаев В.В., 2015
 *
 *  B0-B7 - аноды индикатора в последовательности B-G-C-Dp-D-E-A-F (для PORTB: F-A-E-D-Dp-C-G-B)
 *  C2-C5 - катоды индикаторы в последовательности D4-D3-D2-D1 (для PORTC: x-x-D1-D2-D3-D4-x-x)
 *  D0-D3 - кнопки в последовательности [1]-[2]-[3]-[4] (для PORTD: x-x-x-x-[4]-[3]-[2]-[1])
 *  D7 - температурные датчики DS18B20 (для PORTD: DS-x-x-x-x-x-x-x)
 */

/* Записи в EEPROM */
#define EEPROM_SWAP_SENSORS 0

#include <OneWire.h>
#include <LowPower.h>
#include "indicator.h"
#include "buttons.h"

indicator_t g_indicator;

/* Экраны */
#define SENSOR1     0
#define SENSOR2     1
#define SENSORS_MIN 2
#define SENSOR1_MIN 2
#define SENSOR2_MIN 3
#define SENSORS_MAX 4
#define SENSOR1_MAX 4
#define SENSOR2_MAX 5
#define MESSAGE 254
#define NOTHING 255

screen_t g_screens[7];
uint8_t g_active_screen = SENSOR1;
uint8_t g_last_screen = SENSOR1;
unsigned long g_active_timestamp; /* Время активности (время
    с последнего нажатия кнопки) */

OneWire g_sensors(7); /* Порт D7 на Arduino = D7 на Atmega328p */
uint8_t g_sensors_addr[2][8]; /* Адреса датчиков */
int g_sensors_min[2] = {9999, 9999};
int g_sensors_max[2] = {-999, -999};
bool g_wakeup_by_timer;

unsigned long g_poll_timestamp; /* Метка времени опроса датчиков */

bool g_swap_sensors = false; /* При подключении датчиков невозможно
    задать порядок подключения. Он определяется внутренними адресами
    датчиков. По этой причине для единообразия делаем возможной перемену
    датчиков местами */

/***********************************************************************
 *  Обработка прерывания от нажатия кнопок на портах D0 (PCINT16),
 *  D1 (PCINT17), D2 (PCINT18) и D3 (PCINT19)
 */
ISR(PCINT2_vect)
{
    /* Нам нужно только просыпаться. Всё остальная обработка
        в рабочем цикле */
    g_wakeup_by_timer = false;
}

/***********************************************************************
 *  Задержка на 750мс для получения данных от датчиков
 */
void delay750()
{
    g_indicator.print(SIGN_DP, EMPTY, EMPTY, EMPTY);
    delay(190);
    g_indicator.print(EMPTY, SIGN_DP, EMPTY, EMPTY);
    delay(190);
    g_indicator.print(EMPTY, EMPTY, SIGN_DP, EMPTY);
    delay(190);
    g_indicator.print(EMPTY, EMPTY, EMPTY, SIGN_DP);
    delay(190);
    g_indicator.clear();
}

/***********************************************************************
 *  Запуск температурных датчиков на конвертацию
 */
void convertT()
{
    g_sensors.reset();
    g_sensors.write(0xCC); /* SKIP ROM (обращаемся ко всем датчикам) */
    g_sensors.write(0x44); /* CONVERT T (конверсия значения температуры
        и запись в scratchpad) */
}

/***********************************************************************
 *  Чтение данных с датчика
 */
void update_temp(uint8_t sensor)
{
    /*  Читаем значение температуры. Конвертация температуры уже должна
        быть к этому времени выполнена */
    g_sensors.reset();
    g_sensors.select( g_sensors_addr[sensor]);
    g_sensors.write(0xBE); /* READ SCRATCHPAD (читаем данные) */

    uint8_t tempL = g_sensors.read(); /* "Нижний" байт данных */
    uint8_t tempH = g_sensors.read(); /* "Верхний" байт данных */

    bool negative = false; /* Флаг отрицательного значения */

    /* Получаем целую часть значения */
    uint16_t ti = (tempH << 8) | tempL;
    if (ti & 0x8000) {
        ti = -ti;
        negative = true;
    }

    /*  Переводим дробную часть (4 бита) из двоичной системы в десятичную.
        Оставляем только один знак, округляем значение (+8) */
    uint8_t td = ((ti & 0xF) * 10 + 8) / 16;

    /* Формируем значение с фиксированной точкой */
    int temp = (ti >> 4) * 10 + td;
    if (negative)
        temp = -temp;
    
    g_screens[sensor].print_fix( temp, 1);
    if (temp < g_sensors_min[sensor]) {
        g_sensors_min[sensor] = temp;
        g_screens[sensor + SENSORS_MIN].print_fix( temp, 1);
    }
    
    if (temp > g_sensors_max[sensor]) {
        g_sensors_max[sensor] = temp;
        g_screens[sensor + SENSORS_MAX].print_fix( temp, 1);
    }
}

/***********************************************************************
 *  Запись в EEPROM
 */
void EEPROM_write(uint16_t addr, uint8_t data)
{
    while (EECR & (1<<EEPE)); /* Ждём завершения предыдущей записи */
    
    EEAR = addr;
    EEDR = data;
    EECR |= (1<<EEMPE); /* Так надо, зачем - не понял */
    EECR |= (1<<EEPE); /* Начинаем запись */
}

/***********************************************************************
 *  Чтение из EEPROM
 */
uint8_t EEPROM_read(uint16_t addr)
{
    while (EECR & (1<<EEPE)); /* Ждём завершения предыдущей записи */
    EEAR = addr;
    EECR |= (1<<EERE);
    return EEDR;
}

/***********************************************************************
 * Обмен данными
 */
void swap(void *dat1, void *dat2, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        uint8_t sw = ((uint8_t*)dat1)[i];
        ((uint8_t*)dat1)[i] = ((uint8_t*)dat2)[i];
        ((uint8_t*)dat2)[i] = sw;
    }
}

/***********************************************************************
 * Перемена датчиков местами
 */
void swap_sensors()
{
    g_swap_sensors = !g_swap_sensors;
    swap( g_sensors_addr[0], g_sensors_addr[1], 8);
    EEPROM_write(EEPROM_SWAP_SENSORS, g_swap_sensors);
}

/***********************************************************************
 * Смена режима
 */
void change_screen(uint8_t new_screen, anim_t anim_type)
{
    g_indicator.anim( g_screens[new_screen], anim_type);
    if (g_active_screen != MESSAGE)
        g_last_screen = g_active_screen;
    g_active_screen = new_screen;
}

/***********************************************************************
 * Вывод ошибки на экран
 */
void error(uint8_t errno)
{
    g_screens[MESSAGE].print_int(errno);
    g_screens[MESSAGE].print(CHAR_E, errno < 10 ? DIG3 : DIG2);
    change_screen( MESSAGE, ANIM_NO);
}

/***********************************************************************
 * Функция настройки приложения 
 */
void setup()
{
    /*  Важный комментарий из даташита про неиспользуемые пины
     *  (14.2.6 Unconnected Pins):
     *  "Если некоторые пины не используются, рекомендуется убедиться,
     *  что эти выводы имеют определенный уровень. Хотя большинство
     *  цифровых входов отключены в режимах глубокого сна, как описано
     *  выше, плавающие входы следует избегать, чтобы уменьшить
     *  потребление тока во всех других режимах, когда цифровые входы
     *  включены (сброс, Активный режим и режим ожидания). Самый простой
     *  способ, чтобы гарантировать определенный уровень
     *  у неиспользуемого пина - включение внутреннего подтягивающего
     *  резистора. В этом случае подтягивающие резисторы будут отключены
     *  во время сброса. Если важно низкое энергопотребление в режиме
     *  сброса, рекомендуется использовать внешний подтягивающий
     *  резистор к плюсу или к минусу. Подключение неиспользуемых
     *  выводов непосредственно к VCC или GND не рекомендуется,
     *  поскольку это может привести к чрезмерным токам, если вывод
     *  случайно будет сконфигурирован как выход".
     */

  
    /***
     * Настраиваем порты
     */

    /*  Порт B (аноды индикатора) не трогаем, настраивается отдельно */
    
    /*  Порт C:
     *  C7 - отсутствует
     *  C6 - не используется (input, pull-up)
     *  C5-C2 - катоды индикаторы (не трогаем, настраивается отдельно)
     *  C1-C0 - не используются (input, pull-up)
     */
    DDRC  =  (DDRC & 0b00111100) | 0b00000000;
    PORTC = (PORTC & 0b00111100) | 0b01000011;

    /*  Порт D:
     *  D7 - температурные датчики (не трогаем, настраивается отдельно)
     *  D6-D4 - не используются (input, pull-up)
     *  D3-D0 - кнопки (input, pull-up)
     */
    DDRD  =  (DDRD & 0b10000000) | 0b00000000;
    PORTD = (PORTD & 0b10000000) | 0b01111111;

    /* Устанавливаем прерывания на нажатия кнопок */
    PCICR = (1 << PCIE2);
    PCMSK2 =
        (1 << PCINT16) | (1 << PCINT17)
        | (1 << PCINT18) | (1 << PCINT19);
 
    /***
     * Настраиваем экраны
     */
    
    g_screens[SENSOR1_MIN].set_brightness(7);
    g_screens[SENSOR2_MIN].set_brightness(7);
    g_screens[SENSOR1_MAX].set_brightness(7);
    g_screens[SENSOR2_MAX].set_brightness(7);
    g_active_screen = SENSOR1;
    
    /***
     * Разбираемся с датчиками
     */

    /* Ищем датчики */
    if (!g_sensors.search( g_sensors_addr[0])
            || !g_sensors.search( g_sensors_addr[1])) {
        /* Нет датчика(-ов) */
        error(1);
    }

    uint8_t swap = EEPROM_read( EEPROM_SWAP_SENSORS);
    if (swap == 1) /* Первоначально в EEPROM записано 255 */
        swap_sensors();

    /* Ждём первых результатов от датчиков */
    convertT();
    g_poll_timestamp = millis();
    delay750();
}

/***********************************************************************
 * Основной цикл
 */
void loop()
{
    /* Во время активности опрос датчиков каждые 750 мс */
    if (millis() - g_poll_timestamp > 750) {
        update_temp(SENSOR1);
        update_temp(SENSOR2);
        convertT();
        g_poll_timestamp = millis();
    }
  
    /***  
     *  Обработка сигнала (отпускания) кнопки.
     *  [1] - минимальная температура
     *  [2] - максимальная температура
     *  [3] – 1-й датчик
     *  [4] – 2-й датчик
     *  [1]+[2] - сброс значений min и max
     *  [3]+[4] – настройки: перемена датчиков местами
     */
    uint8_t ctrl_state = 0;
    uint8_t signaled_button = test_buttons(&ctrl_state);

    if (signaled_button) {
        g_active_timestamp = millis();

        if (signaled_button == 1 && ctrl_state == 0b0010 ||
                signaled_button == 2 && ctrl_state == 0b0001) {
            /* [1]+[2] - сброс min-max значений */
            g_sensors_min[0] = g_sensors_min[1] = 9999;
            g_sensors_max[0] = g_sensors_max[1] = -999;
            g_indicator.clear();
            delay(500);
        }
        
        else if (signaled_button == 3 && ctrl_state == 0b1000 ||
                signaled_button == 4 && ctrl_state == 0b0100) {
            /* [3]+[4] - меняем датчики местами  */
            swap_sensors();
        }

        else if (ctrl_state == 0) {

            switch (g_active_screen) {
            case SENSOR1:
                switch (signaled_button) {
                case 1:
                    change_screen( SENSOR1_MIN, ANIM_GODOWN);
                    break;
                case 2:
                    change_screen( SENSOR1_MAX, ANIM_GOUP);
                    break;
                case 4:
                    change_screen( SENSOR2, ANIM_GORIGHT);
                    break;
                }
                break;
                
            case SENSOR2:
                switch (signaled_button) {
                case 1:
                    change_screen( SENSOR2_MIN, ANIM_GODOWN);
                    break;
                case 2:
                    change_screen( SENSOR2_MAX, ANIM_GOUP);
                    break;
                case 3:
                    change_screen( SENSOR1, ANIM_GOLEFT);
                    break;
                }
                break;
            
            case SENSOR1_MIN:
                switch (signaled_button) {
                case 2:
                    change_screen( SENSOR1, ANIM_GOUP);
                    break;
                case 4:
                    change_screen( SENSOR2_MIN, ANIM_GORIGHT);
                    break;
                }
                break;

            case SENSOR2_MIN:
                switch (signaled_button) {
                case 2:
                    change_screen( SENSOR2, ANIM_GOUP);
                    break;
                case 3:
                    change_screen( SENSOR1_MIN, ANIM_GOLEFT);
                    break;
                }
                break;

            case SENSOR1_MAX:
                switch (signaled_button) {
                case 1:
                    change_screen( SENSOR1, ANIM_GODOWN);
                    break;
                case 4:
                    change_screen( SENSOR2_MAX, ANIM_GORIGHT);
                    break;
                }
                break;

            case SENSOR2_MAX:
                switch (signaled_button) {
                case 1:
                    change_screen( SENSOR2, ANIM_GODOWN);
                    break;
                case 3:
                    change_screen( SENSOR1_MAX, ANIM_GOLEFT);
                    break;
                }
                break;

            case MESSAGE:
                change_screen( g_last_screen, ANIM_NO);
                break;
                
            } /* switch g_active_screen */

        } /* else if (ctrl_state == 0) */
            
    } /* if (signaled_button) */

    /* Особенности режимов */
    if (g_active_screen == SENSOR1_MIN || g_active_screen == SENSOR2_MIN) {
        if (millis() - g_active_timestamp > 2000)
            change_screen( g_active_screen - SENSORS_MIN, ANIM_GOUP);
    }
    else if (g_active_screen == SENSOR1_MAX || g_active_screen == SENSOR2_MAX) {
        if (millis() - g_active_timestamp > 2000)
            change_screen( g_active_screen - SENSORS_MAX, ANIM_GODOWN);
    }

    g_indicator.copy( g_screens[g_active_screen]);

    /* Если можно заснуть, засыпаем */
    if (millis() - g_active_timestamp > 5000) {
        cli(); /* Отключаем прерывания, чтобы динамическая индикация
            не обновила индикатор после нашей очистки - иначе есть
            реальная вероятность, что какой-нибудь знак останется
            гореть */
        g_indicator.clear();
        g_wakeup_by_timer = true;
        while (g_wakeup_by_timer) {
            /* Периодически просыпаемся, чтобы считать данные с датчиков */
            LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
            update_temp(SENSOR1);
            update_temp(SENSOR2);
            convertT();
        }
        g_active_timestamp = millis();
        sei();
    }
    else {
        /*  Засыпаем между вызовами TIMER2 для индикации ради экономии
            энергии. TIMER0 не отключаем, т.к. он используется Arduino
            для расчёта времени. Нам он нужен для millis() */
        LowPower.idle(
            SLEEP_FOREVER, ADC_OFF, TIMER2_ON, TIMER1_OFF, TIMER0_ON,
            SPI_OFF, USART0_OFF, TWI_OFF);
    }
}

