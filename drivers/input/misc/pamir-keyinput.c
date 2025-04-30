// SPDX-License-Identifier: GPL-2.0-only
/*
 * PamirAI Key Input Driver
 *
 * Copyright (C) 2025 PamirAI Incorporated - http://www.pamir.ai/
 *	Utsav Balar <utsavbalar1231@gmail.com
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>

#define BUF_SIZE 32

// state masks
#define BTN_UP_MASK 0b0001
#define BTN_DOWN_MASK 0b0010
#define BTN_SELECT_MASK 0b0100
#define SHUT_DOWN_MASK 0b1000

// default debounce_ms
#define DEFAULT_DEBOUNCE_MS 50

/**
 * struct pamir_key_input_config - configuration for pamir key input
 * @debounce_ms: debounce time in milliseconds
 * @raw_protocol: use raw protocol without line parsing
 * @report_press_only: only report key press events, not release
 * @recovery_timeout_ms: timeout for UART recovery
 */
struct pamir_key_input_config {
	unsigned int debounce_ms;
	bool raw_protocol;
	bool report_press_only;
	unsigned int recovery_timeout_ms;
};

/**
 * struct pamir_key_input_data - private data for pamir key input
 * @input_dev: pointer to input device
 * @prev_state: previous button state
 * @buf: buffer for receiving data
 * @buf_len: length of the buffer
 * @config: configuration for pamir key input
 * @uart_error: flag for UART error
 * @last_receive_jiffies: last time data was received
 * @last_btn_jiffies: last time each button was pressed
 */
struct pamir_key_input_data {
	struct input_dev *input_dev;
	unsigned int prev_state;
	char buf[BUF_SIZE];
	size_t buf_len;
	struct pamir_key_input_config config;
	bool uart_error;
	unsigned long last_receive_jiffies;
	unsigned long last_btn_jiffies[4];
};

/**
 * process_button_state - process the button state received from UART
 * @priv: private data for pamir key input
 * @state: button state received from UART
 *
 * This function processes the button state received from UART and reports
 * the corresponding key events to the input device.
 */
static void process_button_state(struct pamir_key_input_data *priv,
				 unsigned int state)
{
	struct input_dev *input_dev = priv->input_dev;
	unsigned int changed = state ^ priv->prev_state;
	unsigned long now = jiffies;
	bool debounced_change = false;

	priv->uart_error = false;
	priv->last_receive_jiffies = now;

	dev_info(&input_dev->dev,
		 "Processing button state: 0x%02x (prev: 0x%02x)\n", state,
		 priv->prev_state);

	if (changed & BTN_UP_MASK) {
		/* Check debounce */
		if (time_after(now,
			       priv->last_btn_jiffies[0] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & BTN_UP_MASK) != 0;

			input_report_key(input_dev, KEY_UP, pressed);
			priv->last_btn_jiffies[0] = now;
			debounced_change = true;

			dev_info(&input_dev->dev, "Button UP %s\n",
				 pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed) {
				/* Immediately report release to ensure key doesn't "stick" */
				input_sync(input_dev);
			}
		} else {
			dev_info(&input_dev->dev,
				 "Button UP change ignored (debounce)\n");
		}
	}

	if (changed & BTN_DOWN_MASK) {
		// Check debounce
		if (time_after(now,
			       priv->last_btn_jiffies[1] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & BTN_DOWN_MASK) != 0;

			input_report_key(input_dev, KEY_DOWN, pressed);
			priv->last_btn_jiffies[1] = now;
			debounced_change = true;

			dev_info(&input_dev->dev, "Button DOWN %s\n",
				 pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed) {
				input_sync(input_dev);
			}
		} else {
			dev_info(&input_dev->dev,
				 "Button DOWN change ignored (debounce)\n");
		}
	}

	if (changed & BTN_SELECT_MASK) {
		// Check debounce
		if (time_after(now,
			       priv->last_btn_jiffies[2] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & BTN_SELECT_MASK) != 0;

			input_report_key(input_dev, KEY_ENTER, pressed);
			priv->last_btn_jiffies[2] = now;
			debounced_change = true;

			dev_info(&input_dev->dev, "Button SELECT %s\n",
				 pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed)
				input_sync(input_dev);
		} else {
			dev_info(&input_dev->dev,
				 "Button SELECT change ignored (debounce)\n");
		}
	}

	if (changed & SHUT_DOWN_MASK) {
		// Check debounce
		if (time_after(now,
			       priv->last_btn_jiffies[3] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & SHUT_DOWN_MASK) != 0;

			// input_report_key(input_dev, KEY_POWER, pressed);
			priv->last_btn_jiffies[3] = now;
			debounced_change = true;

			dev_info(&input_dev->dev, "Button POWER %s\n",
				 pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed)
				input_sync(input_dev);
		} else {
			dev_info(&input_dev->dev,
				 "Button POWER change ignored (debounce)\n");
		}
	}

	// update if at least 1 btn change
	if (debounced_change) {
		input_sync(input_dev);
		priv->prev_state = state;
		dev_info(&input_dev->dev, "Button state updated to 0x%02x\n",
			 priv->prev_state);
	} else {
		dev_info(&input_dev->dev, "No debounced button changes\n");
	}
}

/**
 * key_input_receive_buf - receive buffer callback for serdev device
 * @serdev: pointer to serdev device
 * @data: pointer to received data
 * @count: number of bytes received
 *
 * This function processes the received data from the UART and reports
 * the corresponding key events to the input device.
 *
 * Return: number of bytes processed
 */
static size_t key_input_receive_buf(struct serdev_device *serdev,
				    const unsigned char *data, size_t count)
{
	struct pamir_key_input_data *priv = serdev_device_get_drvdata(serdev);
	size_t i;

	dev_info(&serdev->dev, "Received %zu bytes from UART\n", count);

	if (priv->config.raw_protocol) {
		// Each byte is a button state, but only process valid button states
		for (i = 0; i < count; i++) {
			/*
			 * Filter out ASCII text and only process bytes that could be
			 * valid button states. Valid button states should have only the
			 * 4 least significant bits set (at most), representing our buttons.
			 * This means values should be in range 0-15 (0x00-0x0F).
			 */
			if ((data[i] & 0xF0) == 0) {
				dev_info(
					&serdev->dev,
					"Raw protocol: processing valid button state 0x%02x\n",
					data[i]);
				process_button_state(priv, data[i]);
			} else {
				// This is likely debug text or other non-button data
				dev_dbg(&serdev->dev,
					"Raw protocol: ignoring non-button byte 0x%02x\n",
					data[i]);
			}
		}
		return count;
	}

	dev_info(&serdev->dev, "Line protocol: processing input\n");
	for (i = 0; i < count; i++) {
		if (priv->buf_len < BUF_SIZE - 1) {
			priv->buf[priv->buf_len++] = data[i];

			if (data[i] == '\n') {
				priv->buf[priv->buf_len] = '\0';
				unsigned int state;

				dev_info(&serdev->dev, "Line complete: '%s'\n",
					 priv->buf);

				if (kstrtouint(priv->buf, 10, &state) == 0) {
					dev_info(&serdev->dev,
						 "Parsed state: 0x%02x\n",
						 state);
					process_button_state(priv, state);
				} else {
					/* Log invalid input */
					dev_warn(&serdev->dev,
						 "Invalid input data: '%s'\n",
						 priv->buf);
				}
				priv->buf_len = 0;
			}
		} else {
			// reset buf on overflow
			dev_warn(&serdev->dev,
				 "Buffer overflow (len=%zu), resetting\n",
				 priv->buf_len);
			priv->buf_len = 0;
		}
	}
	return count;
}

static const struct serdev_device_ops key_input_serdev_ops = {
	.receive_buf = key_input_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

/**
 * key_input_load_config - load configuration from device tree
 * @node: pointer to device node
 * @config: pointer to pamir key input configuration
 *
 * This function loads the configuration for pamir key input from the device
 * tree. It sets default values for debounce time, raw protocol, and recovery
 * timeout.
 */
static void key_input_load_config(struct device_node *node,
				  struct pamir_key_input_config *config)
{
	config->debounce_ms = DEFAULT_DEBOUNCE_MS;
	config->raw_protocol = false;
	config->report_press_only = false;
	config->recovery_timeout_ms = 1000;

	of_property_read_u32(node, "debounce-interval-ms",
			     &config->debounce_ms);
	of_property_read_u32(node, "recovery-timeout-ms",
			     &config->recovery_timeout_ms);

	if (of_find_property(node, "raw-protocol", NULL)) {
		config->raw_protocol =
			of_property_read_bool(node, "raw-protocol");
	}
	config->report_press_only =
		of_property_read_bool(node, "report-press-only");
}

/**
 * key_input_probe - probe function for pamir key input driver
 * @serdev: pointer to serdev device
 *
 * This function is called when the driver is probed. It initializes the
 * input device, sets up the serdev device, and configures the UART
 * parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
static int key_input_probe(struct serdev_device *serdev)
{
	struct pamir_key_input_data *priv;
	struct input_dev *input_dev;
	int ret;
	int i;

	dev_info(&serdev->dev, "Probing Pamir Key Input driver\n");

	priv = devm_kzalloc(&serdev->dev, sizeof(*priv), GFP_KERNEL);

	// Load configuration from device tree
	key_input_load_config(serdev->dev.of_node, &priv->config);
	dev_info(&serdev->dev,
		 "Loaded config: debounce=%ums, protocol=%s, recovery=%ums\n",
		 priv->config.debounce_ms,
		 priv->config.raw_protocol ? "raw" : "line",
		 priv->config.recovery_timeout_ms);

	dev_info(&serdev->dev, "Allocating input device\n");
	input_dev = devm_input_allocate_device(&serdev->dev);
	if (!input_dev) {
		dev_err(&serdev->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	priv->input_dev = input_dev;
	priv->prev_state = 0;
	priv->buf_len = 0;
	priv->uart_error = false;
	priv->last_receive_jiffies = jiffies;

	for (i = 0; i < 4; i++)
		priv->last_btn_jiffies[i] = jiffies;

	input_dev->name = "RP2040 Key Input";
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = 0x0001; /* Generic vendor ID */
	input_dev->id.product = 0x0001; /* Generic product ID */
	input_dev->id.version = 0x0100; /* Version 1.0 */

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(KEY_UP, input_dev->keybit);
	__set_bit(KEY_DOWN, input_dev->keybit);
	__set_bit(KEY_ENTER, input_dev->keybit);
	// __set_bit(KEY_POWER, input_dev->keybit);

	dev_info(&serdev->dev, "Registering input device\n");
	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&serdev->dev, "Failed to register input device: %d\n",
			ret);
		return ret;
	}
	dev_info(&serdev->dev, "Input device registered successfully\n");

	// Set up serdev
	dev_info(&serdev->dev, "Setting up Serial Device\n");
	serdev_device_set_drvdata(serdev, priv);
	serdev_device_set_client_ops(serdev, &key_input_serdev_ops);

	dev_info(&serdev->dev, "Opening Serial Device\n");
	ret = serdev_device_open(serdev);
	if (ret) {
		dev_err(&serdev->dev, "Failed to open serdev: %d\n", ret);
		input_unregister_device(input_dev);
		return ret;
	}
	dev_info(&serdev->dev, "Serial device opened successfully\n");

	// uart
	dev_info(&serdev->dev, "Configuring UART parameters\n");
	serdev_device_set_baudrate(serdev, 115200);
	serdev_device_set_flow_control(serdev, false);

	dev_info(&serdev->dev, "Key input driver initialized and ready\n");
	return 0;
}

/**
 * key_input_remove - remove function for pamir key input driver
 * @serdev: pointer to serdev device
 *
 * This function is called when the driver is removed. It unregisters the
 * input device and closes the serdev device.
 */
static void key_input_remove(struct serdev_device *serdev)
{
	struct pamir_key_input_data *priv = serdev_device_get_drvdata(serdev);

	dev_info(&serdev->dev, "Removing Pamir Key Input driver\n");

	dev_info(&serdev->dev, "Closing Serial Device\n");
	serdev_device_close(serdev);

	dev_info(&serdev->dev, "Unregistering input device\n");
	input_unregister_device(priv->input_dev);

	dev_info(&serdev->dev, "Driver removed successfully\n");
}

#ifdef CONFIG_OF
static const struct of_device_id key_input_of_match[] = {
	{ .compatible = "pamir,key-input" },
	{}
};

MODULE_DEVICE_TABLE(of, key_input_of_match);
#endif

static struct serdev_device_driver key_input_driver = {
	.probe = key_input_probe,
	.remove = key_input_remove,
	.driver = {
		.name = "pamir_key_input",
		.of_match_table = of_match_ptr(key_input_of_match),
	},
};

module_serdev_device_driver(key_input_driver);

MODULE_ALIAS("serdev:pamir_key_input");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("PamirAI Inc.");
MODULE_DESCRIPTION("Kernel driver for RP2040 key input via UART");
