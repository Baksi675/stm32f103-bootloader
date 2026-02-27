/*
 * log.c
 *
 *  Created on: Feb 17, 2026
 *      Author: mark
 */

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "log.h"
#include "config.h"
#include "stm32f1xx_hal.h"

struct internal_state_s {
	UART_HandleTypeDef uart_handle;
	char format_buf[CONFIG_LOG_FORMAT_BUF_SIZE];
	bool initialized;
	bool started;
};
static struct internal_state_s internal_state;

int log_init(LOG_CONFIG_ts *log_config) {
	if(internal_state.initialized) {
		return -1;
	}

	internal_state.uart_handle = log_config->uart_handle;

	internal_state.initialized = true;

	return 0;
}

int log_start(void) {
	if(!internal_state.initialized || internal_state.started) {
		return -1;
	}

	internal_state.started = true;

	return 0;
}

int log_print(char *format, ...) {
	if(!internal_state.started) {
		return -1;
	}

#if defined(CONFIG_COMPILE_WITH_LOGGING)

	va_list args;
	va_start(args, format);
	vsnprintf(internal_state.format_buf, sizeof(internal_state.format_buf), format, args);

	HAL_UART_Transmit(&internal_state.uart_handle, (uint8_t*)internal_state.format_buf, strlen(internal_state.format_buf), HAL_MAX_DELAY);

	va_end(args);

#endif

	return 0;
}
