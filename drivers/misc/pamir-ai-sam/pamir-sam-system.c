// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) System Handler
 *
 * System command functionality.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"
#include <linux/version.h>

/**
 * send_system_version() - Send system version information
 * @priv: Private driver data
 *
 * Send kernel version information to the RP2040.
 */
static void send_system_version(struct sam_protocol_data *priv)
{
	struct sam_protocol_packet packet;

	/* Use kernel version as system version */
	uint8_t major = (uint8_t)LINUX_VERSION_CODE >> 16;
	uint8_t minor = (uint8_t)(LINUX_VERSION_CODE >> 8) & 0xFF;

	packet.type_flags = TYPE_SYSTEM | SYSTEM_VERSION;
	packet.data[0] = major;
	packet.data[1] = minor;

	send_packet(priv, &packet);
}

/**
 * process_system_packet() - Process system control packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Handle system control messages from the RP2040.
 */
void process_system_packet(struct sam_protocol_data *priv,
			   const struct sam_protocol_packet *packet)
{
	uint8_t action = packet->type_flags & 0x1F;
	uint8_t command = packet->data[0];
	uint8_t subcommand = packet->data[1];

	dev_dbg(&priv->serdev->dev, "System packet - Action: 0x%02x, Cmd: 0x%02x, Sub: 0x%02x\n",
	     action, command, subcommand);

	switch (action) {
	case SYSTEM_PING:
		/* Respond with ping acknowledgment */
		send_system_command(priv, SYSTEM_PING, 0, 0);
		break;

	case SYSTEM_RESET:
		/* RP2040 will reset itself - acknowledge */
		dev_info(&priv->serdev->dev, "RP2040 reset reported\n");
		break;

	case SYSTEM_VERSION:
		/* Send kernel version information */
		send_system_version(priv);
		break;

	case SYSTEM_STATUS:
		/* Send system status */
		{
			struct sam_protocol_packet response;

			response.type_flags = TYPE_SYSTEM | SYSTEM_STATUS;
			response.data[0] = 0x01;  /* Status OK */
			response.data[1] = 0x00;  /* No error */
			send_packet(priv, &response);
		}
		break;

	case SYSTEM_CONFIG:
		/* Set/Get configuration */
		if (command == 0x00) {
			/* Get configuration */
			dev_dbg(&priv->serdev->dev, "RP2040 requested configuration\n");

			/* Send current configuration */
			struct sam_protocol_packet response;

			response.type_flags = TYPE_SYSTEM | SYSTEM_CONFIG;
			response.data[0] = priv->config.debug_level; /* Debug level */
			response.data[1] = priv->config.ack_required ? 0x01 : 0x00; /* ACK required */
			send_packet(priv, &response);
		} else if (command == 0x01) {
			/* Set configuration */
			dev_info(&priv->serdev->dev, "RP2040 set configuration: Debug=%u, ACK=%u\n",
		subcommand & 0x0F, (subcommand >> 4) & 0x01);

			/* Update configuration */
			priv->config.debug_level = subcommand & 0x0F;
			priv->config.ack_required = ((subcommand >> 4) & 0x01) != 0;
		}
		break;
	}
}

/**
 * process_extended_packet() - Process extended command packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Handle extended commands from the RP2040 (reserved for future use).
 */
void process_extended_packet(struct sam_protocol_data *priv,
			     const struct sam_protocol_packet *packet)
{
	uint8_t ext_cmd = packet->type_flags & 0x1F;
	uint8_t data1 = packet->data[0];
	uint8_t data2 = packet->data[1];

	dev_info(&priv->serdev->dev, "Extended command received: Cmd=0x%02x, Data=0x%02x 0x%02x\n",
	      ext_cmd, data1, data2);

	/* Extended commands are reserved for future use */
	/* Just log them for now and acknowledge receipt */
	if (priv->config.ack_required)
		send_system_command(priv, SYSTEM_PING, 0, 0);
}
