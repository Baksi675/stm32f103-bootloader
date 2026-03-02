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

| Command      | Description |
|--------------|------------|
| `get-ver`    | Get bootloader version |
| `get-cmds`   | Get available command list |
| `get-cid`    | Get MCU chip ID |
| `get-rdp`    | Get MCU flash RDP status |
| `set-rdp`    | Set MCU flash RDP status |
| `erase`      | Erase the flash |
| `write`      | Write to the flash |
| `read`       | Read from the flash |
| `program`    | Erase then writes to the flash |
| `jump`       | Jump to the specified address |
| `rst`        | Reset the MCU |

---

## 📄 License

This project is free to use and modify.  

---

## ⭐ Support

If this project helps you, consider giving it a ⭐ on GitHub!
