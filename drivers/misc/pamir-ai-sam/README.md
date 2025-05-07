# Pamir AI Sensor and Actuator Module (SAM) Driver

This driver provides an interface between a Linux host system and the RP2040 microcontroller in Pamir AI CM5 devices. It enables interaction with hardware components, specifically button inputs and RGB LED control via UART communication.

## Overview

The SAM driver is implemented as a set of modular components:

- **Protocol Core**: Handles packet parsing, validation, and dispatching
- **Input Handling**: Processes button events and integrates with Linux input subsystem
- **LED Handling**: Controls RGB LEDs and integrates with Linux LED subsystem
- **Power Management**: Handles power state changes and battery status reporting
- **Display Control**: Communicates with the E-ink display controller
- **Debug Interface**: Provides diagnostic information and debugging functions
- **System Commands**: Manages overall system control and status reporting
- **Character Device**: Provides a userspace interface for sending/receiving raw packets

## Requirements

- Linux kernel 5.10 or later
- Serial device bus (serdev) support
- LED class support (for LED control)
- Input subsystem support (for button events)

## Usage

### Kernel Configuration

Enable the driver in your kernel configuration:

```
Device Drivers --->
  Misc devices --->
    [*] Pamir AI Sensor and Actuator Module (SAM) driver
```

### Device Tree Configuration

Add the following to your device tree:

```dts
&serial1 {
    status = "okay";
    current-speed = <115200>;
    
    pamir_sam: pamir-sam {
        compatible = "pamir-ai,sam";
        debug-level = <1>;
        ack-required = <0>;
        recovery-timeout-ms = <1000>;
    };
};
```

### Userspace Interface

The driver provides a character device interface at `/dev/pamir-sam` for direct communication with the protocol handler. It also integrates with the Linux input subsystem for button events and the LED subsystem for LED control.

## Testing

A test utility is provided in the `test` directory:

```
$ cd drivers/misc/pamir-ai-sam/test
$ ./pamir_sam_test.py --help
```

Example commands:

```
# Send a ping command
$ ./pamir_sam_test.py ping

# Set the LED to blink red
$ ./pamir_sam_test.py led --mode blink --color 15 0 0

# Monitor incoming packets
$ ./pamir_sam_test.py monitor
```

## License

This driver is distributed under the terms of the GNU General Public License v2. 
