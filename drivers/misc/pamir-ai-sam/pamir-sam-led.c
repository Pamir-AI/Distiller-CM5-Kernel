// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) LED Handler
 *
 * LED control functionality.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/* LED class device for control */
struct led_classdev *pamir_led;

/**
 * process_led_packet() - Process LED control packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Handle LED control messages from the RP2040.
 */
void process_led_packet(struct sam_protocol_data *priv,
			const struct sam_protocol_packet *packet)
{
	uint8_t mode = packet->type_flags & LED_MODE_MASK;
	__maybe_unused uint8_t led_id = packet->type_flags & LED_ID_MASK;
	uint8_t r = (packet->data[0] >> 4) & 0x0F;
	uint8_t g = packet->data[0] & 0x0F;
	uint8_t b = (packet->data[1] >> 4) & 0x0F;
	uint8_t value = packet->data[1] & 0x0F;

	/* Scale RGB values to 0-255 range */
	r = (r * 255) / 15;
	g = (g * 255) / 15;
	b = (b * 255) / 15;

	dev_dbg(&priv->serdev->dev, "LED packet - Mode: 0x%02x, R: %u, G: %u, B: %u, Value: %u\n",
	     mode, r, g, b, value);

	/* If we have a registered LED class device, set its brightness */
	if (pamir_led) {
		/* For simplicity, convert RGB to brightness using average */
		int brightness = (r + g + b) / 3;

		led_set_brightness(pamir_led, brightness);
	}

	/* Forward LED status to user space via a sysfs attribute if needed */
}
