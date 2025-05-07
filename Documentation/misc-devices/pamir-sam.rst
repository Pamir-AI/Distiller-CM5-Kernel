===================================
Pamir AI SAM Module
===================================

Overview
--------

The Pamir AI SAM driver provides an interface between a Linux host system and 
the RP2040 microcontroller in Pamir AI devices. It implements a compact,
efficient binary communication protocol designed for low overhead and high 
reliability.

The driver handles various functions including:

- Button input events (via Linux input subsystem)
- LED control (via Linux LED subsystem)
- Power management (power states)
- Display control for E-ink displays
- Debug and diagnostic information
- System management commands

Protocol Design
--------------

The protocol uses a fixed-size 4-byte packet format:

- Byte 0: Type flags (3 most significant bits) + subtype/data (5 least significant bits)
- Byte 1: Data byte 1
- Byte 2: Data byte 2
- Byte 3: Checksum (XOR of bytes 0-2)

This compact design provides several advantages:

- Fixed packet size makes parsing simpler and more reliable
- Low overhead for resource-constrained microcontrollers
- Fast processing on both ends
- Error detection through checksums

Message Types
------------

The protocol supports the following message types:

- Button events: Report button presses and releases
- LED control: Configure RGB LED color and animations
- Power management: Control power states
- Display commands: Control E-ink display updates
- Debug codes: Send diagnostic codes and parameters
- Debug text: Send multi-packet text messages
- System commands: System control functions and status
- Extended commands: Reserved for future expansion

Driver Architecture
------------------

The driver is implemented as a set of modular components:

1. **Protocol Core**: Handles packet parsing, validation, and dispatching
2. **Input Handler**: Processes button events via the Linux input subsystem
3. **LED Handler**: Controls RGB LEDs via the Linux LED subsystem
4. **Power Manager**: Handles power state changes
5. **Display Handler**: Communicates with the E-ink display controller
6. **Debug Handler**: Provides diagnostic information and debug logging
7. **System Handler**: Manages system control and status reporting
8. **Character Device**: Provides user space interface for raw packet I/O

User Interface
-------------

The driver provides multiple interfaces:

- **Character Device**: /dev/pamir-sam for direct communication
- **Input Device**: Standard Linux input device for button events
- **LED Class Device**: Standard Linux LED control

Utilities
---------

A Python test utility is included in the driver's test directory to:

- Test hardware functionality
- Debug communication issues
- Simulate button events
- Control LED state
- Monitor debug information

Configuration
------------

The driver is configured through device tree properties, including:

- Debug level
- Acknowledgment requirements
- Recovery timeout

See the device tree binding documentation for details.

Requirements
-----------

- Linux kernel 5.10 or later
- Serial device bus (serdev) support
- LED class support (for LED control)
- Input subsystem support (for button events) 
