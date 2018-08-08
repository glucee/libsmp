/* libsmp
 * Copyright (C) 2017-2018 Actronika SAS
 *     Author: Aurélien Zanelli <aurelien.zanelli@actronika.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "serial-protocol.h"

#include <stdlib.h>

#include "libsmp.h"
#include "libsmp-private.h"

#define START_BYTE 0x10
#define END_BYTE 0xFF
#define ESC_BYTE 0x1B

#define DEFAULT_BUFFER_SIZE 1024

static inline int is_magic_byte(uint8_t byte)
{
    return (byte == START_BYTE || byte == END_BYTE || byte == ESC_BYTE);
}

static size_t compute_payload_size(const uint8_t *buf, size_t size)
{
    size_t ret;
    size_t i;

    /* We need at least three extra bytes (START, END and CRC) */
    ret = size + 3;

    /* count the number of extra bytes */
    for (i = 0; i < size; i++) {
        if (is_magic_byte(buf[i]))
            ret++;
    }

    return ret;
}

static uint8_t compute_checksum(const uint8_t *buf, size_t size)
{
    uint8_t checksum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        checksum ^= buf[i];
    }

    return checksum;
}

/* dest should be able to contain at least 2 bytes. returns the number of
 * bytes written */
static int smp_serial_protocol_write_byte(uint8_t *dest, uint8_t byte)
{
    int offset = 0;

    /* escape special byte */
    if (is_magic_byte(byte))
        dest[offset++] = ESC_BYTE;

    dest[offset++] = byte;

    return offset;
}

static int
smp_serial_protocol_decoder_process_byte_inframe(
        SmpSerialProtocolDecoder *decoder, uint8_t byte, uint8_t **frame,
        size_t *framesize)
{
    int ret;

    switch (byte) {
        case START_BYTE:
            /* we are in a frame without end byte, resync on current byte */
            decoder->offset = 0;
            ret = SMP_ERROR_BAD_MESSAGE;
            break;
        case ESC_BYTE:
            decoder->state = SMP_SERIAL_PROTOCOL_DECODER_STATE_IN_FRAME_ESC;
            ret = 0;
            break;
        case END_BYTE: {
           uint8_t cs;

           /* we should at least have the CRC */
           if (decoder->offset < 1) {
               decoder->state = SMP_SERIAL_PROTOCOL_DECODER_STATE_WAIT_HEADER;
               ret = SMP_ERROR_BAD_MESSAGE;
               break;
           }

           /* framesize is without the CRC */
           *framesize = decoder->offset - 1;

           /* frame complete, check crc and call user callback */
           cs = compute_checksum(decoder->buf, *framesize);
           if (cs != decoder->buf[decoder->offset - 1]) {
               ret = SMP_ERROR_BAD_MESSAGE;
           } else {
               *frame = decoder->buf;
               ret = 0;
           }

           decoder->state = SMP_SERIAL_PROTOCOL_DECODER_STATE_WAIT_HEADER;
           break;
        }
        default:
            if (decoder->offset >= decoder->bufsize) {
                ret = SMP_ERROR_TOO_BIG;
                break;
            }

            decoder->buf[decoder->offset++] = byte;
            ret = 0;
            break;
    }

    return ret;
}

static int smp_serial_protocol_decoder_init(SmpSerialProtocolDecoder *decoder,
        uint8_t *buf, size_t bufsize, bool statically_allocated)
{
    if (buf == NULL) {
        buf = calloc(bufsize, sizeof(uint8_t));
        if (buf == NULL)
            return SMP_ERROR_NO_MEM;
    }

    decoder->state = SMP_SERIAL_PROTOCOL_DECODER_STATE_WAIT_HEADER;
    decoder->buf = buf;
    decoder->bufsize = bufsize;
    decoder->statically_allocated = statically_allocated;
    return 0;
}

/* API */
SmpSerialProtocolDecoder *smp_serial_protocol_decoder_new(size_t bufsize)
{
    SmpSerialProtocolDecoder *decoder;
    int ret;

    decoder = smp_new(SmpSerialProtocolDecoder);
    if (decoder == NULL)
        return NULL;

    if (bufsize == 0)
        bufsize = DEFAULT_BUFFER_SIZE;

    ret = smp_serial_protocol_decoder_init(decoder, NULL, bufsize, false);
    if (ret != 0) {
        free(decoder);
        return NULL;
    }


    return decoder;
}

SmpSerialProtocolDecoder *smp_serial_protocol_decoder_new_from_static(
        SmpStaticSerialProtocolDecoder *sdec, size_t struct_size, uint8_t *buf,
        size_t bufsize)
{
    SmpSerialProtocolDecoder *decoder = (SmpSerialProtocolDecoder *) sdec;
    int ret;

    return_val_if_fail(sdec != NULL, NULL);
    return_val_if_fail(struct_size >= sizeof(SmpSerialProtocolDecoder), NULL);
    return_val_if_fail(buf != NULL, NULL);
    return_val_if_fail(bufsize != 0, NULL);

    ret = smp_serial_protocol_decoder_init(decoder, buf, bufsize, true);
    if (ret != 0)
        return NULL;

    return decoder;
}

void smp_serial_protocol_decoder_free(SmpSerialProtocolDecoder *decoder)
{
    return_if_fail(decoder != NULL);

    if (decoder->statically_allocated)
        return;

    free(decoder->buf);
    free(decoder);
}

/* frame contains the new frame if one is finished */
int smp_serial_protocol_decoder_process_byte(SmpSerialProtocolDecoder *decoder,
        uint8_t byte, uint8_t **frame, size_t *framesize)
{
    int ret;

    return_val_if_fail(decoder != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(frame != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(framesize != NULL, SMP_ERROR_INVALID_PARAM);

    *frame = NULL;
    *framesize = 0;

    switch (decoder->state) {
        case SMP_SERIAL_PROTOCOL_DECODER_STATE_WAIT_HEADER:
            if (byte == START_BYTE) {
                decoder->state = SMP_SERIAL_PROTOCOL_DECODER_STATE_IN_FRAME;
                decoder->offset = 0;
            }
            ret = 0;
            break;

        case SMP_SERIAL_PROTOCOL_DECODER_STATE_IN_FRAME:
            ret = smp_serial_protocol_decoder_process_byte_inframe(decoder,
                    byte, frame, framesize);
            break;

        case SMP_SERIAL_PROTOCOL_DECODER_STATE_IN_FRAME_ESC:
            if (decoder->offset >= decoder->bufsize) {
                ret = SMP_ERROR_TOO_BIG;
                break;
            }

            decoder->buf[decoder->offset++] = byte;
            decoder->state = SMP_SERIAL_PROTOCOL_DECODER_STATE_IN_FRAME;
            ret = 0;
            break;

        default:
            ret = SMP_ERROR_OTHER;
            break;
    }

    return ret;
}

/* if *outbuf == NULL, it will be allocated */
ssize_t smp_serial_protocol_encode(const uint8_t *inbuf, size_t insize,
        uint8_t **outbuf, size_t outsize)
{
    uint8_t *txbuf;
    size_t payload_size;
    size_t offset = 0;
    size_t i;

    return_val_if_fail(inbuf != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(outbuf != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(*outbuf == NULL || (*outbuf != NULL && outsize > 0),
            SMP_ERROR_INVALID_PARAM);

    payload_size = compute_payload_size(inbuf, insize);
    if (*outbuf == NULL) {
        txbuf = malloc(payload_size);
        if (txbuf == NULL)
            return SMP_ERROR_NO_MEM;
    } else {
        if (outsize < payload_size)
            return SMP_ERROR_OVERFLOW;

        txbuf = *outbuf;
    }

    txbuf[offset++] = START_BYTE;
    for (i = 0; i < insize; i++)
        offset += smp_serial_protocol_write_byte(txbuf + offset, inbuf[i]);

    offset += smp_serial_protocol_write_byte(txbuf + offset,
            compute_checksum(inbuf, insize));
    txbuf[offset++] = END_BYTE;

    *outbuf = txbuf;

    return offset;
}