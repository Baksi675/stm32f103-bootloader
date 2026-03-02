# ⚠️ WARNING ⚠️

This markdown file was vibe coded.

---

# 🔄 STM32F103 Bootloader 
### Custom bootloader for STM32F103

A custom bootloader implementation for STM32F103 microcontrollers.

---

## ✨ Features

- Communicates with dfutools host application
- Flash erase / write / read support
- Readout Protection (RDP)
- Chip identification
- Bootloader version detection
- Modular command structure

---

## 🚀 Usage

- Upload the bootloader binary to 0x08000000 address
- Upload the MCU application to 0x08008000 address (modify the linker script and the vector table offset)

---

## 🧩 Available Commands

| Command          | Description |
|--------------|------------|
| `CMD_GET_VER`    | Get bootloader version |
| `CMD_GET_CMDS`   | Get available command list |
| `CMD_GET_CID`    | Get MCU chip ID |
| `CMD_GET_RDP`    | Get MCU flash RDP status |
| `CMD_SET_RDP`    | Set MCU flash RDP status |
| `CMD_GET_WRP`    | Get MCU flash WRP status |
| `CMD_SET_WRP`    | Set MCU flash WRP status |
| `CMD_ERASE`      | Erase the flash |
| `CMD_WRITE`      | Write to the flash |
| `CMD_READ`       | Read from the flash |
| `CMD_PROGRAM`    | Erase then write to the flash |   
| `CMD_JUMP`       | Jump to the specified address |
| `CMD_RST`        | Reset the MCU |

---

## 📄 License

This project is free to use and modify.  

---

## ⭐ Support

If this project helps you, consider giving it a ⭐ on GitHub!
