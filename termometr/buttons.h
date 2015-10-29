#ifndef BUTTONS_H
#define BUTTONS_H

#include <stddef.h>

#define BUTTONS_COUNT 4

/* Макрос сохранения состояния кнопок в переменной */
#define GET_BUTTONS_HARD_STATE(var) \
    var = PIND & 0b1111

uint8_t test_buttons(uint8_t *p_ctrl_state);

#endif /* BUTTONS_H */

