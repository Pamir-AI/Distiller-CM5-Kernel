Pamir AI I2C Sound Driver
=========================

This document describes the Pamir AI I2C sound driver for the TLV320AIC3204 audio codec.

Overview
--------

The Pamir AI I2C sound driver (`pamir-ai-i2c-sound`) provides an interface for
configuring the TLV320AIC3204 audio codec via I2C register writes. It allows
for controlling volume, input gain, and direct register access via sysfs.

Device Tree Bindings
--------------------

**Required properties:**

- `compatible`: Must be `"pamir-ai,i2c-sound"`
- `reg`: I2C device address of the codec chip

**Example:**

.. code-block:: dts

    pamir_ai_sound: pamir-ai-i2c-sound@18 {
        reg = <0x18>;
        compatible = "pamir-ai,i2c-sound";
        status = "okay";
    };

Sysfs Interface
---------------

The driver exposes the following sysfs attributes for user space interaction:

1. **`/sys/bus/i2c/devices/i2c-X/X-0018/volume_level`**
   - **Read**: Get current volume (0-100)
   - **Write**: Set volume (0-100)

2. **`/sys/bus/i2c/devices/i2c-X/X-0018/input_gain`**
   - **Read**: Get current input gain (0-100)
   - **Write**: Set input gain (0-100)

3. **`/sys/bus/i2c/devices/i2c-X/X-0018/register_access`**
   - **Read**: Read value from register (format: `"page reg"`)
   - **Write**: Write value to register (format: `"page reg value"`)

Usage Examples
--------------

.. code-block:: bash

    # Set volume to 50%
    echo 50 > /sys/bus/i2c/devices/i2c-1/1-0018/volume_level

    # Get current volume
    cat /sys/bus/i2c/devices/i2c-1/1-0018/volume_level

    # Set input gain to 70%
    echo 70 > /sys/bus/i2c/devices/i2c-1/1-0018/input_gain

    # Read register (page 0, register 0x41)
    echo "0 65" > /sys/bus/i2c/devices/i2c-1/1-0018/register_access
    cat /sys/bus/i2c/devices/i2c-1/1-0018/register_access

    # Write to register (page 0, register 0x41, value 0)
    echo "0 65 0" > /sys/bus/i2c/devices/i2c-1/1-0018/register_access

Register Map
------------

The TLV320AIC3204 uses a paged register model. Key registers include:

**Page 0:**

- `0x41`, `0x42`: DAC volume control (left, right)
- `0x51`: DAC power and playback control
- `0x52`: DAC mute control
- `0x53`, `0x54`: ADC volume control (left, right)

**Page 1:**

- `0x10`, `0x11`: Headphone output volume (left, right)
- `0x12`, `0x13`: Line output volume (left, right)
