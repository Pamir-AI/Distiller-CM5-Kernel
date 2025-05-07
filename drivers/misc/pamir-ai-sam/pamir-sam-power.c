// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Power Manager
 *
 * Power management functionality.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/**
 * process_power_packet() - Process power management packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Handle power state reports from the RP2040.
 */
void process_power_packet(struct sam_protocol_data *priv,
			  const struct sam_protocol_packet *packet)
{
	uint8_t cmd = packet->type_flags & POWER_CMD_MASK;
	uint8_t param = packet->type_flags & 0x0F;
	__maybe_unused uint8_t data1 = packet->data[0];
	__maybe_unused uint8_t data2 = packet->data[1];

	dev_dbg(&priv->serdev->dev,
	 "Power packet - Cmd: 0x%02x, Param: 0x%02x\n", cmd, param);

	switch (cmd) {
	case POWER_CMD_QUERY:
		/* RP2040 is querying power status */
		/* Respond with current power state */
		{
			struct sam_protocol_packet response;

			response.type_flags = TYPE_POWER | POWER_CMD_QUERY;
			response.data[0] = 0x01; /* Power state - 1: Running */
			response.data[1] = 0x00; /* Reserved */
			send_packet(priv, &response);
		}
		break;

	case POWER_CMD_SET:
	case POWER_CMD_SLEEP:
	case POWER_CMD_SHUTDOWN:
	default:
		/* Handle other power commands */
		dev_info(&priv->serdev->dev,
	"Power command: 0x%02x, Param: 0x%02x\n", cmd, param);
		break;
	}
}
