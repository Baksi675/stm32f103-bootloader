#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bl.h"
#include "config.h"
#include "stm32f1xx_hal.h"
#include "log.h"

#define ACK_CODE			0xAA
#define NACK_CODE			0xDD

#define SRAM_SIZE			(20U * 1024U)
#define SRAM_END			(SRAM_BASE + (SRAM_SIZE - 1U))

#define PAGE_NUM			128

#define CMD_GET_VER			0x01U
#define CMD_GET_CMDS		0x02U
#define CMD_GET_CID			0x03U
#define CMD_GET_RDP			0x04U
#define CMD_SET_RDP			0x05U
#define CMD_GET_WRP 		0x06U
#define CMD_SET_WRP 		0x07U
#define CMD_ERASE			0x08U
#define CMD_WRITE			0x09U
#define CMD_READ			0x0AU
#define CMD_PROGRAM			0x0BU
#define CMD_JUMP 			0x0CU
#define CMD_RST				0x0DU


struct internal_state_s {
	UART_HandleTypeDef uart_handle;
	CRC_HandleTypeDef crc_handle;
	uint8_t rx_buf[CONFIG_BL_RX_BUF_SIZE];
	bool initialized;
	bool started;
};
static struct internal_state_s internal_state;

static const uint8_t supported_cmds[] = {
    CMD_GET_VER,
    CMD_GET_CMDS,
    CMD_GET_CID,
    CMD_GET_RDP,
	CMD_SET_RDP,
	CMD_GET_WRP,
	CMD_SET_WRP,
	CMD_ERASE,
	CMD_WRITE,
	CMD_READ,
	CMD_PROGRAM,
	CMD_JUMP,
	CMD_RST,
};

static int cmd_get_ver_handler(void);
static int cmd_get_cmds_handler(void);
static int cmd_get_cid_handler(void);
static int cmd_get_rdp_handler(void);
static int cmd_set_rdp_handler(uint8_t rdp);
static int cmd_get_wrp_handler(void);
static int cmd_set_wrp_handler(uint8_t start_page, uint8_t num_page, uint8_t wp);
static int cmd_erase_handler(uint8_t start_page, uint8_t num_page);
static int cmd_write_handler(uint32_t addr, uint8_t payload_len, uint8_t *payload);
static int cmd_read_handler(uint32_t addr, uint32_t num_bytes);
static int cmd_program_handler(uint8_t page, uint8_t page_round, uint8_t payload_len, uint8_t *payload);
static int cmd_jump_handler(uint32_t addr);
static int cmd_rst_handler(void);

static uint32_t get_crc(uint8_t *data, uint32_t data_len);
static int send_nack(void);

typedef struct {
	uint8_t msg_len;
	uint8_t cmd_code;
	uint8_t data[CONFIG_BL_RX_BUF_SIZE - 6];
	uint32_t crc;
}RECV_MSG_INFO_ts;
static RECV_MSG_INFO_ts recv_msg_handler(void);

typedef enum {
	CRC_RES_OK,
	CRC_RES_FAIL
} CRC_RES_te;
static CRC_RES_te verify_crc(uint8_t *data, uint32_t data_len, uint32_t crc);

typedef enum {
	ADDR_RES_OK,
	ADDR_RES_FAIL
} ADDR_RES_te;
static ADDR_RES_te verify_addr(uint32_t addr, bool verify_t_bit);

typedef enum {
	SECT_RES_OK,
	SECT_RES_FAIL
}SECT_RES_te;
static SECT_RES_te verify_page(uint8_t start_page, uint8_t num_page);

/**
 * @brief Initializes the bootloader internal state.
 *
 * @param[in] bl_config Pointer to the bootloader configuration structure.
 *
 * @return Error.
 */
int bl_init(BL_CONFIG_ts *bl_config) {
	if(internal_state.initialized) {
		return -1;
	}

	internal_state.uart_handle = bl_config->uart_handle;
	internal_state.crc_handle = bl_config->crc_handle;

	internal_state.initialized = true;

	return 0;
}

/**
 * @brief Allows the bootloader to run.
 *
 * @return Error.
 */
int bl_start(void) {
	if(!internal_state.initialized || internal_state.started) {
		return -1;
	}

	internal_state.started = true;

	return 0;
}

/**
 * @brief Received messages through UART connection. Decodes the messages and calls the appropriate command handlers.
 *
 * @return Error.
 */
int bl_run(void) {
	if(!internal_state.started) {
		return -1;
	}

	// Receive length information
	HAL_UART_Receive(&internal_state.uart_handle, internal_state.rx_buf, 1, HAL_MAX_DELAY);
	// Receive packet
	HAL_UART_Receive(&internal_state.uart_handle, internal_state.rx_buf + 1, internal_state.rx_buf[0], HAL_MAX_DELAY);

	RECV_MSG_INFO_ts recv_msg_info = recv_msg_handler();

	log_print("bootloader: received the following message\r\n");
	log_print("message length: %d\r\n", recv_msg_info.msg_len);
	log_print("command code: 0x%01x\r\n", recv_msg_info.cmd_code);
	log_print("data: ");;
	for(uint32_t i = 0; i < recv_msg_info.msg_len - 5; i++) {
		log_print("0x%01x ", recv_msg_info.data[i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", recv_msg_info.crc);

	// Check if message length is smaller than minimum allowed message length
	if(recv_msg_info.msg_len < 5) {
		send_nack();

		return -1;
	}

	// Check if CRC is valid
	CRC_RES_te crc_check_res = verify_crc(internal_state.rx_buf, (recv_msg_info.msg_len + 1) - 4, recv_msg_info.crc);
	if(crc_check_res != CRC_RES_OK) {
		send_nack();

		return -1;
	}

	switch(recv_msg_info.cmd_code) {
		case CMD_GET_VER:
		{
			cmd_get_ver_handler();
			break;
		}
		case CMD_GET_CMDS:
		{
			cmd_get_cmds_handler();
			break;
		}
		case CMD_GET_CID:
		{
			cmd_get_cid_handler();
			break;
		}
		case CMD_GET_RDP:
		{
			cmd_get_rdp_handler();
			break;
		}
		case CMD_JUMP:
		{
			uint32_t addr;
			memcpy(&addr, recv_msg_info.data, sizeof(uint32_t));
			cmd_jump_handler(addr);
			break;
		}
		case CMD_ERASE:
		{
			uint8_t start_page;
			uint8_t num_page;
			memcpy(&start_page, recv_msg_info.data, sizeof(uint8_t));
			memcpy(&num_page, recv_msg_info.data + 1, sizeof(uint8_t));
			cmd_erase_handler(start_page, num_page);
			break;
		}
		case CMD_WRITE:
		{
			uint32_t addr;
			uint8_t payload_len;
			uint8_t *payload;

			memcpy(&addr, recv_msg_info.data, sizeof(uint32_t));
			memcpy(&payload_len, recv_msg_info.data + 4, sizeof(uint8_t));
			payload = recv_msg_info.data + 5;

			cmd_write_handler(addr, payload_len, payload);
			break;
		}
		case CMD_READ:
		{
			uint32_t addr;
			uint8_t num_bytes;

			memcpy(&addr, recv_msg_info.data, sizeof(uint32_t));
			memcpy(&num_bytes, recv_msg_info.data + 4, sizeof(uint8_t));

			cmd_read_handler(addr, num_bytes);
			break;
		}
		case CMD_RST:
		{
			cmd_rst_handler();
			break;
		}
		case CMD_PROGRAM:
		{
			uint8_t page;
			uint8_t page_round;
			uint8_t payload_len;
			uint8_t *payload;

			memcpy(&page, recv_msg_info.data, sizeof(uint8_t));
			memcpy(&page_round, recv_msg_info.data + 1, sizeof(uint8_t));
			memcpy(&payload_len, recv_msg_info.data + 2, sizeof(uint8_t));
			payload = recv_msg_info.data + 3;

			cmd_program_handler(page, page_round, payload_len, payload);
			break;
		}
		case CMD_SET_RDP:
		{
			uint8_t rdp;

			memcpy(&rdp, recv_msg_info.data, sizeof(uint8_t));

			cmd_set_rdp_handler(rdp);
			break;
		}
		case CMD_GET_WRP:
		{
			cmd_get_wrp_handler();
			break;
		}
		case CMD_SET_WRP:
		{
			uint8_t start_page;
			uint8_t num_page;
			uint8_t wp;

			memcpy(&start_page, recv_msg_info.data, sizeof(uint8_t));
			memcpy(&num_page, recv_msg_info.data + 1, sizeof(uint8_t));
			memcpy(&wp, recv_msg_info.data + 2, sizeof(uint8_t));

			cmd_set_wrp_handler(start_page, num_page, wp);
			break;
		}
		default:
			return -1;
			break;
	}

	return 0;
}

/**
 * @brief Gets the CRC of the given data.
 *
 * @param[in] data Pointer to the data buffer.
 * @param[in] data_len Length of the data buffer.
 * @return The calculated 32 bit CRC.
 */
static uint32_t get_crc(uint8_t *data, uint32_t data_len) {
	__HAL_CRC_DR_RESET(&internal_state.crc_handle);

	 uint32_t crc_calculated = 0;

	for(uint32_t i = 0; i < data_len; i++) {
        uint32_t current_data = data[i];
        crc_calculated = HAL_CRC_Accumulate(&internal_state.crc_handle, &current_data, 1);
	}

	return crc_calculated;
}

/**
 * @brief Transmits a not acknowledge message to host.
 *
 * @return Error.
 */
static int send_nack(void) {
	uint8_t msg[] = { 1, NACK_CODE };

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", 1);
	log_print("ack info: 0x%01x\r\n", NACK_CODE);

	HAL_UART_Transmit(&internal_state.uart_handle, msg, sizeof(msg), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Generic function to sort received data in internal state for further processing.
 *
 * @return Received data structure.
 */
static RECV_MSG_INFO_ts recv_msg_handler(void) {
	RECV_MSG_INFO_ts recv_msg_info = { 0 };

	// Save message length
	recv_msg_info.msg_len = internal_state.rx_buf[0];

	// Save command code
	recv_msg_info.cmd_code = internal_state.rx_buf[1];

	// Save CRC
	uint32_t crc = 0;
	memcpy(&crc, &internal_state.rx_buf[recv_msg_info.msg_len - 3], sizeof(uint32_t));
	recv_msg_info.crc = crc;

	// Save optional data for further processing
	if(recv_msg_info.msg_len > 5) {
		for(uint32_t i = 2; i < recv_msg_info.msg_len - 3; i++) {
			recv_msg_info.data[i - 2] = internal_state.rx_buf[i];
		}
	}

	return recv_msg_info;
}

/**
 * @brief Verifies the CRC of a data sequence.
 *
 * @param[in] data Pointer to the data sequence to verify.
 * @param[in] data_len Length of the data to verify.
 * @param[in] crc_expected The expected CRC value.
 * @return CRC_RES_te CRC_RES_OK, CRC_RES_FAIL
 */
static CRC_RES_te verify_crc(uint8_t *data, uint32_t len, uint32_t crc_host)
{
    __HAL_CRC_DR_RESET(&internal_state.crc_handle);

    uint32_t crc_calculated = 0;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t current_data = data[i];
        crc_calculated = HAL_CRC_Accumulate(&internal_state.crc_handle, &current_data, 1);
    }

    if (crc_calculated == crc_host) {
        return CRC_RES_OK;
    }

    return CRC_RES_FAIL;
}

/**
 * @brief Verifies the address to be that of flash memory.
 *
 * @param[in] data The address to be verified.
 * @return ADDR_RES_te ADDR_RES_OK, ADDR_RES_FAIL
 */
static ADDR_RES_te verify_addr(uint32_t addr, bool verify_t_bit) {
	if(verify_t_bit && (addr - 1) % 4 != 0) {
		return ADDR_RES_FAIL;
	}

	if(addr >= FLASH_BASE && addr <= FLASH_BANK1_END) {
		return ADDR_RES_OK;
	}

	return ADDR_RES_FAIL;
}

/**
 * @brief Verifies the pages.
 *
 * @param[in] start_sector The start_sector to be verified.
 * @param[in] num_sector The num_sector to be verified
 * @return SECT_RES_te SECT_RES_OK, SECT_RES_FAIL
 */
static SECT_RES_te verify_page(uint8_t start_page, uint8_t num_page) {
	// To perform mass erase
	if(start_page == 0xFF && num_page == 0) {
		return SECT_RES_OK;
	}

	if(start_page > PAGE_NUM - 1 || (start_page + num_page > PAGE_NUM) || num_page == 0) {
		return SECT_RES_FAIL;
	}

	return SECT_RES_OK;
}

/**
 * @brief Sends bootloader version information to the host.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_get_ver_handler(void) {
	uint8_t ver_len = strlen(CONFIG_BL_VERSION);
	uint8_t ver_buf[ver_len];
	memcpy(ver_buf, CONFIG_BL_VERSION, ver_len);

	uint8_t data_buf[6 + ver_len];

	data_buf[0] = 5 + ver_len;
	data_buf[1] = ACK_CODE;

	for(uint8_t i = 0; i < ver_len; i++) {
		data_buf[2 + i] = ver_buf[i];
	}

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Sends available command list information to the host.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_get_cmds_handler(void) {
	uint32_t supported_cmds_num = sizeof(supported_cmds);
	uint32_t msg_location_cnt = 0;
	uint8_t data_buf[6 + supported_cmds_num];

	data_buf[msg_location_cnt++] = 5 + supported_cmds_num;
	data_buf[msg_location_cnt++] = ACK_CODE;

	for(uint32_t i = 0; i < supported_cmds_num; i++) {
		data_buf[msg_location_cnt++] = supported_cmds[i];
	}

	uint32_t crc = get_crc(data_buf, 2 + supported_cmds_num);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[msg_location_cnt++] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Sends chip identification information to the host.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_get_cid_handler(void) {
	uint32_t msg_location_cnt = 0;
	uint8_t data_buf[8];

	data_buf[msg_location_cnt++] = 7;
	data_buf[msg_location_cnt++] = ACK_CODE;

	uint32_t volatile *p_dbgmcu_idcode = (uint32_t*)0xE0042000;
	uint16_t cid = *p_dbgmcu_idcode & 0xFFF;

	data_buf[msg_location_cnt++] = cid & 0xFF;
	data_buf[msg_location_cnt++] = (cid >> 8) & 0xFF;

	uint32_t crc = get_crc(data_buf, msg_location_cnt);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[msg_location_cnt++] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Sends readout protection information to the host.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_get_rdp_handler(void) {
	uint8_t data_buf[7];

	data_buf[0] = 6;
	data_buf[1] = ACK_CODE;

	FLASH_OBProgramInitTypeDef ob_handle;
	HAL_FLASHEx_OBGetConfig(&ob_handle);
	uint8_t rdp_level = ob_handle.RDPLevel;

	data_buf[2] = rdp_level;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Sets the readout protection of the flash memory.
 *
 * @param[in] rdp Enable or disable readout protection.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_set_rdp_handler(uint8_t rdp) {
	HAL_StatusTypeDef err;
	FLASH_OBProgramInitTypeDef ob;

	ob.OptionType = OPTIONBYTE_RDP;

	if(rdp != 0xEE && rdp != 0xDD) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_Unlock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_OB_Unlock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	// Enable read protection
	if(rdp == 0xEE) {
		ob.RDPLevel = OB_RDP_LEVEL1;
	}
	// Disable read protection
	else if(rdp == 0xDD) {
		ob.RDPLevel = OB_RDP_LEVEL0;
		err = HAL_FLASHEx_OBErase();
		if(err != HAL_OK) {
			send_nack();
			return -1;
		}
	}

	err = HAL_FLASHEx_OBProgram(&ob);
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_OB_Lock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_Lock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	HAL_FLASH_OB_Launch();

	return 0;
}

/**
 * @brief Gets the write protection of the flash memory pages.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_get_wrp_handler(void) {
	uint8_t data_buf[10];

	data_buf[0] = 9;
	data_buf[1] = ACK_CODE;

	uint8_t wrp0 = (*(uint32_t volatile*)0x1FFFF808) & 0xFF;
	uint8_t wrp1 = ((*(uint32_t volatile*)0x1FFFF808) >> 16) & 0xFF;
	uint8_t wrp2 = (*(uint32_t volatile*)0x1FFFF80C) & 0xFF;
	uint8_t wrp3 = ((*(uint32_t volatile*)0x1FFFF80C) >> 16) & 0xFF;

	data_buf[2] = wrp0;
	data_buf[3] = wrp1;
	data_buf[4] = wrp2;
	data_buf[5] = wrp3;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Sets the write protection of the flash memory pages.
 *
 * @param[in] start_page The first page to set the write protection of.
 * @param[in] num_page The number of pages to set the write protection of.
 * @param[in] wp Enable or disable the write protection.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_set_wrp_handler(uint8_t start_page, uint8_t num_page, uint8_t wp) {
	HAL_StatusTypeDef err;
	FLASH_OBProgramInitTypeDef ob;

	ob.OptionType = OPTIONBYTE_WRP;
	ob.Banks = FLASH_BANK_1;

	if(wp == 0xEE) {
		ob.WRPState = OB_WRPSTATE_ENABLE;
	}
	else if(wp == 0xDD) {
		ob.WRPState = OB_WRPSTATE_DISABLE;
	}
	else {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_Unlock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_OB_Unlock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	uint8_t current_page = start_page;
	uint32_t wrp_mask = 0;

	while(current_page < start_page + num_page) {
		wrp_mask |= 1 << (current_page / 4);

		current_page += 4;
	}

	ob.WRPPage = wrp_mask;

	err = HAL_FLASHEx_OBProgram(&ob);
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_OB_Lock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	err = HAL_FLASH_Lock();
	if(err != HAL_OK) {
		send_nack();
		return -1;
	}

	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	HAL_FLASH_OB_Launch();

	return 0;
}

/**
 * @brief Erases the flash memory.
 *
 * @param[in] start_page The first page to erase.
 * @param[in] num_page The number of pages to erase.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_erase_handler(uint8_t start_page, uint8_t num_page) {
	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	SECT_RES_te res = verify_page(start_page, num_page);
	if(res != SECT_RES_OK) {
		log_print("bootloader: invalid arguments");
		return -1;
	}

	FLASH_EraseInitTypeDef flash;

	// Mass erase
	if(start_page == 0xFF) {
		flash.TypeErase = FLASH_TYPEERASE_MASSERASE;
		flash.Banks = FLASH_BANK_1;
	}
	// Page erase
	else {
		flash.TypeErase = FLASH_TYPEERASE_PAGES;
		flash.PageAddress = (start_page * PAGESIZE) + FLASH_BASE;
		flash.NbPages = num_page;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	uint32_t err;
	HAL_FLASH_Unlock();
	HAL_FLASHEx_Erase(&flash, &err);
	HAL_FLASH_Lock();

	if(err != 0xFFFFFFFF) {
		// Erase was unsuccessful
		log_print("bootloader: erase failed\r\n");
		send_nack();
		return -1;
	}

	return 0;
}

/**
 * @brief Writes to the flash memory.
 *
 * @param[in] addr The address to start writing at.
 * @param[in] payload_len The length of the received payload.
 * @param[in] *payload The received payload.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_write_handler(uint32_t addr, uint8_t payload_len, uint8_t *payload) {
	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	ADDR_RES_te res = verify_addr(addr, false);
	if(res != ADDR_RES_OK) {
		log_print("bootloader: invalid address");
		send_nack();
		return -1;
	}

	uint8_t err;

	HAL_FLASH_Unlock();

	for(uint8_t i = 0; i < payload_len; i += 2) {
		uint16_t data = *(uint16_t*)(payload + i);

		err = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, data);
	    if(err != HAL_OK) {
	    	HAL_FLASH_Lock();
	    	send_nack();

	    	return -1;
	    }
	}
	HAL_FLASH_Lock();

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Reads from the flash memory.
 *
 * @param[in] addr The address to start reading at.
 * @param[in] num_bytes Number of bytes to read.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_read_handler(uint32_t addr, uint32_t num_bytes) {
	uint8_t data_buf[6 + num_bytes];

	data_buf[0] = 5 + num_bytes;
	data_buf[1] = ACK_CODE;

	for(uint32_t i = 0; i < num_bytes; i++) {

	    ADDR_RES_te res = verify_addr(addr + i, false);
	    if(res != ADDR_RES_OK) {
	        send_nack();
	        return -1;
	    }

	    data_buf[2 + i] = *(uint8_t*)(addr + i);
	}

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}


/**
 * @brief Erases then writes to the flash memory.
 *
 * @param[in] page The page which to program.
 * @param[in] page_round The n-th cycle of the page to be programmed.
 * @param[in] payload_len The length of the payload.
 * @param[in] *payload The payload to program.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_program_handler(uint8_t page, uint8_t page_round, uint8_t payload_len, uint8_t *payload) {
	if(page_round == 0) {
		FLASH_EraseInitTypeDef flash;

		flash.TypeErase = FLASH_TYPEERASE_PAGES;
		flash.PageAddress = (page * PAGESIZE) + FLASH_BASE;
		flash.NbPages = 1;

		uint32_t err;
		HAL_FLASH_Unlock();
		HAL_FLASHEx_Erase(&flash, &err);
		HAL_FLASH_Lock();

		if(err != 0xFFFFFFFF) {
			// Erase was unsuccessful
			log_print("bootloader: erase failed\r\n");
			send_nack();
			return -1;
		}
	}

	uint8_t err;

	HAL_FLASH_Unlock();

	uint32_t dest_addr = (page * PAGESIZE) + FLASH_BASE + (page_round * 128);

	for(uint8_t i = 0; i < payload_len; i += 2) {
		uint16_t data = *(uint16_t*)(payload + i);

		err = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, dest_addr + i, data);
	    if(err != HAL_OK) {
	    	HAL_FLASH_Lock();
	    	send_nack();

	    	return -1;
	    }
	}
	HAL_FLASH_Lock();

	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	return 0;
}

/**
 * @brief Jumps to the specified address.
 *
 * @param[in] addr The address to jump to.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_jump_handler(uint32_t addr) {
	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	ADDR_RES_te res = verify_addr(addr, false);
	if(res != ADDR_RES_OK) {
		log_print("bootloader: invalid address\r\n");
		send_nack();

		return -1;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	void (*dest)(void);
	dest = (void*)(addr);			// dest = (void*)(addr + 1); If user doesn't specify T bit
	dest();

	return 0;
}

/**
 * @brief Resets the MCU.
 *
 * @return 0 if success, -1 if failed.
 */
static int cmd_rst_handler(void) {
	uint8_t data_buf[6];

	data_buf[0] = 5;
	data_buf[1] = ACK_CODE;

	uint32_t crc = get_crc(data_buf, sizeof(data_buf) - 4);

	for(uint32_t i = 0; i < 4; i++) {
		data_buf[(sizeof(data_buf) - 4) + i] = (crc >> (8 * i)) & 0xFF;
	}

	log_print("bootloader: sending the following message\r\n");
	log_print("message length: %d\r\n", data_buf[0]);
	log_print("ack info: 0x%01x\r\n", ACK_CODE);
	log_print("data: ");
	for(uint32_t i = 0; i < data_buf[0] - 5; i++) {
		log_print("0x%02x ", data_buf[2 + i]);
	}
	log_print("\r\ncrc: 0x%08x\r\n\r\n", crc);

	HAL_UART_Transmit(&internal_state.uart_handle, data_buf, sizeof(data_buf), HAL_MAX_DELAY);

	HAL_NVIC_SystemReset();

	return 0;
}

