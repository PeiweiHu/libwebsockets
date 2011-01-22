/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "private-libwebsockets.h"

const struct lws_tokens lws_tokens[WSI_TOKEN_COUNT] = {
	[WSI_TOKEN_GET_URI]	= { "GET ",			 4 },
	[WSI_TOKEN_HOST]	= { "Host:",			 5 },
	[WSI_TOKEN_CONNECTION]	= { "Connection:",		11 },
	[WSI_TOKEN_KEY1]	= { "Sec-WebSocket-Key1:",	19 },
	[WSI_TOKEN_KEY2]	= { "Sec-WebSocket-Key2:",	19 },
	[WSI_TOKEN_PROTOCOL]	= { "Sec-WebSocket-Protocol:",	23 },
	[WSI_TOKEN_UPGRADE]	= { "Upgrade:",			 8 },
	[WSI_TOKEN_ORIGIN]	= { "Origin:",			 7 },
	[WSI_TOKEN_DRAFT]	= { "Sec-WebSocket-Draft:",	20 },
	[WSI_TOKEN_CHALLENGE]	= { "\x0d\x0a",			 2 },

	[WSI_TOKEN_KEY]		= { "Sec-WebSocket-Key:",	18 },
	[WSI_TOKEN_VERSION]	= { "Sec-WebSocket-Version:",	22 },

	[WSI_TOKEN_ACCEPT]	= { "Sec-WebSocket-Accept:",	21 },
	[WSI_TOKEN_NONCE]	= { "Sec-WebSocket-Nonce:",	20 },
	[WSI_TOKEN_HTTP]	= { "HTTP/1.1 ",		 9 },
	[WSI_TOKEN_SWORIGIN]	= { "Sec-WebSocket-Origin:",	21 },

};

int libwebsocket_parse(struct libwebsocket *wsi, unsigned char c)
{
	int n;

	switch (wsi->parser_state) {
	case WSI_TOKEN_GET_URI:
	case WSI_TOKEN_HOST:
	case WSI_TOKEN_CONNECTION:
	case WSI_TOKEN_KEY1:
	case WSI_TOKEN_KEY2:
	case WSI_TOKEN_PROTOCOL:
	case WSI_TOKEN_UPGRADE:
	case WSI_TOKEN_ORIGIN:
	case WSI_TOKEN_SWORIGIN:
	case WSI_TOKEN_DRAFT:
	case WSI_TOKEN_CHALLENGE:
	case WSI_TOKEN_KEY:
	case WSI_TOKEN_VERSION:
	case WSI_TOKEN_ACCEPT:
	case WSI_TOKEN_NONCE:
	case WSI_TOKEN_HTTP:
		debug("WSI_TOKEN_(%d) '%c'\n", wsi->parser_state, c);

		/* collect into malloc'd buffers */
		/* optional space swallow */
		if (!wsi->utf8_token[wsi->parser_state].token_len && c == ' ')
			break;

		/* special case space terminator for get-uri */
		if (wsi->parser_state == WSI_TOKEN_GET_URI && c == ' ') {
			wsi->utf8_token[wsi->parser_state].token[
			   wsi->utf8_token[wsi->parser_state].token_len] = '\0';
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}

		/* allocate appropriate memory */
		if (wsi->utf8_token[wsi->parser_state].token_len ==
						   wsi->current_alloc_len - 1) {
			/* need to extend */
			wsi->current_alloc_len += LWS_ADDITIONAL_HDR_ALLOC;
			if (wsi->current_alloc_len >= LWS_MAX_HEADER_LEN) {
				/* it's waaay to much payload, fail it */
				strcpy(wsi->utf8_token[wsi->parser_state].token,
				   "!!! Length exceeded maximum supported !!!");
				wsi->parser_state = WSI_TOKEN_SKIPPING;
				break;
			}
			wsi->utf8_token[wsi->parser_state].token =
			       realloc(wsi->utf8_token[wsi->parser_state].token,
							wsi->current_alloc_len);
		}

		/* bail at EOL */
		if (wsi->parser_state != WSI_TOKEN_CHALLENGE && c == '\x0d') {
			wsi->utf8_token[wsi->parser_state].token[
			   wsi->utf8_token[wsi->parser_state].token_len] = '\0';
			wsi->parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
			break;
		}

		wsi->utf8_token[wsi->parser_state].token[
			    wsi->utf8_token[wsi->parser_state].token_len++] = c;

		/* per-protocol end of headers management */

		if (wsi->parser_state != WSI_TOKEN_CHALLENGE)
			break;

		/* -76 has no version header */
		if (!wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			      wsi->utf8_token[wsi->parser_state].token_len != 8)
			break;

		/* <= 03 has old handshake with version header */
		if (wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			 atoi(wsi->utf8_token[WSI_TOKEN_VERSION].token) < 4 &&
			      wsi->utf8_token[wsi->parser_state].token_len != 8)
			break;

		/* For any supported protocol we have enough payload */

		debug("Setting WSI_PARSING_COMPLETE\n");
		wsi->parser_state = WSI_PARSING_COMPLETE;
		break;

		/* collecting and checking a name part */
	case WSI_TOKEN_NAME_PART:
		debug("WSI_TOKEN_NAME_PART '%c'\n", c);

		if (wsi->name_buffer_pos == sizeof(wsi->name_buffer) - 1) {
			/* name bigger than we can handle, skip until next */
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}
		wsi->name_buffer[wsi->name_buffer_pos++] = c;
		wsi->name_buffer[wsi->name_buffer_pos] = '\0';

		for (n = 0; n < WSI_TOKEN_COUNT; n++) {
			if (wsi->name_buffer_pos != lws_tokens[n].token_len)
				continue;
			if (strcmp(lws_tokens[n].token, wsi->name_buffer))
				continue;
			debug("known hdr '%s'\n", wsi->name_buffer);
			wsi->parser_state = WSI_TOKEN_GET_URI + n;
			wsi->current_alloc_len = LWS_INITIAL_HDR_ALLOC;
			
			wsi->utf8_token[wsi->parser_state].token =
						 malloc(wsi->current_alloc_len);
			wsi->utf8_token[wsi->parser_state].token_len = 0;
			n = WSI_TOKEN_COUNT;
		}

		/* colon delimiter means we just don't know this name */

		if (wsi->parser_state == WSI_TOKEN_NAME_PART && c == ':') {
			debug("skipping unknown header '%s'\n",
							      wsi->name_buffer);
			wsi->parser_state = WSI_TOKEN_SKIPPING;
			break;
		}

		if (wsi->parser_state != WSI_TOKEN_CHALLENGE)
			break;

		/* don't look for payload when it can just be http headers */

		if (!wsi->utf8_token[WSI_TOKEN_UPGRADE].token_len) {
			/* they're HTTP headers, not websocket upgrade! */
			debug("Setting WSI_PARSING_COMPLETE "
							 "from http headers\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
		}

		/* 04 version has no packet content after end of hdrs */

		if (wsi->utf8_token[WSI_TOKEN_VERSION].token_len &&
			 atoi(wsi->utf8_token[WSI_TOKEN_VERSION].token) >= 4) {
			debug("04 header completed\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
		}

		/* client parser? */

		if (wsi->ietf_spec_revision >= 4) {
			debug("04 header completed\n");
			wsi->parser_state = WSI_PARSING_COMPLETE;
		}

		break;

		/* skipping arg part of a name we didn't recognize */
	case WSI_TOKEN_SKIPPING:
		debug("WSI_TOKEN_SKIPPING '%c'\n", c);
		if (c == '\x0d')
			wsi->parser_state = WSI_TOKEN_SKIPPING_SAW_CR;
		break;
	case WSI_TOKEN_SKIPPING_SAW_CR:
		debug("WSI_TOKEN_SKIPPING_SAW_CR '%c'\n", c);
		if (c == '\x0a')
			wsi->parser_state = WSI_TOKEN_NAME_PART;
		else
			wsi->parser_state = WSI_TOKEN_SKIPPING;
		wsi->name_buffer_pos = 0;
		break;
		/* we're done, ignore anything else */
	case WSI_PARSING_COMPLETE:
		debug("WSI_PARSING_COMPLETE '%c'\n", c);
		break;

	default:	/* keep gcc happy */
		break;
	}

	return 0;
}

static unsigned char inline
xor_mask(struct libwebsocket *wsi, unsigned char c)
{
	c ^= wsi->masking_key_04[wsi->frame_mask_index++];
	if (wsi->frame_mask_index == 20)
		wsi->frame_mask_index = 0;

	return c;
}


static int libwebsocket_rx_sm(struct libwebsocket *wsi, unsigned char c)
{
	int n;
	unsigned char buf[20 + 4];

	switch (wsi->lws_rx_parse_state) {
	case LWS_RXPS_NEW:

		switch (wsi->ietf_spec_revision) {
		/* Firefox 4.0b6 likes this as of 30 Oct */
		case 0:
			if (c == 0xff)
				wsi->lws_rx_parse_state = LWS_RXPS_SEEN_76_FF;
			if (c == 0) {
				wsi->lws_rx_parse_state =
						       LWS_RXPS_EAT_UNTIL_76_FF;
				wsi->rx_user_buffer_head = 0;
			}
			break;
		case 4:
			wsi->frame_masking_nonce_04[0] = c;
			wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_1;
			break;
		}
		break;
	case LWS_RXPS_04_MASK_NONCE_1:
		wsi->frame_masking_nonce_04[1] = c;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_2;
		break;
	case LWS_RXPS_04_MASK_NONCE_2:
		wsi->frame_masking_nonce_04[2] = c;
		wsi->lws_rx_parse_state = LWS_RXPS_04_MASK_NONCE_3;
		break;
	case LWS_RXPS_04_MASK_NONCE_3:
		wsi->frame_masking_nonce_04[3] = c;

		/*
		 * we are able to compute the frame key now
		 * it's a SHA1 of ( frame nonce we were just sent, concatenated
		 * with the connection masking key we computed at handshake
		 * time ) -- yeah every frame from the client invokes a SHA1
		 * for no real reason so much for lightweight.
		 */

		buf[0] = wsi->frame_masking_nonce_04[0];
		buf[1] = wsi->frame_masking_nonce_04[1];
		buf[2] = wsi->frame_masking_nonce_04[2];
		buf[3] = wsi->frame_masking_nonce_04[3];

		memcpy(buf + 4, wsi->masking_key_04, 20);

		/*
		 * wsi->frame_mask_04 will be our recirculating 20-byte XOR key
		 * for this frame
		 */
	
		SHA1((unsigned char *)buf, 4 + 20, wsi->frame_mask_04);

		/*
		 * start from the zero'th byte in the XOR key buffer since
		 * this is the start of a frame with a new key
		 */

		wsi->frame_mask_index = 0;
		
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_1;
		break;

	/*
	 *  04 logical framing from the spec (all this is masked when incoming
	 *  and has to be unmasked)
	 *
	 * We ignore the possibility of extension data because we don't
	 * negotiate any extensions at the moment.
	 * 
	 *    0                   1                   2                   3
	 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *   +-+-+-+-+-------+-+-------------+-------------------------------+
	 *   |F|R|R|R| opcode|R| Payload len |    Extended payload length    |
	 *   |I|S|S|S|  (4)  |S|     (7)     |             (16/63)           |
	 *   |N|V|V|V|       |V|             |   (if payload len==126/127)   |
	 *   | |1|2|3|       |4|             |                               |
	 *   +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	 *   |     Extended payload length continued, if payload len == 127  |
	 *   + - - - - - - - - - - - - - - - +-------------------------------+
	 *   |                               |         Extension data        |
	 *   +-------------------------------+ - - - - - - - - - - - - - - - +
	 *   :                                                               :
	 *   +---------------------------------------------------------------+
	 *   :                       Application data                        :
	 *   +---------------------------------------------------------------+
	 *
	 *  We pass payload through to userland as soon as we get it, ignoring
	 *  FIN.  It's up to userland to buffer it up if it wants to see a
	 *  whole unfragmented block of the original size (which may be up to
	 *  2^63 long!)
	 */

	case LWS_RXPS_04_FRAME_HDR_1:
		/*
		 * 04 spec defines the opcode like this: (1, 2, and 3 are
		 * "control frame" opcodes which may not be fragmented or
		 * have size larger than 126)
		 * 
		 *       frame-opcode           =
		 * 	       %x0 ; continuation frame
		 *           / %x1 ; connection close
		 *           / %x2 ; ping
		 *           / %x3 ; pong
		 *           / %x4 ; text frame
		 *           / %x5 ; binary frame
		 *           / %x6-F ; reserved
		 *
		 * 	  FIN (b7)
		 */

		c = xor_mask(wsi, c);

		if (c & 0x70) {
			fprintf(stderr, "Frame has extensions set illegally 1\n");
			/* kill the connection */
			return -1;
		}

		wsi->opcode = c & 0xf;
		wsi->final = !!((c >> 7) & 1);

		if (wsi->final &&
			wsi->opcode == LWS_WS_OPCODE_04__CONTINUATION &&
						   wsi->rx_packet_length == 0) {
			fprintf(stderr,
				      "Frame starts with final continuation\n");
			/* kill the connection */
			return -1;
		}

		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN:
		c = xor_mask(wsi, c);

		if (c & 0x80) {
			fprintf(stderr, "Frame has extensions "
							   "set illegally 2\n");
			/* kill the connection */
			return -1;
		}

		switch (c) {
		case 126:
			/* control frames are not allowed to have big lengths */
			switch (wsi->opcode) {
			case LWS_WS_OPCODE_04__CLOSE:
			case LWS_WS_OPCODE_04__PING:
			case LWS_WS_OPCODE_04__PONG:
				fprintf(stderr, "Control frame asking for "
						"extended length is illegal\n");
				/* kill the connection */
				return -1;
			default:
				break;
			}
			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_2;
			break;
		case 127:
			/* control frames are not allowed to have big lengths */
			switch (wsi->opcode) {
			case LWS_WS_OPCODE_04__CLOSE:
			case LWS_WS_OPCODE_04__PING:
			case LWS_WS_OPCODE_04__PONG:
				fprintf(stderr, "Control frame asking for "
						"extended length is illegal\n");
				/* kill the connection */
				return -1;
			default:
				break;
			}
			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_8;
			break;
		default:
//			fprintf(stderr, "seen incoming 04 frame len %d\n", c);
			wsi->rx_packet_length = c;
			wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
			break;
		}
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_2:
		c = xor_mask(wsi, c);

		wsi->rx_packet_length = c << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_1:
		c = xor_mask(wsi, c);

		wsi->rx_packet_length |= c;
		wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_8:
		c = xor_mask(wsi, c);
		if (c & 0x80) {
			fprintf(stderr, "b63 of length must be zero\n");
			/* kill the connection */
			return -1;
		}
		wsi->rx_packet_length = ((size_t)c) << 56;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_7;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_7:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c)) << 48;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_6;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_6:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c)) << 40;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_5;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_5:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c)) << 32;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_4;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_4:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c)) << 24;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_3;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_3:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c)) << 16;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_2;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_2:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c)) << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_1:
		wsi->rx_packet_length |= ((size_t)xor_mask(wsi, c));
		wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_EAT_UNTIL_76_FF:
		if (c == 0xff) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto issue;
		}
		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
					      (wsi->rx_user_buffer_head++)] = c;

		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
issue:
		if (wsi->protocol->callback)
			wsi->protocol->callback(wsi, LWS_CALLBACK_RECEIVE,
			  wsi->user_space,
			  &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
			  wsi->rx_user_buffer_head);
		wsi->rx_user_buffer_head = 0;
		break;
	case LWS_RXPS_SEEN_76_FF:
		if (c)
			break;

		debug("Seen that client is requesting "
				"a v76 close, sending ack\n");
		buf[0] = 0xff;
		buf[1] = 0;
		n = libwebsocket_write(wsi, buf, 2, LWS_WRITE_HTTP);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
		debug("  v76 close ack sent, server closing skt\n");
		/* returning < 0 will get it closed in parent */
		return -1;

	case LWS_RXPS_PULLING_76_LENGTH:
		break;

	case LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED:
		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
			       (wsi->rx_user_buffer_head++)] = xor_mask(wsi, c);

		if (--wsi->rx_packet_length == 0) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto spill;
		}
		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
spill:
		/*
		 * is this frame a control packet we should take care of at this
		 * layer?  If so service it and hide it from the user callback
		 */

		switch (wsi->opcode) {
		case LWS_WS_OPCODE_04__CLOSE:
			/* parrot the close packet payload back */
			n = libwebsocket_write(wsi, (unsigned char *)
			   &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
				     wsi->rx_user_buffer_head, LWS_WRITE_CLOSE);
			/* close the connection */
			return -1;

		case LWS_WS_OPCODE_04__PING:
			/* parrot the ping packet payload back as a pong*/
			n = libwebsocket_write(wsi, (unsigned char *)
			    &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
				    wsi->rx_user_buffer_head, LWS_WRITE_PONG);
			break;

		case LWS_WS_OPCODE_04__PONG:
			/* keep the statistics... */
			wsi->pings_vs_pongs--;
			/* ... then just drop it */
			wsi->rx_user_buffer_head = 0;
			return 0;

		default:
			break;
		}

		/*
		 * No it's real payload, pass it up to the user callback.
		 * It's nicely buffered with the pre-padding taken care of
		 * so it can be sent straight out again using libwebsocket_write
		 */

		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
					       wsi->rx_user_buffer_head] = '\0';

		if (wsi->protocol->callback)
			wsi->protocol->callback(wsi, LWS_CALLBACK_RECEIVE,
						wsi->user_space,
			  &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
						      wsi->rx_user_buffer_head);
		wsi->rx_user_buffer_head = 0;
		break;
	}

	return 0;
}


int libwebsocket_client_rx_sm(struct libwebsocket *wsi, unsigned char c)
{
	int n;
	unsigned char buf[20 + 4];

	switch (wsi->lws_rx_parse_state) {
	case LWS_RXPS_NEW:

		switch (wsi->ietf_spec_revision) {
		/* Firefox 4.0b6 likes this as of 30 Oct */
		case 0:
			if (c == 0xff)
				wsi->lws_rx_parse_state = LWS_RXPS_SEEN_76_FF;
			if (c == 0) {
				wsi->lws_rx_parse_state =
						       LWS_RXPS_EAT_UNTIL_76_FF;
				wsi->rx_user_buffer_head = 0;
			}
			break;
		case 4:
		/*
		 *  04 logical framing from the spec (all this is masked when incoming
		 *  and has to be unmasked)
		 *
		 * We ignore the possibility of extension data because we don't
		 * negotiate any extensions at the moment.
		 * 
		 *    0                   1                   2                   3
		 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		 *   +-+-+-+-+-------+-+-------------+-------------------------------+
		 *   |F|R|R|R| opcode|R| Payload len |    Extended payload length    |
		 *   |I|S|S|S|  (4)  |S|     (7)     |             (16/63)           |
		 *   |N|V|V|V|       |V|             |   (if payload len==126/127)   |
		 *   | |1|2|3|       |4|             |                               |
		 *   +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
		 *   |     Extended payload length continued, if payload len == 127  |
		 *   + - - - - - - - - - - - - - - - +-------------------------------+
		 *   |                               |         Extension data        |
		 *   +-------------------------------+ - - - - - - - - - - - - - - - +
		 *   :                                                               :
		 *   +---------------------------------------------------------------+
		 *   :                       Application data                        :
		 *   +---------------------------------------------------------------+
		 *
		 *  We pass payload through to userland as soon as we get it, ignoring
		 *  FIN.  It's up to userland to buffer it up if it wants to see a
		 *  whole unfragmented block of the original size (which may be up to
		 *  2^63 long!)
		 */

		/*
		 * 04 spec defines the opcode like this: (1, 2, and 3 are
		 * "control frame" opcodes which may not be fragmented or
		 * have size larger than 126)
		 * 
		 *       frame-opcode           =
		 * 	       %x0 ; continuation frame
		 *           / %x1 ; connection close
		 *           / %x2 ; ping
		 *           / %x3 ; pong
		 *           / %x4 ; text frame
		 *           / %x5 ; binary frame
		 *           / %x6-F ; reserved
		 *
		 * 	  FIN (b7)
		 */

			if (c & 0x70) {
				fprintf(stderr, "Frame has extensions set "
				   "illegally on first framing byte %02X\n", c);
				/* kill the connection */
				return -1;
			}

			wsi->opcode = c & 0xf;
			wsi->final = !!((c >> 7) & 1);

			if (wsi->final &&
			    wsi->opcode == LWS_WS_OPCODE_04__CONTINUATION &&
						   wsi->rx_packet_length == 0) {
				fprintf(stderr,
				      "Frame starts with final continuation\n");
				/* kill the connection */
				return -1;
			}

			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN;
			break;
		}
		break;


	case LWS_RXPS_04_FRAME_HDR_LEN:

		if (c & 0x80) {
			fprintf(stderr,
				      "Frame has extensions set illegally 4\n");
			/* kill the connection */
			return -1;
		}

		switch (c) {
		case 126:
			/* control frames are not allowed to have big lengths */
			switch (wsi->opcode) {
			case LWS_WS_OPCODE_04__CLOSE:
			case LWS_WS_OPCODE_04__PING:
			case LWS_WS_OPCODE_04__PONG:
				fprintf(stderr, "Control frame asking for "
						"extended length is illegal\n");
				/* kill the connection */
				return -1;
			default:
				break;
			}
			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_2;
			break;
		case 127:
			/* control frames are not allowed to have big lengths */
			switch (wsi->opcode) {
			case LWS_WS_OPCODE_04__CLOSE:
			case LWS_WS_OPCODE_04__PING:
			case LWS_WS_OPCODE_04__PONG:
				fprintf(stderr, "Control frame asking for "
						"extended length is illegal\n");
				/* kill the connection */
				return -1;
			default:
				break;
			}
			wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_8;
			break;
		default:
			wsi->rx_packet_length = c;
			wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
			break;
		}
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_2:
		wsi->rx_packet_length = c << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN16_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN16_1:
		wsi->rx_packet_length |= c;
		wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_8:
		if (c & 0x80) {
			fprintf(stderr, "b63 of length must be zero\n");
			/* kill the connection */
			return -1;
		}
		wsi->rx_packet_length = ((size_t)c) << 56;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_7;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_7:
		wsi->rx_packet_length |= ((size_t)c) << 48;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_6;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_6:
		wsi->rx_packet_length |= ((size_t)c) << 40;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_5;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_5:
		wsi->rx_packet_length |= ((size_t)c) << 32;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_4;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_4:
		wsi->rx_packet_length |= ((size_t)c) << 24;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_3;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_3:
		wsi->rx_packet_length |= ((size_t)c) << 16;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_2;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_2:
		wsi->rx_packet_length |= ((size_t)c) << 8;
		wsi->lws_rx_parse_state = LWS_RXPS_04_FRAME_HDR_LEN64_1;
		break;

	case LWS_RXPS_04_FRAME_HDR_LEN64_1:
		wsi->rx_packet_length |= (size_t)c;
		wsi->lws_rx_parse_state =
					LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED;
		break;

	case LWS_RXPS_EAT_UNTIL_76_FF:
		if (c == 0xff) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto issue;
		}
		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
					      (wsi->rx_user_buffer_head++)] = c;

		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
issue:
		if (wsi->protocol->callback)
			wsi->protocol->callback(wsi,
						LWS_CALLBACK_CLIENT_RECEIVE,
						wsi->user_space,
			  &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
						      wsi->rx_user_buffer_head);
		wsi->rx_user_buffer_head = 0;
		break;
	case LWS_RXPS_SEEN_76_FF:
		if (c)
			break;

		debug("Seen that client is requesting "
				"a v76 close, sending ack\n");
		buf[0] = 0xff;
		buf[1] = 0;
		n = libwebsocket_write(wsi, buf, 2, LWS_WRITE_HTTP);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
		debug("  v76 close ack sent, server closing skt\n");
		/* returning < 0 will get it closed in parent */
		return -1;

	case LWS_RXPS_PULLING_76_LENGTH:
		break;

	case LWS_RXPS_PAYLOAD_UNTIL_LENGTH_EXHAUSTED:
		wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING +
				 (wsi->rx_user_buffer_head++)] = c;
		if (--wsi->rx_packet_length == 0) {
			wsi->lws_rx_parse_state = LWS_RXPS_NEW;
			goto spill;
		}
		if (wsi->rx_user_buffer_head != MAX_USER_RX_BUFFER)
			break;
spill:
		/*
		 * is this frame a control packet we should take care of at this
		 * layer?  If so service it and hide it from the user callback
		 */

		switch (wsi->opcode) {
		case LWS_WS_OPCODE_04__CLOSE:
			/* parrot the close packet payload back */
			n = libwebsocket_write(wsi, (unsigned char *)
			   &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
				     wsi->rx_user_buffer_head, LWS_WRITE_CLOSE);
			/* close the connection */
			return -1;

		case LWS_WS_OPCODE_04__PING:
			/* parrot the ping packet payload back as a pong*/
			n = libwebsocket_write(wsi, (unsigned char *)
			    &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
				    wsi->rx_user_buffer_head, LWS_WRITE_PONG);
			break;

		case LWS_WS_OPCODE_04__PONG:
			/* keep the statistics... */
			wsi->pings_vs_pongs--;
			/* ... then just drop it */
			wsi->rx_user_buffer_head = 0;
			return 0;

		default:
			break;
		}

		/*
		 * No it's real payload, pass it up to the user callback.
		 * It's nicely buffered with the pre-padding taken care of
		 * so it can be sent straight out again using libwebsocket_write
		 */

		if (wsi->protocol->callback)
			wsi->protocol->callback(wsi,
						LWS_CALLBACK_CLIENT_RECEIVE,
						wsi->user_space,
			  &wsi->rx_user_buffer[LWS_SEND_BUFFER_PRE_PADDING],
						      wsi->rx_user_buffer_head);
		wsi->rx_user_buffer_head = 0;
		break;
	default:
		fprintf(stderr, "client rx illegal state\n");
		return 1;
	}

	return 0;
}



int libwebsocket_interpret_incoming_packet(struct libwebsocket *wsi,
						 unsigned char *buf, size_t len)
{
	int n;

#ifdef DEBUG
	fprintf(stderr, "received %d byte packet\n", (int)len);
	for (n = 0; n < len; n++)
		fprintf(stderr, "%02X ", buf[n]);
	fprintf(stderr, "\n");
#endif

	/* let the rx protocol state machine have as much as it needs */

	n = 0;
	while (n < len)
		if (libwebsocket_rx_sm(wsi, buf[n++]) < 0)
			return -1;

	return 0;
}


static int
libwebsocket_04_frame_mask_generate(struct libwebsocket *wsi)
{
	int fd;
	char buf[4 + 20];
	int n;

	/* fetch the per-frame nonce */

	fd = open(SYSTEM_RANDOM_FILEPATH, O_RDONLY);
	if (fd < 1) {
		fprintf(stderr, "Unable to open random device %s\n",
							SYSTEM_RANDOM_FILEPATH);
		return 1;
	}
	n = read(fd, wsi->frame_masking_nonce_04, 4);
	if (n != 4) {
		fprintf(stderr, "Unable to read from random device %s %d\n",
						     SYSTEM_RANDOM_FILEPATH, n);
		return 1;
	}
	close(fd);

	/*
	 * the frame key is the frame nonce (4 bytes) followed by the
	 * connection masking key, hashed by SHA1
	 */

	memcpy(buf, wsi->frame_masking_nonce_04, 4);
	memcpy(buf + 4, wsi->masking_key_04, 20);

	/* concatenate the nonce with the connection key then hash it */

	SHA1((unsigned char *)buf, 4 + 20, wsi->frame_mask_04);

	/* start masking from first byte of masking key buffer */
	wsi->frame_mask_index = 0;

	return 0;
}


/**
 * libwebsocket_write() - Apply protocol then write data to client
 * @wsi:	Websocket instance (available from user callback)
 * @buf:	The data to send.  For data being sent on a websocket
 *		connection (ie, not default http), this buffer MUST have
 *		LWS_SEND_BUFFER_PRE_PADDING bytes valid BEFORE the pointer
 *		and an additional LWS_SEND_BUFFER_POST_PADDING bytes valid
 *		in the buffer after (buf + len).  This is so the protocol
 *		header and trailer data can be added in-situ.
 * @len:	Count of the data bytes in the payload starting from buf
 * @protocol:	Use LWS_WRITE_HTTP to reply to an http connection, and one
 *		of LWS_WRITE_BINARY or LWS_WRITE_TEXT to send appropriate
 *		data on a websockets connection.  Remember to allow the extra
 *		bytes before and after buf if LWS_WRITE_BINARY or LWS_WRITE_TEXT
 *		are used.
 *
 *	This function provides the way to issue data back to the client
 *	for both http and websocket protocols.
 *
 *	In the case of sending using websocket protocol, be sure to allocate
 *	valid storage before and after buf as explained above.  This scheme
 *	allows maximum efficiency of sending data and protocol in a single
 *	packet while not burdening the user code with any protocol knowledge.
 */

 /* FIXME FIN bit */

int libwebsocket_write(struct libwebsocket *wsi, unsigned char *buf,
			  size_t len, enum libwebsocket_write_protocol protocol)
{
	int n;
	int pre = 0;
	int post = 0;
	unsigned int shift = 7;

	if (protocol == LWS_WRITE_HTTP)
		goto send_raw;

	/* websocket protocol, either binary or text */

	if (wsi->state != WSI_STATE_ESTABLISHED)
		return -1;

	switch (wsi->ietf_spec_revision) {
	/* chrome likes this as of 30 Oct */
	/* Firefox 4.0b6 likes this as of 30 Oct */
	case 0:
		if (protocol == LWS_WRITE_BINARY) {
			/* in binary mode we send 7-bit used length blocks */
			pre = 1;
			while (len & (127 << shift)) {
				pre++;
				shift += 7;
			}
			n = 0;
			shift -= 7;
			while (shift >= 0) {
				if (shift)
					buf[0 - pre + n] =
						  ((len >> shift) & 127) | 0x80;
				else
					buf[0 - pre + n] =
						  ((len >> shift) & 127);
				n++;
				shift -= 7;
			}
			break;
		}

		/* frame type = text, length-free spam mode */

		buf[-1] = 0;
		buf[len] = 0xff; /* EOT marker */
		pre = 1;
		post = 1;
		break;

	case 4:

		switch (protocol) {
		case LWS_WRITE_TEXT:
			n = LWS_WS_OPCODE_04__TEXT_FRAME;
			break;
		case LWS_WRITE_BINARY:
			n = LWS_WS_OPCODE_04__BINARY_FRAME;
			break;
		case LWS_WRITE_CLOSE:
			n = LWS_WS_OPCODE_04__CLOSE;
			break;
		case LWS_WRITE_PING:
			n = LWS_WS_OPCODE_04__PING;
			wsi->pings_vs_pongs++;
			break;
		case LWS_WRITE_PONG:
			n = LWS_WS_OPCODE_04__PONG;
			break;
		default:
			fprintf(stderr, "libwebsocket_write: unknown write "
							 "opcode / protocol\n");
			return -1;
		}

		/*
		 * We don't really support the metaframe concept with FIN.
		 * Just set FIN on every packet for now
		 */

		n |= 1 << 7;

		if (len < 126) {
			buf[-2] = n;
			buf[-1] = len;
			pre = 2;
		} else {
			if (len < 65536) {
				buf[-4] = n;
				buf[-3] = 126;
				buf[-2] = len >> 8;
				buf[-1] = len;
				pre = 4;
			} else {
				buf[-10] = n;
				buf[-9] = 127;
#if defined __LP64__
					buf[-8] = (len >> 56) & 0x7f;
					buf[-7] = len >> 48;
					buf[-6] = len >> 40;
					buf[-5] = len >> 32;
#else
					buf[-8] = 0;
					buf[-7] = 0;
					buf[-6] = 0;
					buf[-5] = 0;
#endif
				buf[-4] = len >> 24;
				buf[-3] = len >> 16;
				buf[-2] = len >> 8;
				buf[-1] = len;
				pre = 10;
			}
		}
		break;
	}

#if 0
	for (n = 0; n < (len + pre + post); n++)
		fprintf(stderr, "%02X ", buf[n - pre]);

	fprintf(stderr, "\n");
#endif

	/*
	 * Deal with masking if appropriate
	 */

	if (wsi->client_mode && wsi->ietf_spec_revision == 4) {

		if (libwebsocket_04_frame_mask_generate(wsi)) {
			fprintf(stderr, "libwebsocket_write: "
					      "frame mask generation failed\n");
			return 1;
		}

		/*
		 * use the XOR masking against everything we send
		 * past the frame nonce
		 */

		for (n = 0; n < (len + pre + post); n++)
			buf[n - pre] = xor_mask(wsi, buf[n - pre]);

		/* make space for the frame nonce in clear */
		pre += 4;

		/* copy the frame nonce into place */
		memcpy(&buf[0 - pre], wsi->frame_masking_nonce_04, 4);
	}


send_raw:
#ifdef LWS_OPENSSL_SUPPORT
	if (use_ssl) {
		n = SSL_write(wsi->ssl, buf - pre, len + pre + post);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
	} else {
#endif
		n = send(wsi->sock, buf - pre, len + pre + post, 0);
		if (n < 0) {
			fprintf(stderr, "ERROR writing to socket");
			return -1;
		}
#ifdef LWS_OPENSSL_SUPPORT
	}
#endif
	debug("written %d bytes to client\n", (int)len);

	return 0;
}


/**
 * libwebsockets_serve_http_file() - Send a file back to the client using http
 * @wsi:		Websocket instance (available from user callback)
 * @file:		The file to issue over http
 * @content_type:	The http content type, eg, text/html
 *
 *	This function is intended to be called from the callback in response
 *	to http requests from the client.  It allows the callback to issue
 *	local files down the http link in a single step.
 */

int libwebsockets_serve_http_file(struct libwebsocket *wsi, const char *file,
						       const char *content_type)
{
	int fd;
	struct stat stat;
	char buf[512];
	char *p = buf;
	int n;

	fd = open(file, O_RDONLY);
	if (fd < 1) {
		p += sprintf(p, "HTTP/1.0 400 Bad\x0d\x0a"
			"Server: libwebsockets\x0d\x0a"
			"\x0d\x0a"
		);
		libwebsocket_write(wsi, (unsigned char *)buf, p - buf,
								LWS_WRITE_HTTP);

		return -1;
	}

	fstat(fd, &stat);
	p += sprintf(p, "HTTP/1.0 200 OK\x0d\x0a"
			"Server: libwebsockets\x0d\x0a"
			"Content-Type: %s\x0d\x0a"
			"Content-Length: %u\x0d\x0a"
			"\x0d\x0a", content_type, (unsigned int)stat.st_size);

	libwebsocket_write(wsi, (unsigned char *)buf, p - buf, LWS_WRITE_HTTP);

	n = 1;
	while (n > 0) {
		n = read(fd, buf, 512);
		libwebsocket_write(wsi, (unsigned char *)buf, n,
								LWS_WRITE_HTTP);
	}

	close(fd);

	return 0;
}


/**
 * libwebsockets_remaining_packet_payload() - Bytes to come before "overall"
 * 					      rx packet is complete
 * @wsi:		Websocket instance (available from user callback)
 *
 *	This function is intended to be called from the callback if the
 *  user code is interested in "complete packets" from the client.
 *  libwebsockets just passes through payload as it comes and issues a buffer
 *  additionally when it hits a built-in limit.  The LWS_CALLBACK_RECEIVE
 *  callback handler can use this API to find out if the buffer it has just
 *  been given is the last piece of a "complete packet" from the client --
 *  when that is the case libwebsockets_remaining_packet_payload() will return
 *  0.
 *
 *  Many protocols won't care becuse their packets are always small.
 */

size_t
libwebsockets_remaining_packet_payload(struct libwebsocket *wsi)
{
	return wsi->rx_packet_length;
}
