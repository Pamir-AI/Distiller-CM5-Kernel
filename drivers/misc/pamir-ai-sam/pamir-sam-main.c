// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Driver
 *
 * Driver for communication between host processor and RP2040 microcontroller
 * providing interface to sensor inputs and actuator outputs on CM5 device.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/**
 * sam_protocol_receive_buf() - Process received UART data
 * @serdev: Serial device
 * @data: Received data buffer
 * @count: Number of bytes received
 *
 * State machine for packet processing.
 *
 * Return: Number of bytes processed
 */
static size_t sam_protocol_receive_buf(struct serdev_device *serdev,
				     const unsigned char *data, size_t count)
{
	struct sam_protocol_data *priv = serdev_device_get_drvdata(serdev);
	size_t i;

	for (i = 0; i < count; i++) {
		/* Store byte in buffer */
		if (priv->rx_pos < RX_BUF_SIZE)
			priv->rx_buf[priv->rx_pos++] = data[i];

		/* Process packet when complete */
		if (priv->rx_pos >= PACKET_SIZE) {
			process_packet(priv, (const struct sam_protocol_packet *)priv->rx_buf);
			priv->rx_pos = 0;
		}
	}

	priv->last_receive_jiffies = jiffies;
	return count;
}

/* UART operations */
static const struct serdev_device_ops sam_protocol_serdev_ops = {
	.receive_buf = sam_protocol_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

/**
 * sam_protocol_load_config() - Load driver configuration from device tree
 * @node: Device tree node
 * @config: Configuration structure to populate
 *
 * Read device tree properties to configure driver behavior.
 */
static void sam_protocol_load_config(struct device_node *node,
				      struct sam_protocol_config *config)
{
	/* Set defaults */
	config->debug_level = 1;
	config->ack_required = false;
	config->recovery_timeout_ms = 1000;

	/* Override with device tree settings if present */
	of_property_read_u32(node, "debug-level", &config->debug_level);
	of_property_read_u32(node, "recovery-timeout-ms",
			  &config->recovery_timeout_ms);

	config->ack_required = of_property_read_bool(node, "ack-required");
}

/**
 * sam_protocol_probe() - Probe function for SAM protocol driver
 * @serdev: Serial device
 *
 * Initialize hardware, set up input device, and register with the kernel.
 *
 * Return: 0 on success, negative error code on failure
 */
static int sam_protocol_probe(struct serdev_device *serdev)
{
	struct sam_protocol_data *priv;
	struct input_dev *input_dev;
	int ret;

	priv = devm_kzalloc(&serdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Load configuration from device tree */
	sam_protocol_load_config(serdev->dev.of_node, &priv->config);

	input_dev = devm_input_allocate_device(&serdev->dev);
	if (!input_dev) {
		dev_err(&serdev->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	priv->input_dev = input_dev;
	priv->rx_pos = 0;
	priv->last_receive_jiffies = jiffies;
	priv->serdev = serdev;
	priv->debug_head = 0;
	priv->debug_tail = 0;

	input_dev->name = "Pamir AI Sensor and Actuator Module";
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0002;
	input_dev->id.version = 0x0100;

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(KEY_UP, input_dev->keybit);
	__set_bit(KEY_DOWN, input_dev->keybit);
	__set_bit(KEY_ENTER, input_dev->keybit);
	__set_bit(KEY_POWER, input_dev->keybit);

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&serdev->dev, "Failed to register input device: %d\n",
	  ret);
		return ret;
	}

	/* Set up LED class device */
	pamir_led = devm_kzalloc(&serdev->dev, sizeof(struct led_classdev), GFP_KERNEL);
	if (pamir_led) {
		pamir_led->name = "pamir:status";
		pamir_led->max_brightness = 255;
		pamir_led->brightness_set = NULL; /* We don't control LED from host yet */
		pamir_led->brightness = 0;

		ret = devm_led_classdev_register(&serdev->dev, pamir_led);
		if (ret) {
			dev_err(&serdev->dev, "Failed to register LED: %d\n", ret);
			pamir_led = NULL; /* Clear on failure */
		}
	}

	serdev_device_set_drvdata(serdev, priv);
	serdev_device_set_client_ops(serdev, &sam_protocol_serdev_ops);

	ret = serdev_device_open(serdev);
	if (ret) {
		dev_err(&serdev->dev, "Failed to open serdev: %d\n", ret);
		input_unregister_device(input_dev);
		return ret;
	}

	serdev_device_set_baudrate(serdev, 115200);
	serdev_device_set_flow_control(serdev, false);

	ret = setup_char_device(priv);
	if (ret < 0) {
		dev_err(&serdev->dev, "Failed to set up char device: %d\n",
	  ret);
		serdev_device_close(serdev);
		input_unregister_device(input_dev);
		return ret;
	}

	/* Initialize work queue for deferred processing */
	priv->work_queue = create_singlethread_workqueue("sam_protocol_wq");
	if (!priv->work_queue) {
		dev_err(&serdev->dev, "Failed to create workqueue\n");
		cleanup_char_device(priv);
		serdev_device_close(serdev);
		input_unregister_device(input_dev);
		return -ENOMEM;
	}

	/* Test communication by sending a ping */
	if (send_system_command(priv, SYSTEM_PING, 0, 0) == 0)
		dev_info(&serdev->dev, "Initial communication test successful\n");
	else
		dev_warn(&serdev->dev, "Initial communication test failed\n");

	dev_info(&serdev->dev, "SAM protocol driver initialized and ready\n");
	return 0;
}

/**
 * sam_protocol_remove() - Remove function for SAM protocol driver
 * @serdev: Serial device
 *
 * Clean up resources when the driver is unloaded.
 */
static void sam_protocol_remove(struct serdev_device *serdev)
{
	struct sam_protocol_data *priv = serdev_device_get_drvdata(serdev);

	dev_info(&serdev->dev, "Removing SAM protocol driver\n");

	if (priv->work_queue)
		destroy_workqueue(priv->work_queue);

	/* Send a system reset or shutdown command */
	send_system_command(priv, SYSTEM_RESET, 0, 0);

	/* Clean up character device */
	cleanup_char_device(priv);

	serdev_device_close(serdev);
	input_unregister_device(priv->input_dev);
}

#ifdef CONFIG_OF
static const struct of_device_id sam_protocol_of_match[] = {
	{ .compatible = "pamir-ai,sam" },
	{}
};

MODULE_DEVICE_TABLE(of, sam_protocol_of_match);
#endif

static struct serdev_device_driver sam_protocol_driver = {
	.probe = sam_protocol_probe,
	.remove = sam_protocol_remove,
	.driver = {
		.name = "pamir_sam_protocol",
		.of_match_table = of_match_ptr(sam_protocol_of_match),
	},
};

module_serdev_device_driver(sam_protocol_driver);

MODULE_ALIAS("serdev:pamir_sam_protocol");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Pamir AI Incorporated.");
MODULE_DESCRIPTION("PamirAI Sensor and Actuator Module (SAM)");
