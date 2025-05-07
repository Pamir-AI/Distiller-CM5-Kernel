// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pamir AI Sensor and Actuator Module (SAM) Debug Handler
 *
 * Debug functionality for handling debug codes and text.
 *
 * Copyright (C) 2025 Pamir AI Incorporated - http://www.pamir.ai/
 */
#include "pamir-sam.h"

/**
 * process_debug_code_packet() - Process debug code packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Extract and store debug code information.
 */
void process_debug_code_packet(struct sam_protocol_data *priv,
			       const struct sam_protocol_packet *packet)
{
	uint8_t category = packet->type_flags & 0x1F;
	uint8_t code = packet->data[0];
	uint8_t param = packet->data[1];

	mutex_lock(&priv->debug_mutex);

	/* Store in circular buffer */
	priv->debug_codes[priv->debug_head].category = category;
	priv->debug_codes[priv->debug_head].code = code;
	priv->debug_codes[priv->debug_head].param = param;
	priv->debug_codes[priv->debug_head].timestamp = jiffies;

	priv->debug_head = (priv->debug_head + 1) % DEBUG_QUEUE_SIZE;
	if (priv->debug_head == priv->debug_tail)
		priv->debug_tail = (priv->debug_tail + 1) % DEBUG_QUEUE_SIZE;

	mutex_unlock(&priv->debug_mutex);

	if (priv->config.debug_level >= 2 ||
		(priv->config.debug_level >= 1 && category <= 1)) {
		dev_info(&priv->serdev->dev,
	   "Debug[%u]: Code=%u Param=%u\n",
	   category, code, param);
	}
}

/**
 * process_debug_text_packet() - Process debug text packet
 * @priv: Private driver data
 * @packet: Received packet
 *
 * Handle text debug messages, potentially spanning multiple packets.
 */
void process_debug_text_packet(struct sam_protocol_data *priv,
			       const struct sam_protocol_packet *packet)
{
	static char debug_text[256];
	static size_t text_pos;
	bool is_first = packet->type_flags & DEBUG_FIRST_CHUNK;
	bool has_more = packet->type_flags & DEBUG_CONTINUE;
	__maybe_unused uint8_t chunk_num = packet->type_flags & DEBUG_CHUNK_MASK;

	/* Handle first chunk */
	if (is_first) {
		text_pos = 0;
		memset(debug_text, 0, sizeof(debug_text));
	}

	/* Append text from this chunk (safely) */
	if (text_pos < sizeof(debug_text) - 3) {
		debug_text[text_pos++] = packet->data[0];
		debug_text[text_pos++] = packet->data[1];
	}

	/* Process complete message if this is the last chunk */
	if (!has_more) {
		debug_text[text_pos] = '\0';
		dev_info(&priv->serdev->dev, "Debug text: %s\n", debug_text);
		text_pos = 0;
	}
}
