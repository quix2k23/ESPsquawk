ESP-IDF 6.0.2+ Setup & GPS Integration Guide

See https://github.com/VOLTEKOVER/ESP_DRONE_REMOTEID/blob/main/README.md for accessing via web interfaace on 192.168.4.1

Link to bin file if you do not want to compile: https://github.com/quix2k23/ESPsquawk/tree/ESPSquawk/bin

Minimum Wiring (ESP32 + Flight Controller)

Flight Controller    ESP32 (or variant)
─────────────────    ─────────────────
TX (UART)       →    GPIO18 (UART#2 RX) 57600 baud. firmware default autoselects NMEA, MPS or Mavlink 
GND             →    GND
5V (BEC)        →    5V / VIN


A comprehensive guide for setting up ESP-IDF 6.0.2+, configuring your project, and integrating GPS with UART.
📋 Prerequisites

Before you begin, ensure you have:

    A supported ESP32-series board

    USB cable for programming

    Computer running Windows, Linux, or macOS

    GPS module (UART or I2C)

    Internet connection for installation

🛠️ Installing ESP-IDF 6.0.2+

ESP-IDF v6.0.2 is the latest stable release. Espressif provides the ESP-IDF Installation Manager (EIM) for all platforms .
Windows

Option A: GUI Installer (Recommended)

    Download and run the Espressif Installation Manager (EIM) GUI

    Click New Installation → Start Easy Installation

    Follow the wizard to install the latest stable version with default settings 

Option B: Command Line
powershell

# Install EIM via WinGet
winget install Espressif.EIM-CLI

# Install latest stable ESP-IDF
eim install

Option C: Interactive Installation
powershell

eim wizard

macOS

    Install dependencies via Homebrew:
    bash

    brew install libgcrypt glib pixman sdl2 libslirp dfu-util cmake python

    Install EIM:
    bash

    brew tap espressif/eim
    brew install eim        # CLI
    # or
    brew install --cask eim-gui  # GUI

    Install ESP-IDF:
    bash

    eim install

Linux

The EIM is also available for Linux distributions. Follow similar steps as macOS, using the appropriate package manager.
🔧 Activating the ESP-IDF Environment

After installation, you must activate the environment in your terminal :
bash

# Find your activation script path (printed after installation)
source "/path/to/activate_idf_v6.0.2.sh"

# On Windows, use the "ESP-IDF Command Prompt" shortcut
# or run export.bat from the ESP-IDF directory

Note: All subsequent ESP-IDF commands must be run in an activated terminal session.
📦 Cloning Your Project
Clone with Submodules
bash

git clone --recursive https://github.com/your-repo/your-project.git
cd your-project

Important: Use --recursive to ensure all submodules are fetched.
🎯 Setting the Target Chip

Set the correct chip target for your board :
bash

idf.py set-target esp32
# or
idf.py set-target esp32s3
# or
idf.py set-target esp32c6

This step clears previous builds and initializes the configuration.
⚙️ Configuring with idf.py menuconfig

The menuconfig utility allows you to configure project-specific settings .
bash

idf.py menuconfig

Key Configuration Sections
1. Serial Flasher Config

    Set the serial port (e.g., /dev/ttyUSB0, COM3)

    Configure baud rate

2. Partition Table

Navigate to Partition Table to choose :

    Single factory app, no OTA – Simple setup

    Factory app, two OTA definitions – For OTA updates

    Custom partition table CSV – Advanced customization

The partition table defines how flash memory is organized. By default, it starts at offset 0x8000 .
🗂️ Partition Table Configuration
Built-in Options

Single Factory App (no OTA):
text

# Name,     Type, SubType, Offset,  Size
nvs,        data, nvs,     0x9000,  0x6000
phy_init,   data, phy,     0xf000,  0x1000
factory,    app,  factory, 0x10000, 1M

The factory app is flashed at offset 0x10000 (64KB) .

Factory App with Two OTA Slots:
text

# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x4000
otadata,    data, ota,     0xd000,   0x2000
phy_init,   data, phy,     0xf000,   0x1000
factory,    app,  factory, 0x10000,  1M
ota_0,      app,  ota_0,   0x110000, 1M
ota_1,      app,  ota_1,   0x210000, 1M

Custom Partition Table

For advanced use, create a partitions.csv file :
csv

# Name,     Type, SubType,  Offset,  Size,   Flags
nvs,        data, nvs,      0x9000,  0x4000
otadata,    data, ota,      0xd000,  0x2000
phy_init,   data, phy,      0xf000,  0x1000
factory,    app,  factory,  0x10000, 1M
ota_0,      app,  ota_0,    ,        1M
ota_1,      app,  ota_1,    ,        1M

Tips:

    Leave the Offset field blank for automatic placement

    Comments start with #

    In menuconfig, select Custom partition table CSV and enter the filename

Adjusting Partition Table Offset

If your bootloader needs more space, adjust the offset :

    In menuconfig, go to Partition Table → Custom partition table offset

    Change CONFIG_PARTITION_TABLE_OFFSET (default: 0x8000)

    Update your partitions.csv accordingly:

        First partition starts at offset + 0x1000

🏗️ Building the Project
bash

# Clean previous builds (optional)
idf.py clean

# Build the project
idf.py build

🔌 Wiring GPS to UART
Hardware Connections

Most GPS modules connect via UART. Here's the standard wiring:
ESP32 GPIO	GPS Module	Notes
GPIO5 (default)	TX (GPS output)	UART RX – receives GPS data
GND	GND	Common ground
5V / 3.3V	VCC	Match voltage levels

Note: The UART TX pin is not needed if you're only receiving GPS data .

Alternative GPIOs: You can change the RX pin in menuconfig under Example Configuration .
Configuration in Menuconfig

Navigate to:
text

Component config → Example Configuration

Set:

    NMEA Parser Ring Buffer Size – Adjust for your data volume

    NMEA Parser Task Stack Size – Stack size for the parser task

    UART RX Pin – The GPIO connected to GPS TX

    NMEA Statement Support – Select which NMEA sentences to parse 

📡 GPS Protocol Auto-Selection

The firmware can automatically select between NMEA, MSP, and MAVLink protocols – making it compatible with various GPS modules and flight controllers.
Supported Protocols
Protocol	Common Use Cases
NMEA	Standard GPS modules, marine navigation
MSP	MultiWii Serial Protocol – used by some flight controllers
MAVLink	Drone telemetry, Pixhawk/ArduPilot systems

This auto-detection feature simplifies integration – just wire the GPS to UART, and the firmware handles the rest.
🔥 Flashing the Firmware
Flash the Project
bash

idf.py -p PORT flash

Replace PORT with your device:

    Windows: COM3, COM4, etc.

    Linux: /dev/ttyUSB0, /dev/ttyACM0

    macOS: /dev/cu.usbserial-*

Flash and Monitor
bash

idf.py -p PORT flash monitor

Erase Flash (if needed)
bash

idf.py -p PORT erase-flash

🖥️ Serial Monitor

To view GPS data output:
bash

idf.py -p PORT monitor

Exit: Press Ctrl + ]

The monitor will display parsed NMEA messages, signal status, and other debug information .
🚀 Common Workflow
bash

# 1. Activate environment
source /path/to/activate_idf_v6.0.2.sh

# 2. Clone project
git clone --recursive https://github.com/your-repo/project.git
cd project

# 3. Set target chip
idf.py set-target esp32

# 4. Configure
idf.py menuconfig
# - Set serial port
# - Configure partition table
# - Set GPS UART pin
# - Select NMEA statements

# 5. Build
idf.py build

# 6. Flash & monitor
idf.py -p PORT flash monitor

🧪 Troubleshooting
Serial Port Issues

    Windows: Check Device Manager for the correct COM port

    Linux/macOS: Ensure you have read/write permissions (sudo may be needed)

GPS No Data

    Verify wiring (TX ↔ RX, GND to GND)

    Check voltage levels (3.3V vs 5V)

    Ensure GPS has a clear sky view for satellite fix

    Verify UART pin configuration matches wiring

Build Errors

    Ensure ESP-IDF environment is activated

    Check for sufficient disk space

    Verify submodules are cloned (git submodule update --init --recursive)

Flash Errors

    Verify the correct port

    Ensure the board is in flashing mode

    Check USB cable quality

📚 Additional Resources

    ESP-IDF Official Documentation

    ESP-IDF GitHub Repository

    NMEA Parser Component

    Partition Tables Guide

This guide was prepared for ESP-IDF v6.0.2+. Always refer to the official documentation for the latest updates and chip-specific details.
