/*
 * log.h
 *
 *  Created on: Feb 17, 2026
 *      Author: mark
 */

#ifndef INC_LOG_H_
#define INC_LOG_H_

#include "stm32f1xx_hal.h"

typedef struct {
	UART_HandleTypeDef uart_handle;
} LOG_CONFIG_ts;

int log_init(LOG_CONFIG_ts *log_config);
int log_start(void);
int log_print(char *msg, ...);

#endif /* INC_LOG_H_ */
