// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Display Handler
 *
 * E-ink display control functionality.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/**
 * process_display_packet() - Process display control packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Handle E-ink display status updates from the RP2040.
 */
void process_display_packet(struct sam_protocol_data *priv,
			    const struct sam_protocol_packet *packet)
{
	uint8_t display_cmd = packet->type_flags & 0x1F;
	uint8_t data1 = packet->data[0];
	uint8_t data2 = packet->data[1];

	dev_dbg(&priv->serdev->dev, "Display packet - Cmd: 0x%02x, Data: 0x%02x 0x%02x\n",
	     display_cmd, data1, data2);

	/* Process display status information */
	/* This could update a sysfs attribute for userspace or trigger events */

	/* Could expose display status through sysfs attributes */
	/* For example, if we received a display refresh completion notification */
	if (display_cmd == 0x01 && data1 == 0xFF)
		dev_info(&priv->serdev->dev, "Display refresh completed\n");
}
