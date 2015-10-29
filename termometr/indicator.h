#ifndef INDICATOR_H
#define INDICATOR_H

#include <stddef.h>

/* Изображения цифр, букв и знаков для индикатора
 *      6
 *    7   0
 *      1
 *    5   2   
 *      4   3
 */
#define EMPTY       0
#define DIGIT_0     0b11110101  /* 0 */
#define DIGIT_1     0b00000101  /* 1 */
#define DIGIT_2     0b01110011  /* 2 */
#define DIGIT_3     0b01010111  /* 3 */
#define DIGIT_4     0b10000111  /* 4 */
#define DIGIT_5     0b11010110  /* 5 */
#define DIGIT_6     0b11110110  /* 6 */
#define DIGIT_7     0b01000101  /* 7 */
#define DIGIT_8     0b11110111  /* 8 */
#define DIGIT_9     0b11010111  /* 9 */
#define SIGN_MINUS  0b00000010  /* - */
#define SIGN_DP     0b00001000  /* . */
#define CHAR_A      0b11100111  /* A */
#define CHAR_b      0b10110110  /* b */
#define CHAR_C      0b11110000  /* C */
#define CHAR_c      0b00110010  /* c */
#define CHAR_d      0b00110111  /* d */
#define CHAR_E      0b11110010  /* E */
#define CHAR_F      0b11100010  /* F */
#define CHAR_G      0b11110100  /* G */
#define CHAR_h      0b10100110  /* h */
#define CHAR_I      0b10100000  /* I(слева) */
#define CHAR_IR     DIGIT_1     /* I (справа) */
#define CHAR_i      0b00100000  /* i(слева) */
#define CHAR_iR     0b00000100  /* i(справа) */
#define CHAR_J      0b00010101  /* J */
#define CHAR_L      0b10110000  /* L */
#define CHAR_n      0b00100110  /* n */
#define CHAR_O      DIGIT_0     /* O */
#define CHAR_o      0b00110110  /* o */
#define CHAR_P      0b11100011  /* P */
#define CHAR_r      0b00100010  /* r */
#define CHAR_S      DIGIT_5     /* S */
#define CHAR_t      0b10110010  /* t */
#define CHAR_U      0b10110101  /* U */
#define CHAR_u      0b00110100  /* u */
#define CHAR_Y      0b10010111  /* Y */
#define CHAR_Z      DIGIT_2     /* Z */
#define SIGN_QUOT   0b10000001  /* " */
#define SIGN_APOL   0b10000000  /* '(слева) */
#define SIGN_APOR   0b00000001  /* '(справа) */
#define SIGN_LOW    0b00010000  /* _ */
#define SIGN_HIGH   0b01000000  /* ¯ */

/* Номера знакомест на индикаторе */
#define DIG1 1
#define DIG2 2
#define DIG3 3
#define DIG4 4
#define DIG_COUNT 4

/* Типы анимации */
enum anim_t
{
    ANIM_NO,
    ANIM_GOLEFT,
    ANIM_GORIGHT,
    ANIM_GOUP,
    ANIM_GODOWN
};

/* Состояние анимации */
enum anim_state_t
{
    ANIM_STOP,
    ANIM_LEAVE,
    ANIM_LEAVE_STOP,
    ANIM_COME
};

#define ANIM_STEP_DELAY_DEFAULT 100

class screen_t
{
protected:
    uint8_t digits_[DIG_COUNT] = {0};

    /* Выбор режима индикации заметно влияет на энергопотребление.
     *  Разница между максимальным и предыдущим режимами по
     *  энергозатратам - почти в три раза.
     */
    volatile uint8_t brightness_ = 15;

public:
    void set_brightness(int8_t brightness);
    int8_t get_brightness() const
    {
        return brightness_;
    }

    void clear()
    {
        digits_[0] = 0;
        digits_[1] = 0;
        digits_[2] = 0;
        digits_[3] = 0;
    }

    bool is_empty()
    {
        return digits_[0] == 0 && digits_[1] == 0 &&
            digits_[2] == 0 && digits_[3] == 0;
    }

    void copy(const screen_t &screen)
    {
        digits_[0] = screen.digits_[0];
        digits_[1] = screen.digits_[1];
        digits_[2] = screen.digits_[2];
        digits_[3] = screen.digits_[3];
        
        brightness_ = screen.brightness_;
    }

    void print(
        uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4)
    {
        digits_[0] = d1;
        digits_[1] = d2;
        digits_[2] = d3;
        digits_[3] = d4;
    }

    void print(uint8_t d, uint8_t dig_n)
    {
        if (dig_n >= DIG1 && dig_n <= DIG4)
            digits_[dig_n - 1] = d;
    }

    bool print_fix(
        int num, uint8_t decimals,
        uint8_t dig_first = DIG1, uint8_t dig_last = DIG4,
        uint8_t space = EMPTY);
    
    bool print_int(
        int num,
        uint8_t dig_first = DIG1, uint8_t dig_last = DIG4,
        uint8_t space = EMPTY)
    {
        return print_fix(
            num, 0, dig_first, dig_last, space);
    }

    static uint8_t anim_send_up(uint8_t d);
    static uint8_t anim_take_from_bottom(uint8_t d, uint8_t step);
    static uint8_t anim_send_down(uint8_t d);
    static uint8_t anim_take_from_above(uint8_t d, uint8_t step);
    anim_state_t anim_leave(anim_t anim_type);
    anim_state_t anim_come(
        anim_t anim_type, const screen_t &new_screen, uint8_t *p_step);

    void anim(
        const screen_t &new_screen, anim_t anim_type,
        uint16_t step_delay = ANIM_STEP_DELAY_DEFAULT);

    void blink(uint8_t step);
};

/***********************************************************************
 * Класс индикатора
 */
class indicator_t : public screen_t
{
private:
    uint8_t digits_n_ = 0; /* Счётчик для динамической индикации */
    uint8_t repeat_counter_ = 1; /* Внутренний счётчик
        для динамической индикации */

    const screen_t *p_copy_screen_ = 0;
    const screen_t *p_new_copy_screen_;

    /* Динамическая анимация */
    volatile anim_state_t anim_state_;
    anim_t anim_type_;
    uint16_t step_delay_;
    unsigned long anim_timestamp_;
    uint8_t anim_step_;

public:

    indicator_t();
    
    void timer_processing();

    void stop()
    {
        TIMSK2 = 0;
        PORTB = 0; /* Аноды на землю */
        PORTC |= 0b00111100; /* Катоды к питанию */
    }

    void start()
    {
        TIMSK2 = (1 << TOIE2);
    }

    void set_copy_screen(const screen_t *p_copy_screen)
    {
        p_copy_screen_ = p_copy_screen;
    }
    
    void delayed_anim(
        const screen_t *p_copy_screen, anim_t anim_type,
        uint16_t step_delay = ANIM_STEP_DELAY_DEFAULT)
    {
        while (anim_state_ != ANIM_STOP) ;
        
        cli();
        p_new_copy_screen_ = p_copy_screen;
        anim_type_ = anim_type;
        step_delay_ = step_delay;
        anim_timestamp_ = millis();
        anim_state_ = ANIM_LEAVE;
        anim_step_ = 0;
        sei();
    }
};

#endif /* INDICATOR_H */

