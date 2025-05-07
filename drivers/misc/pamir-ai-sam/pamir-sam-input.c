// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Input Handler
 *
 * Input device functionality for button events.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/**
 * process_button_packet() - Process button event packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Extract button state from the packet and report to input subsystem.
 */
void process_button_packet(struct sam_protocol_data *priv,
			   const struct sam_protocol_packet *packet)
{
	struct input_dev *input_dev = priv->input_dev;
	uint8_t button_state = packet->type_flags & 0x1F;

	input_report_key(input_dev, KEY_UP, button_state & BTN_UP_MASK);
	input_report_key(input_dev, KEY_DOWN, button_state & BTN_DOWN_MASK);
	input_report_key(input_dev, KEY_ENTER, button_state & BTN_SELECT_MASK);
	input_report_key(input_dev, KEY_POWER, button_state & BTN_POWER_MASK);
	input_sync(input_dev);

	dev_dbg(&input_dev->dev, "Button state: 0x%02x\n", button_state);
}
