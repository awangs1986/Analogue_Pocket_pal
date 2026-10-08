/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_OF_INPUT_H
#define TEST_OF_INPUT_H
#include "of_input_types.h"
void of_input_poll(void);
uint32_t of_input_state(int, of_input_state_t *);
void of_input_keyboard_state(of_keyboard_state_t *);
#endif
