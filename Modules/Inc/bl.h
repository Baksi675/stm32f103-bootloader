/*
 * bl.h
 *
 *  Created on: Feb 17, 2026
 *      Author: mark
 */

#ifndef INC_BL_H_
#define INC_BL_H_

#include "stm32f1xx_hal.h"

typedef struct {
	UART_HandleTypeDef uart_handle;
	CRC_HandleTypeDef crc_handle;
} BL_CONFIG_ts;

int bl_init(BL_CONFIG_ts *bl_config);
int bl_start(void);
int bl_run(void);

#endif /* INC_BL_H_ */
