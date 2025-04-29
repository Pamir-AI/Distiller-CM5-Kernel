// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for configuring the TLV320AIC3204 audio codec via
 * I2C register writes.
 *
 * Copyright (C) 2025 PamirAI Incorporated - http://www.pamir.ai/
 *	Utsav Balar <utsavbalar1231@gmail.com
 */

/**
 * TODOs:
 * - Add support for parsing defaults from device tree
 * - Use latest kernel APIs for I2C communication and sysfs
 * - Use register names from the datasheet instead of magic numbers
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/sysfs.h>

/**
 * struct reg_val - register value pair
 * @reg: register address
 * @val: register value
 */
struct reg_val {
	u8 reg;
	u8 val;
};

/**
 * struct pamir_ai_i2c_sound_data - private data for pamir AI sound
 * @client: I2C client
 * @dev: device structure
 * @volume: volume level (0-100)
 * @input_gain: input gain level (0-100)
 */
struct pamir_ai_i2c_sound_data {
	struct i2c_client *client;
	struct device *dev;
	u8 volume;
	u8 input_gain;
};

/**
 * Initialization sequence for the AIC3204 device,
 */
static const struct reg_val init_sequence[] = {
	/* select page 0 */
	{ 0x01, 0x01 }, /* software reset */
	{ 0x00, 0x00 }, /* select page 0 */
	{ 0x0b, 0x81 }, /* ndac = 1, powered on */
	{ 0x0c, 0x84 }, /* mdac = 2, modified for 3mhz */
	{ 0x12, 0x81 }, /* nadc = 1, powered on */
	{ 0x13, 0x84 }, /* madc = 4 */
	{ 0x19, 0x07 }, /* cdiv_clkin = adc_mod_clk */
	{ 0x1a, 0x81 }, /* divider = 1, powered up */
	{ 0x34, 0x10 }, /* set gpio output */
	{ 0x00, 0x01 }, /* select page 1 */
	{ 0x02, 0x09 }, /* power up avdd ldo */
	{ 0x01, 0x08 }, /* disable weak avdd */
	{ 0x02, 0x01 }, /* enable master analog power */
	{ 0x47, 0x32 }, /* input power-up time 3.1ms */
	{ 0x7b, 0x01 }, /* ref charging time 40ms */
	{ 0x00, 0x00 }, /* select page 0 */
	{ 0x37, 0x0e }, /* change mfp4 */
	{ 0x38, 0x02 }, /* change mfp3 */
	{ 0x53, 0x23 }, /* adc left volume +20db */
	{ 0x54, 0x23 }, /* adc right volume +20db */
	{ 0x41, 0x30 }, /* adc +24db */
	{ 0x42, 0x30 }, /* adc +24db */
	{ 0x00, 0x01 }, /* select page 1 */
	{ 0x14, 0x25 }, /* de-pop: 5 time constants */
	{ 0x0c, 0x08 }, /* route ldac to hpl */
	{ 0x0d, 0x08 }, /* route rdac to hpr */
	{ 0x0e, 0x08 }, /* route ldac to lol */
	{ 0x0f, 0x08 }, /* route rdac to lor */
	{ 0x09, 0x3c }, /* power up hpl/hpr */
	{ 0x10, 0x07 }, /* unmute hpl, 29db gain */
	{ 0x11, 0x07 }, /* unmute hpr, 29db gain */
	{ 0x12, 0x07 }, /* unmute lol, 29db gain */
	{ 0x13, 0x07 }, /* unmute lor, 29db gain */
	{ 0x00, 0x00 }, /* select page 0 */
	{ 0x41, 0x00 }, /* dac left 0db */
	{ 0x42, 0x00 }, /* dac right 0db */
	{ 0x3f, 0xd6 }, /* power up ldac/rdac */
	{ 0x40, 0x00 }, /* unmute ldac/rdac */
	{ 0x51, 0xdc }, /* start dac and configure adc */
	{ 0x52, 0x00 }, /* unmute adc */
};

/**
 * pamir_ai_i2c_sound_set_volume - set the volume of the AIC3204 device
 * @data: private data structure
 * @volume: volume level (0-100)
 *
 * This function sets the volume of the AIC3204 device by writing to
 * the appropriate registers.
 *
 * Return: 0 on success, negative error code on failure
 */
static int pamir_ai_i2c_sound_set_volume(struct pamir_ai_i2c_sound_data *data,
				     u8 volume)
{
	int ret;
	u8 hp_val, dac_val;

	if (volume > 100)
		volume = 100;
	else if (volume < 0)
		volume = 0;

	data->volume = volume;

	/*
	 * DAC Volume Control (Page 0, Registers 0x41/0x42):
	 * 0x00 = 0dB (no attenuation)
	 * 0xFF to 0x81 = -0.5dB to -63.5dB
	 * 0x01 to 0x30 = +0.5dB to +24dB
	 *
	 * Headphone/Line Driver Gain (Page 1, Registers 0x10-0x13):
	 * Bit D6 = Mute bit (1=mute, 0=unmute)
	 * Bits D5-D0 = Gain value:
	 *   0x00 = 0dB
	 *   0x1D = +29dB (maximum)
	 *   0x3A = -6dB (minimum, can't be muted in this setting)
	 */

	if (volume == 0) {
		/* Mute all outputs by setting mute bit */
		hp_val = 0x40; /* Set mute bit (D6) */
		dac_val = 0x00; /* Use 0dB for DAC (no effect when muted) */
	} else {
		/* Two-stage volume control strategy:
		 * 1. For 1-100: Keep DAC at 0dB for clean signal, adjust HP gain
		 * 2. Fine-tune perceived volume curve using both when needed
		 */

		/* Unmute by clearing mute bit (D6) */
		hp_val = 0x00;

		if (volume <= 20) {
			/* Low volume (1-20): Use -6dB to 0dB for HP + some DAC attenuation */
			hp_val |= 0x3A - ((volume - 1) * (0x3A - 0x00) / 19);
			dac_val = 0xA0; /* Around -32dB attenuation */
		} else if (volume <= 60) {
			/* Medium volume (21-60): Use 0dB to +20dB for HP, no DAC attenuation */
			hp_val |= ((volume - 21) * 0x14) /
				  39; /* Map to 0x00-0x14 (0dB to +20dB) */
			dac_val = 0x00; /* 0dB (no attenuation) */
		} else {
			/* High volume (61-100): Use +20dB to +29dB for HP, some DAC boost */
			hp_val |= 0x14 +
				  ((volume - 61) * (0x1D - 0x14)) /
					  39; /* 0x14-0x1D (+20dB to +29dB) */

			/* For very high volumes, add some DAC boost */
			if (volume > 90) {
				/* Add a bit of DAC boost for the highest volumes (91-100) */
				dac_val = 0x04 +
					  ((volume - 91) * (0x10 - 0x04)) /
						  9; /* +2dB to +8dB boost */
			} else {
				dac_val = 0x00; /* 0dB (no boost/attenuation) */
			}
		}
	}

	/* Set page 1 for headphone/line out gain */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, 0x01);
	if (ret < 0)
		return ret;

	/* Set headphone gains (left and right) */
	ret = i2c_smbus_write_byte_data(data->client, 0x10, hp_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, 0x11, hp_val);
	if (ret < 0)
		return ret;

	/* Set line out gains (left and right) */
	ret = i2c_smbus_write_byte_data(data->client, 0x12, hp_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, 0x13, hp_val);
	if (ret < 0)
		return ret;

	/* Set page 0 for DAC volume */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* Set DAC volumes (left and right) */
	ret = i2c_smbus_write_byte_data(data->client, 0x41, dac_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, 0x42, dac_val);
	if (ret < 0)
		return ret;

	dev_info(data->dev,
		 "Volume set to %d%% (hp_val=0x%02x, dac_val=0x%02x)\n", volume,
		 hp_val, dac_val);

	return 0;
}

/**
 * pamir_ai_i2c_sound_set_input_gain - set the input gain of the AIC3204 device
 * @data: private data structure
 * @gain: input gain level (0-100)
 *
 * This function sets the input gain of the AIC3204 device by writing to
 * the appropriate registers.
 *
 * Return: 0 on success, negative error code on failure
 */
static int pamir_ai_i2c_sound_set_input_gain(struct pamir_ai_i2c_sound_data *data,
					 u8 gain)
{
	int ret;
	u8 adc_val;

	if (gain > 100)
		gain = 100;
	else if (gain < 0)
		gain = 0;

	data->input_gain = gain;

	/*
	 * ADC Volume Control (Page 0, Registers 0x53/0x54):
	 * Range: -12dB (0x68) to +20dB (0x28)
	 * 0x00 represents 0dB
	 *
	 * Map 0-100 scale to ADC gain:
	 * - 0-20: -12dB to 0dB (0x68 to 0x00)
	 * - 21-100: 0dB to +20dB (0x00 to 0x28)
	 */

	if (gain <= 20) {
		/* Low gain (0-20): -12dB to 0dB */
		adc_val = 0x68 - ((gain * (0x68 - 0x00)) / 20);
	} else {
		/* Higher gain (21-100): 0dB to +20dB */
		adc_val = ((gain - 20) * 0x28) / 80;
	}

	/* Set page 0 for ADC volume */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* Set ADC volumes (left and right) */
	ret = i2c_smbus_write_byte_data(data->client, 0x53, adc_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, 0x54, adc_val);
	if (ret < 0)
		return ret;

	dev_info(data->dev, "Input gain set to %d%% (adc_val=0x%02x)\n", gain,
		 adc_val);

	return 0;
}

/**
 * pamir_ai_i2c_sound_get_volume - get the current volume of the AIC3204 device
 * @data: private data structure
 *
 * This function retrieves the current volume of the AIC3204 device by
 * reading from the appropriate registers.
 *
 * Return: 0 on success, negative error code on failure
 */
static int pamir_ai_i2c_sound_get_volume(struct pamir_ai_i2c_sound_data *data)
{
	int ret;
	u8 hp_val, dac_val;
	u8 volume = 0;
	bool is_muted = false;

	/* Read headphone gain from page 1 reg 0x10 (left headphone volume) */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, 0x01);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, 0x10);
	if (ret < 0)
		return ret;

	hp_val = ret;

	/* Check mute bit (D6) */
	if (hp_val & 0x40)
		is_muted = true;

	/* Get gain bits (D5-D0) */
	hp_val &= 0x3F;

	/* Read DAC gain from page 0 reg 0x41 (left DAC volume) */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, 0x00);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, 0x41);
	if (ret < 0)
		return ret;

	dac_val = ret;

	/* If muted, volume is 0 */
	if (is_muted) {
		volume = 0;
	} else {
		/* Reverse the mapping logic from set_volume */

		/* Check if we're in the high volume range with DAC boost */
		if (dac_val >= 0x04 && dac_val <= 0x10) {
			/* High volume range (91-100) with DAC boost */
			volume = 91 + ((dac_val - 0x04) * 9) / (0x10 - 0x04);
		}
		/* Check if we're in the high volume range (hp gain between +20dB and +29dB) */
		else if (hp_val >= 0x14 && hp_val <= 0x1D) {
			/* High volume range (61-90) */
			volume = 61 + ((hp_val - 0x14) * 39) / (0x1D - 0x14);
			if (volume > 90)
				volume =
					90; /* Cap at 90 if no DAC boost detected */
		}
		/* Check if we're in the medium volume range (hp gain between 0dB and +20dB) */
		else if (hp_val <= 0x14 && dac_val == 0x00) {
			/* Medium volume range (21-60) */
			volume = 21 + (hp_val * 39) / 0x14;
		}
		/* Check if we're in the low volume range (hp gain between -6dB and 0dB) */
		else if (hp_val >= 0x00 && hp_val <= 0x3A && dac_val == 0xA0) {
			/* Low volume range (1-20) */
			volume = 1 + ((0x3A - hp_val) * 19) / (0x3A - 0x00);
		}
		/* Fallback if the registers are in an unexpected state */
		else {
			/* Try to make a best guess based on HP gain */
			if (hp_val <= 0x00) {
				volume = 21; /* 0dB = ~21% volume */
			} else if (hp_val <= 0x14) {
				volume = 21 + (hp_val * 39) /
						      0x14; /* 0dB to +20dB */
			} else if (hp_val <= 0x1D) {
				volume = 61 +
					 ((hp_val - 0x14) * 39) /
						 (0x1D -
						  0x14); /* +20dB to +29dB */
			} else {
				volume =
					20; /* Default to 20% if in attenuation range */
			}
		}
	}

	if (volume > 100)
		volume = 100;
	data->volume = volume;

	return 0;
}

/**
 * pamir_ai_i2c_sound_get_input_gain - get the current input gain of the AIC3204 device
 * @data: private data structure
 *
 * This function retrieves the current input gain of the AIC3204 device by
 * reading from the appropriate registers.
 *
 * Return: 0 on success, negative error code on failure
 */
static int pamir_ai_i2c_sound_get_input_gain(struct pamir_ai_i2c_sound_data *data)
{
	int ret;
	u8 adc_val;
	u8 gain;

	/* Set page 0 for ADC volume */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, 0x00);
	if (ret < 0)
		return ret;

	/* Read ADC gain from register 0x53 (left ADC volume) */
	ret = i2c_smbus_read_byte_data(data->client, 0x53);
	if (ret < 0)
		return ret;

	adc_val = ret & 0x7F; /* Mask out the reserved bit */

	/* Convert the ADC value to gain percentage */
	if (adc_val >= 0x68) {
		/* Minimum gain (-12dB or below) */
		gain = 0;
	} else if (adc_val > 0x00) {
		/* -12dB to 0dB (0x68 to 0x00) maps to 0-20% */
		gain = ((0x68 - adc_val) * 20) / 0x68;
	} else if (adc_val <= 0x28) {
		/* 0dB to +20dB (0x00 to 0x28) maps to 20-100% */
		gain = 20 + ((adc_val * 80) / 0x28);
	} else {
		/* Above +20dB, cap at 100% */
		gain = 100;
	}

	/* Store the read input gain value */
	data->input_gain = gain;

	return 0;
}

/**
 * register_access_show - read a register from the codec
 * @dev: device structure
 * @attr: device attribute
 * @buf: buffer to write the register value to
 *
 * This function allows userspace to read a register from the codec.
 * The format is "page reg" (e.g., "0 53" to read page 0 register 0x53).
 *
 * Return: number of bytes written to buffer, or negative error code
 */
static ssize_t register_access_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pamir_ai_i2c_sound_data *data = dev_get_drvdata(dev);
	int page, reg, value;
	int ret;

	/* Get last page and reg from the previous write */
	if (sscanf(buf, "%d %d", &page, &reg) != 2) {
		dev_err(dev, "Invalid format. Use: 'page reg'\n");
		return -EINVAL;
	}

	if (page < 0 || page > 255 || reg < 0 || reg > 255) {
		dev_err(dev, "Invalid page or register value (0-255)\n");
		return -EINVAL;
	}

	/* First select the page */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, page);
	if (ret < 0) {
		dev_err(dev, "Failed to select page %d: %d\n", page, ret);
		return ret;
	}

	/* Then read the register */
	value = i2c_smbus_read_byte_data(data->client, reg);
	if (value < 0) {
		dev_err(dev, "Failed to read register 0x%02x: %d\n", reg, value);
		return value;
	}

	dev_info(dev, "Read page %d reg 0x%02x: 0x%02x\n", page, reg, value);
	return sprintf(buf, "%d\n", value);
}

/**
 * register_access_store - write to a register of the codec
 * @dev: device structure
 * @attr: device attribute
 * @buf: buffer containing the register and value to write
 * @count: number of bytes in the buffer
 *
 * This function allows userspace to write to a register of the codec.
 * The format is "page reg value" (e.g., "0 41 0" to write 0 to page 0 register 0x41).
 *
 * Return: number of bytes processed, or negative error code
 */
static ssize_t register_access_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct pamir_ai_i2c_sound_data *data = dev_get_drvdata(dev);
	int page, reg, value;
	int ret;

	if (sscanf(buf, "%d %d %d", &page, &reg, &value) != 3) {
		dev_err(dev, "Invalid format. Use: 'page reg value'\n");
		return -EINVAL;
	}

	if (page < 0 || page > 255 || reg < 0 || reg > 255 || value < 0 || value > 255) {
		dev_err(dev, "Invalid parameter(s), valid range is 0-255\n");
		return -EINVAL;
	}

	/* First select the page */
	ret = i2c_smbus_write_byte_data(data->client, 0x00, page);
	if (ret < 0) {
		dev_err(dev, "Failed to select page %d: %d\n", page, ret);
		return ret;
	}

	/* Then write to the register */
	ret = i2c_smbus_write_byte_data(data->client, reg, value);
	if (ret < 0) {
		dev_err(dev, "Failed to write 0x%02x to page %d reg 0x%02x: %d\n",
			value, page, reg, ret);
		return ret;
	}

	dev_info(dev, "Wrote 0x%02x to page %d reg 0x%02x\n", value, page, reg);
	return count;
}

static ssize_t volume_level_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pamir_ai_i2c_sound_data *data = dev_get_drvdata(dev);
	int ret;

	/* Read current volume from hardware */
	ret = pamir_ai_i2c_sound_get_volume(data);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", data->volume);
}

static ssize_t volume_level_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct pamir_ai_i2c_sound_data *data = dev_get_drvdata(dev);
	int volume;
	int ret;

	ret = kstrtoint(buf, 10, &volume);
	if (ret < 0)
		return ret;

	if (volume < 0)
		volume = 0;
	else if (volume > 100)
		volume = 100;

	ret = pamir_ai_i2c_sound_set_volume(data, volume);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t input_gain_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pamir_ai_i2c_sound_data *data = dev_get_drvdata(dev);
	int ret;

	/* Read current input gain from hardware */
	ret = pamir_ai_i2c_sound_get_input_gain(data);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", data->input_gain);
}

static ssize_t input_gain_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct pamir_ai_i2c_sound_data *data = dev_get_drvdata(dev);
	int gain;
	int ret;

	ret = kstrtoint(buf, 10, &gain);
	if (ret < 0)
		return ret;

	if (gain < 0)
		gain = 0;
	else if (gain > 100)
		gain = 100;

	ret = pamir_ai_i2c_sound_set_input_gain(data, gain);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(volume_level);
static DEVICE_ATTR_RW(input_gain);
static DEVICE_ATTR_RW(register_access);

static struct attribute *pamir_ai_i2c_sound_attrs[] = {
	&dev_attr_volume_level.attr,
	&dev_attr_input_gain.attr,
	&dev_attr_register_access.attr,
	NULL,
};

static const struct attribute_group pamir_ai_i2c_sound_attr_group = {
	.attrs = pamir_ai_i2c_sound_attrs,
};

static int pamir_ai_i2c_sound_probe(struct i2c_client *client)
{
	struct pamir_ai_i2c_sound_data *data;
	int i, ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->dev = &client->dev;
	data->volume = 50;
	data->input_gain = 50;

	i2c_set_clientdata(client, data);
	dev_set_drvdata(&client->dev, data);

	dev_info(&client->dev, "Starting initialization sequence\n");
	for (i = 0; i < ARRAY_SIZE(init_sequence); i++) {
		ret = i2c_smbus_write_byte_data(client, init_sequence[i].reg,
						init_sequence[i].val);
		if (ret < 0) {
			dev_err(&client->dev,
				"Failed to write reg 0x%02x with value 0x%02x: %d\n",
				init_sequence[i].reg, init_sequence[i].val,
				ret);
			return ret;
		}
		dev_info(&client->dev, "Wrote 0x%02x to reg 0x%02x\n",
			 init_sequence[i].val, init_sequence[i].reg);
	}
	dev_info(&client->dev,
		 "Initialization sequence completed successfully\n");

	/* Create sysfs interface */
	ret = sysfs_create_group(&client->dev.kobj, &pamir_ai_i2c_sound_attr_group);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to create sysfs group: %d\n",
			ret);
		return ret;
	}

	/* Set initial volume and input gain */
	ret = pamir_ai_i2c_sound_set_volume(data, data->volume);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to set initial volume: %d\n",
			ret);
		sysfs_remove_group(&client->dev.kobj,
				   &pamir_ai_i2c_sound_attr_group);
		return ret;
	}

	ret = pamir_ai_i2c_sound_set_input_gain(data, data->input_gain);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to set initial input gain: %d\n",
			ret);
		sysfs_remove_group(&client->dev.kobj,
				   &pamir_ai_i2c_sound_attr_group);
		return ret;
	}

	return 0;
}

static void pamir_ai_i2c_sound_remove(struct i2c_client *client)
{
	struct pamir_ai_i2c_sound_data *data = i2c_get_clientdata(client);

	if (data)
		sysfs_remove_group(&client->dev.kobj,
				   &pamir_ai_i2c_sound_attr_group);
}

static const struct i2c_device_id pamir_ai_i2c_sound_id[] = {
	{ "pamir-ai-i2c-sound", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, pamir_ai_i2c_sound_id);

static const struct of_device_id pamir_ai_i2c_sound_of_match[] = {
	{ .compatible = "pamir-ai,i2c-sound" },
	{}
};
MODULE_DEVICE_TABLE(of, pamir_ai_i2c_sound_of_match);

static struct i2c_driver pamir_ai_i2c_sound_driver = {
	.driver = {
		.name = "pamir-ai-i2c-sound",
		.of_match_table = pamir_ai_i2c_sound_of_match,
	},
	.probe = pamir_ai_i2c_sound_probe,
	.remove = pamir_ai_i2c_sound_remove,
	.id_table = pamir_ai_i2c_sound_id,
};

module_i2c_driver(pamir_ai_i2c_sound_driver);

MODULE_AUTHOR("PamirAI, Inc");
MODULE_DESCRIPTION("I2C driver for configuring the TLV320AIC3204 audio codec");
MODULE_LICENSE("GPL v2");
