// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Protocol Core
 *
 * Core protocol functionality for the SAM driver.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/**
 * calculate_checksum() - Calculate XOR checksum for packet
 * @packet: Packet to calculate checksum for
 *
 * Calculate the XOR checksum of all bytes in the packet.
 *
 * Return: Calculated checksum
 */
uint8_t calculate_checksum(const struct sam_protocol_packet *packet)
{
	return packet->type_flags ^ packet->data[0] ^ packet->data[1];
}

/**
 * verify_checksum() - Verify packet checksum
 * @packet: Packet to verify
 *
 * Check if the packet's checksum matches the calculated checksum.
 *
 * Return: true if checksum is valid, false otherwise
 */
bool verify_checksum(const struct sam_protocol_packet *packet)
{
	return packet->checksum == calculate_checksum(packet);
}

/**
 * send_packet() - Send packet to device
 * @priv: Private driver data
 * @packet: Packet to send
 *
 * Prepare and send a packet, updating the checksum.
 *
 * Return: 0 on success, negative error code on failure
 */
int send_packet(struct sam_protocol_data *priv,
		struct sam_protocol_packet *packet)
{
	int ret;

	/* Calculate checksum */
	packet->checksum = calculate_checksum(packet);

	mutex_lock(&priv->tx_mutex);
	ret = serdev_device_write(priv->serdev, (const unsigned char *)packet,
			       PACKET_SIZE, MAX_SCHEDULE_TIMEOUT);
	mutex_unlock(&priv->tx_mutex);

	return (ret == PACKET_SIZE) ? 0 : -EIO;
}

/**
 * send_led_command() - Send LED control command
 * @priv: Private driver data
 * @mode: LED mode (static, blink, etc.)
 * @r: Red component (0-15)
 * @g: Green component (0-15)
 * @b: Blue component (0-15)
 * @value: Brightness or animation parameter
 *
 * Send a command to control the LED.
 *
 * Return: 0 on success, negative error code on failure
 */
int send_led_command(struct sam_protocol_data *priv, uint8_t mode,
		     uint8_t r, uint8_t g, uint8_t b, uint8_t value)
{
	struct sam_protocol_packet packet;

	packet.type_flags = TYPE_LED | mode;
	packet.data[0] = ((r & 0x0F) << 4) | (g & 0x0F);
	packet.data[1] = ((b & 0x0F) << 4) | (value & 0x0F);

	return send_packet(priv, &packet);
}

/**
 * send_system_command() - Send system control command
 * @priv: Private driver data
 * @action: System action (ping, reset, etc.)
 * @command: Command parameter
 * @subcommand: Subcommand parameter
 *
 * Send a system control command to the device.
 *
 * Return: 0 on success, negative error code on failure
 */
int send_system_command(struct sam_protocol_data *priv, uint8_t action,
			uint8_t command, uint8_t subcommand)
{
	struct sam_protocol_packet packet;

	packet.type_flags = TYPE_SYSTEM | (action & 0x1F);
	packet.data[0] = command;
	packet.data[1] = subcommand;

	return send_packet(priv, &packet);
}

/**
 * process_packet() - Process a complete packet
 * @priv: Private driver data
 * @packet: Packet to process
 *
 * Process a received packet based on its type.
 */
void process_packet(struct sam_protocol_data *priv,
		    const struct sam_protocol_packet *packet)
{
	uint8_t type = packet->type_flags & TYPE_MASK;

	/* Update statistics */
	priv->packet_stats[type >> 5]++;

	/* Verify checksum first */
	if (!verify_checksum(packet)) {
		dev_dbg(&priv->serdev->dev,
	  "Invalid checksum: got 0x%02x, expected 0x%02x\n",
	  packet->checksum, calculate_checksum(packet));
		return;
	}

	/* Process by type */
	switch (type) {
	case TYPE_BUTTON:
		process_button_packet(priv, packet);
		break;

	case TYPE_LED:
		process_led_packet(priv, packet);
		break;

	case TYPE_POWER:
		process_power_packet(priv, packet);
		break;

	case TYPE_DISPLAY:
		process_display_packet(priv, packet);
		break;

	case TYPE_DEBUG_CODE:
		process_debug_code_packet(priv, packet);
		break;

	case TYPE_DEBUG_TEXT:
		process_debug_text_packet(priv, packet);
		break;

	case TYPE_SYSTEM:
		process_system_packet(priv, packet);
		break;

	case TYPE_RESERVED:
		process_extended_packet(priv, packet);
		break;

	default:
		/* Should never happen due to mask */
		dev_warn(&priv->serdev->dev, "Unknown packet type: 0x%02x\n", type);
		break;
	}

	/* Send acknowledgment if required */
	if (priv->config.ack_required && type != TYPE_BUTTON)
		send_system_command(priv, SYSTEM_PING, 0x00, 0x00);
}
