// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Key Input Driver
 *
 * Serial device driver for handling button inputs from RP2040 microcontroller
 * over UART with bidirectional communication support.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 *
 * Author: Utsav Balar <utsavbalar1231@gmail.com>
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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define BUF_SIZE 32
#define TX_BUF_SIZE 256
#define DEVICE_NAME "pamir-uart"
#define MAX_DEVICES 1

/* Button state masks */
#define BTN_UP_MASK 0b0001
#define BTN_DOWN_MASK 0b0010
#define BTN_SELECT_MASK 0b0100
#define SHUT_DOWN_MASK 0b1000

/* Default debounce time in milliseconds */
#define DEFAULT_DEBOUNCE_MS 50

/**
 * struct pamir_key_input_config - configuration for Pamir key input
 * @debounce_ms: debounce time in milliseconds
 * @raw_protocol: use raw protocol without line parsing
 * @report_press_only: only report key press events, not release
 * @recovery_timeout_ms: timeout for UART recovery
 *
 * This struct holds configurable parameters for the Pamir key input driver
 * that can be set through device tree properties.
 */
struct pamir_key_input_config {
	unsigned int debounce_ms;
	bool raw_protocol;
	bool report_press_only;
	unsigned int recovery_timeout_ms;
};

/**
 * struct pamir_key_input_data - private data for Pamir key input
 * @input_dev: pointer to input device
 * @prev_state: previous button state
 * @buf: buffer for receiving data
 * @buf_len: length of the buffer
 * @config: configuration for Pamir key input
 * @uart_error: flag for UART error
 * @last_receive_jiffies: last time data was received
 * @last_btn_jiffies: last time each button was pressed
 * @serdev: pointer to serdev device (for TX)
 * @cdev: character device for TX from userspace
 * @tx_dev: device number for TX char device
 * @tx_mutex: mutex for TX operations
 * @tx_class: device class for TX char device
 *
 * This struct holds the runtime state of the Pamir key input driver.
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

	/* UART TX functionality */
	struct serdev_device *serdev;
	struct cdev cdev;
	dev_t tx_dev;
	struct mutex tx_mutex;
	struct class *tx_class;
};

/* Global device data pointer for char device operations */
static struct pamir_key_input_data *g_pamir_data;

/**
 * process_button_state() - Process received button state
 * @priv: Private device data
 * @state: Button state received from UART
 *
 * Process the button state received from UART and report key events
 * to the input subsystem after applying debounce logic.
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

	dev_dbg(&input_dev->dev,
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

			dev_dbg(&input_dev->dev, "Button UP %s\n",
				pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed) {
				/* Immediately report release to ensure key doesn't "stick" */
				input_sync(input_dev);
			}
		}
	}

	if (changed & BTN_DOWN_MASK) {
		/* Check debounce */
		if (time_after(now,
			       priv->last_btn_jiffies[1] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & BTN_DOWN_MASK) != 0;

			input_report_key(input_dev, KEY_DOWN, pressed);
			priv->last_btn_jiffies[1] = now;
			debounced_change = true;

			dev_dbg(&input_dev->dev, "Button DOWN %s\n",
				pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed)
				input_sync(input_dev);
		}
	}

	if (changed & BTN_SELECT_MASK) {
		/* Check debounce */
		if (time_after(now,
			       priv->last_btn_jiffies[2] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & BTN_SELECT_MASK) != 0;

			input_report_key(input_dev, KEY_ENTER, pressed);
			priv->last_btn_jiffies[2] = now;
			debounced_change = true;

			dev_dbg(&input_dev->dev, "Button SELECT %s\n",
				pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed)
				input_sync(input_dev);
		}
	}

	if (changed & SHUT_DOWN_MASK) {
		/* Check debounce */
		if (time_after(now,
			       priv->last_btn_jiffies[3] +
				       msecs_to_jiffies(
					       priv->config.debounce_ms))) {
			bool pressed = (state & SHUT_DOWN_MASK) != 0;

			/* Currently not reporting KEY_POWER events */
			priv->last_btn_jiffies[3] = now;
			debounced_change = true;

			dev_dbg(&input_dev->dev, "Button POWER %s\n",
				pressed ? "pressed" : "released");

			if (priv->config.report_press_only && !pressed)
				input_sync(input_dev);
		}
	}

	/* Update if at least one button changed */
	if (debounced_change) {
		input_sync(input_dev);
		priv->prev_state = state;
	}
}

/**
 * key_input_receive_buf() - Process received UART data
 * @serdev: Serial device
 * @data: Received data buffer
 * @count: Number of bytes received
 *
 * Handle data received from the UART and extract button state information.
 * Support for both line-based protocol and raw protocol.
 *
 * Return: Number of bytes processed
 */
static size_t key_input_receive_buf(struct serdev_device *serdev,
				    const unsigned char *data, size_t count)
{
	struct pamir_key_input_data *priv = serdev_device_get_drvdata(serdev);
	size_t i;

	if (priv->config.raw_protocol) {
		/* Each byte is a button state, but only process valid button states */
		for (i = 0; i < count; i++) {
			/*
			 * Filter out ASCII text and only process bytes that could be
			 * valid button states. Valid button states should have only the
			 * 4 least significant bits set (at most), representing our buttons.
			 * This means values should be in range 0-15 (0x00-0x0F).
			 */
			if ((data[i] & 0xF0) == 0)
				process_button_state(priv, data[i]);
		}
		return count;
	}

	/* Text-based protocol: Each line contains a decimal button state */
	for (i = 0; i < count; i++) {
		if (priv->buf_len < BUF_SIZE - 1) {
			priv->buf[priv->buf_len++] = data[i];

			if (data[i] == '\n') {
				priv->buf[priv->buf_len] = '\0';
				unsigned int state;

				if (kstrtouint(priv->buf, 10, &state) == 0)
					process_button_state(priv, state);
				priv->buf_len = 0;
			}
		} else {
			/* Reset buffer on overflow */
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
 * key_input_load_config() - Load driver configuration from device tree
 * @node: Device tree node
 * @config: Configuration structure to populate
 *
 * Read device tree properties to configure driver behavior.
 */
static void key_input_load_config(struct device_node *node,
				  struct pamir_key_input_config *config)
{
	/* Set defaults */
	config->debounce_ms = DEFAULT_DEBOUNCE_MS;
	config->raw_protocol = false;
	config->report_press_only = false;
	config->recovery_timeout_ms = 1000;

	/* Override with device tree settings if present */
	of_property_read_u32(node, "debounce-interval-ms",
			     &config->debounce_ms);
	of_property_read_u32(node, "recovery-timeout-ms",
			     &config->recovery_timeout_ms);

	config->raw_protocol = of_property_read_bool(node, "raw-protocol");
	config->report_press_only =
		of_property_read_bool(node, "report-press-only");
}

/* Character device operations for UART TX functionality */

/**
 * pamir_uart_open() - Open character device
 * @inode: Inode pointer
 * @filp: File pointer
 *
 * Handle open() syscall on the character device.
 *
 * Return: 0 on success, negative error code on failure
 */
static int pamir_uart_open(struct inode *inode, struct file *filp)
{
	struct pamir_key_input_data *priv = g_pamir_data;

	if (!priv)
		return -ENODEV;

	filp->private_data = priv;
	return 0;
}

/**
 * pamir_uart_write() - Write data to UART
 * @filp: File pointer
 * @buf: User buffer containing data to send
 * @count: Number of bytes to send
 * @ppos: File position pointer (unused)
 *
 * Handle write() syscall on the character device to send data to UART.
 *
 * Return: Number of bytes sent on success, negative error code on failure
 */
static ssize_t pamir_uart_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct pamir_key_input_data *priv = filp->private_data;
	char tx_buf[TX_BUF_SIZE];
	size_t bytes_to_send;
	int ret;

	if (!priv || !priv->serdev)
		return -ENODEV;

	bytes_to_send = min_t(size_t, count, TX_BUF_SIZE - 1);
	if (copy_from_user(tx_buf, buf, bytes_to_send))
		return -EFAULT;

	mutex_lock(&priv->tx_mutex);
	ret = serdev_device_write(priv->serdev, tx_buf, bytes_to_send,
				  MAX_SCHEDULE_TIMEOUT);
	mutex_unlock(&priv->tx_mutex);

	return ret > 0 ? ret : -EIO;
}

/**
 * pamir_uart_release() - Release character device
 * @inode: Inode pointer
 * @filp: File pointer
 *
 * Handle close() syscall on the character device.
 *
 * Return: Always returns 0
 */
static int pamir_uart_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* File operations for character device */
static const struct file_operations pamir_uart_fops = {
	.owner = THIS_MODULE,
	.open = pamir_uart_open,
	.write = pamir_uart_write,
	.release = pamir_uart_release,
};

/**
 * setup_uart_tx_device() - Set up character device for UART transmit
 * @priv: Driver's private data
 *
 * Create character device node to allow userspace applications to send
 * data through the UART.
 *
 * Return: 0 on success, negative error code on failure
 */
static int setup_uart_tx_device(struct pamir_key_input_data *priv)
{
	int ret;
	struct device *device;

	/* Initialize mutex */
	mutex_init(&priv->tx_mutex);

	/* Allocate device number */
	ret = alloc_chrdev_region(&priv->tx_dev, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		dev_err(&priv->serdev->dev,
			"Failed to allocate device number: %d\n", ret);
		return ret;
	}

	/* Initialize character device */
	cdev_init(&priv->cdev, &pamir_uart_fops);
	priv->cdev.owner = THIS_MODULE;

	/* Add character device to system */
	ret = cdev_add(&priv->cdev, priv->tx_dev, 1);
	if (ret < 0) {
		dev_err(&priv->serdev->dev,
			"Failed to add character device: %d\n", ret);
		unregister_chrdev_region(priv->tx_dev, 1);
		return ret;
	}

	/* Create device class */
	priv->tx_class = class_create(DEVICE_NAME);
	if (IS_ERR(priv->tx_class)) {
		ret = PTR_ERR(priv->tx_class);
		dev_err(&priv->serdev->dev,
			"Failed to create device class: %d\n", ret);
		cdev_del(&priv->cdev);
		unregister_chrdev_region(priv->tx_dev, 1);
		return ret;
	}

	/* Create device node */
	device = device_create(priv->tx_class, NULL, priv->tx_dev, NULL,
			       DEVICE_NAME);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		dev_err(&priv->serdev->dev, "Failed to create device: %d\n",
			ret);
		class_destroy(priv->tx_class);
		cdev_del(&priv->cdev);
		unregister_chrdev_region(priv->tx_dev, 1);
		return ret;
	}

	/* Store global data pointer for file operations */
	g_pamir_data = priv;

	dev_dbg(&priv->serdev->dev, "UART TX char device created: /dev/%s\n",
		DEVICE_NAME);
	return 0;
}

/**
 * cleanup_uart_tx_device() - Clean up character device resources
 * @priv: Driver's private data
 *
 * Release all resources allocated for the character device.
 */
static void cleanup_uart_tx_device(struct pamir_key_input_data *priv)
{
	if (priv->tx_class) {
		device_destroy(priv->tx_class, priv->tx_dev);
		class_destroy(priv->tx_class);
	}

	cdev_del(&priv->cdev);
	unregister_chrdev_region(priv->tx_dev, 1);

	g_pamir_data = NULL;
}

/**
 * key_input_probe() - Probe function for Pamir key input driver
 * @serdev: Serial device
 *
 * Initialize hardware, set up input device, and register with the kernel.
 *
 * Return: 0 on success, negative error code on failure
 */
static int key_input_probe(struct serdev_device *serdev)
{
	struct pamir_key_input_data *priv;
	struct input_dev *input_dev;
	int ret;
	int i;

	priv = devm_kzalloc(&serdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Load configuration from device tree */
	key_input_load_config(serdev->dev.of_node, &priv->config);

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
	priv->serdev = serdev; /* Store serdev for TX operations */

	for (i = 0; i < 4; i++)
		priv->last_btn_jiffies[i] = jiffies;

	input_dev->name = "Pamir AI Key Input";
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = 0x0001; /* Generic vendor ID */
	input_dev->id.product = 0x0001; /* Generic product ID */
	input_dev->id.version = 0x0100; /* Version 1.0 */

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(KEY_UP, input_dev->keybit);
	__set_bit(KEY_DOWN, input_dev->keybit);
	__set_bit(KEY_ENTER, input_dev->keybit);
	/* __set_bit(KEY_POWER, input_dev->keybit); */

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&serdev->dev, "Failed to register input device: %d\n",
			ret);
		return ret;
	}

	serdev_device_set_drvdata(serdev, priv);
	serdev_device_set_client_ops(serdev, &key_input_serdev_ops);

	ret = serdev_device_open(serdev);
	if (ret) {
		dev_err(&serdev->dev, "Failed to open serdev: %d\n", ret);
		input_unregister_device(input_dev);
		return ret;
	}

	serdev_device_set_baudrate(serdev, 115200);
	serdev_device_set_flow_control(serdev, false);

	ret = setup_uart_tx_device(priv);
	if (ret < 0) {
		dev_err(&serdev->dev, "Failed to set up UART TX device: %d\n",
			ret);
		serdev_device_close(serdev);
		input_unregister_device(input_dev);
		return ret;
	}

	return 0;
}

/**
 * key_input_remove() - Remove function for Pamir key input driver
 * @serdev: Serial device
 *
 * Clean up resources when the driver is unloaded.
 */
static void key_input_remove(struct serdev_device *serdev)
{
	struct pamir_key_input_data *priv = serdev_device_get_drvdata(serdev);

	cleanup_uart_tx_device(priv);
	serdev_device_close(serdev);
	input_unregister_device(priv->input_dev);
}

#ifdef CONFIG_OF
static const struct of_device_id key_input_of_match[] = {
	{ .compatible = "pamir-ai,key-input" },
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
MODULE_AUTHOR("Pamir AI Inc.");
MODULE_DESCRIPTION("Kernel driver for Pamir AI key input via UART");
