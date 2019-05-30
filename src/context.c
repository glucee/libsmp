/* libsmp
 * Copyright (C) 2018 Actronika SAS
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
/**
 * @file
 * \defgroup context Context
 *
 * Use a device to send and receive messages.
 */

#define SMP_ENABLE_STATIC_API
#include "context.h"
#include "libsmp-private.h"
#include <stdlib.h>

#include "buffer.h"
#include "serial-device.h"

SMP_STATIC_ASSERT(sizeof(SmpContext) == sizeof(SmpStaticContext));

static void smp_context_init(SmpContext *ctx, SmpSerialProtocolDecoder *decoder,
        const SmpEventCallbacks *cbs, void *userdata, bool statically_allocated)
{
    ctx->decoder = decoder;
    smp_serial_device_init(&ctx->device);
    ctx->cbs = *cbs;
    ctx->userdata = userdata;
    ctx->opened = false;
    ctx->statically_allocated = statically_allocated;
}

static void smp_context_notify_new_message(SmpContext *ctx, SmpMessage *msg)
{
    if (ctx->cbs.new_message_cb != NULL)
        ctx->cbs.new_message_cb(ctx, msg, ctx->userdata);
}

static void smp_context_notify_error(SmpContext *ctx, SmpError err)
{
    if (ctx->cbs.error_cb != NULL)
        ctx->cbs.error_cb(ctx, err, ctx->userdata);
}

static void smp_context_process_serial_frame(SmpContext *ctx, uint8_t *frame,
        size_t framesize)
{
    SmpMessage *msg;
    int ret;

    if (ctx->msg_rx != NULL)
        msg = ctx->msg_rx;
    else
        msg = smp_message_new();

    if (msg == NULL) {
        smp_context_notify_error(ctx, SMP_ERROR_NO_MEM);
        return;
    }

    ret = smp_message_build_from_buffer(msg, frame, framesize);
    if (ret < 0) {
        smp_context_notify_error(ctx, ret);
        return;
    };

    smp_context_notify_new_message(ctx, msg);

    if (ctx->msg_rx == NULL)
        smp_message_free(msg);
    else
        smp_message_clear(msg);
}

/* API */

/**
 * \ingroup context
 * Create a new SmpContext object and attached the provided callback to it.
 *
 * @param[in] cbs callback to use to notify events
 * @param[in] userdata a pointer to userdata which will be passed to callback
 *
 * @return a pointer to a new SmpContext or NULL on error.
 */
SmpContext *smp_context_new(const SmpEventCallbacks *cbs, void *userdata)
{
    SmpContext *ctx;
    SmpSerialProtocolDecoder *decoder;

    return_val_if_fail(cbs != NULL, NULL);

    ctx = smp_new(SmpContext);
    if (ctx == NULL)
        return NULL;

    decoder = smp_serial_protocol_decoder_new(0);
    if (decoder == NULL) {
        free(ctx);
        return NULL;
    }

    smp_context_init(ctx, decoder, cbs, userdata, false);
    return ctx;
}

/**
 * \ingroup context
 * Create a new SmpContext object from a static storage.
 *
 * @param[in] sctx a SmpStaticContext
 * @param[in] struct_size the size of sctx
 * @param[in] cbs callback to use to notify events
 * @param[in] userdata a pointer to userdata which will be passed to callback
 * @param[in] decoder a SmpSerialProtocolDecoder to use with this context
 * @param[in] serial_tx a SmpBuffer to use as serial TX buffer
 * @param[in] msg_tx a SmpBuffer to use as msg TX buffer
 * @param[in] msg_rx a SmpMessage to use for reception
 *
 * @return a SmpContext or NULL on error.
 */
SmpContext *smp_context_new_from_static(SmpStaticContext *sctx,
        size_t struct_size, const SmpEventCallbacks *cbs, void *userdata,
        SmpSerialProtocolDecoder *decoder, SmpBuffer *serial_tx,
        SmpBuffer *msg_tx, SmpMessage *msg_rx)
{
    SmpContext *ctx = (SmpContext *) sctx;

    return_val_if_fail(sctx != NULL, NULL);
    return_val_if_fail(struct_size >= sizeof(SmpContext), NULL);
    return_val_if_fail(cbs != NULL, NULL);
    return_val_if_fail(decoder != NULL, NULL);
    return_val_if_fail(msg_tx != NULL, NULL);
    return_val_if_fail(serial_tx != NULL, NULL);
    return_val_if_fail(msg_rx != NULL, NULL);

    smp_context_init(ctx, decoder, cbs, userdata, true);

    ctx->serial_tx = serial_tx;
    ctx->msg_tx = msg_tx;
    ctx->msg_rx = msg_rx;

    return ctx;
}

/**
 * \ingroup context
 * Free a SmpContext object.
 *
 * It is safe to call this function on a statically allocated context.
 *
 * @param[in] ctx the SmpContext
 */
void smp_context_free(SmpContext *ctx)
{
    return_if_fail(ctx != NULL);

    if (ctx->statically_allocated)
        return;

    free(ctx->decoder);
    free(ctx);
}

/**
 * \ingroup context
 * Open the provided serial device and use it in the given context.
 *
 * @param[in] ctx the SmpContext
 * @param[in] device path to the serial device to use
 *
 * @return 0 on success, a SmpError otherwise.
 */
int smp_context_open(SmpContext *ctx, const char *device)
{
    int ret;

    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(device != NULL, SMP_ERROR_INVALID_PARAM);

    if (ctx->opened)
        return SMP_ERROR_BUSY;

    ret = smp_serial_device_open(&ctx->device, device);
    if (ret < 0)
        return ret;

    ctx->opened = true;
    return 0;
}

/**
 * \ingroup context
 * Close the context, releasing the attached serial device.
 *
 * @param[in] ctx the SmpContext
 */
void smp_context_close(SmpContext *ctx)
{
    return_if_fail(ctx != NULL);

    if (!ctx->opened)
        return;

    smp_serial_device_close(&ctx->device);
    ctx->opened = false;
}

/**
 * \ingroup context
 * Set serial device config. Depending on the system, it could be not
 * implemented.
 *
 * @param[in] ctx the SmpContext
 * @param[in] baudrate the baudrate
 * @param[in] parity the parity configuration
 * @param[in] flow_control 1 to enable flow control, 0 to disable
 *
 * @return 0 on success, a SmpError otherwise.
 */
int smp_context_set_serial_config(SmpContext *ctx, SmpSerialBaudrate baudrate,
        SmpSerialParity parity, int flow_control)
{
    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);

    return smp_serial_device_set_config(&ctx->device, baudrate, parity,
            flow_control);
}

/**
 * \ingroup context
 * Get the file descriptor of the opened serial device.
 *
 * @param[in] ctx the SmpContext
 *
 * @return the fd (or a handle on Win32) on success, a SmpError otherwise.
 */
intptr_t smp_context_get_fd(SmpContext *ctx)
{
    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);

    return smp_serial_device_get_fd(&ctx->device);
}

/**
 * Send a message using the specified context.
 *
 * @param[in] ctx the SmpContext
 * @param[in] msg the SmpMessage to send
 *
 * @return 0 on success, a SmpError otherwise.
 */
int smp_context_send_message(SmpContext *ctx, SmpMessage *msg)
{
    SmpBuffer *msgbuf;
    size_t msgsize;
    uint8_t *serial_buf = NULL;
    size_t serial_bufsize = 0;
    ssize_t encoded_size;
    ssize_t wbytes;
    ssize_t ret;

    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(msg != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(ctx->opened, SMP_ERROR_BAD_FD);

    /* step 1: encode the message */
    msgbuf = ctx->msg_tx;
    msgsize = smp_message_get_encoded_size(msg);

    if (msgbuf == NULL) {
        /* no user provided buffer, alloc */
        msgbuf = smp_buffer_new_allocate(msgsize);
        if (msgbuf == NULL)
            return SMP_ERROR_NO_MEM;
    } else if (msgbuf->maxsize < msgsize) {
        /* check size */
        return SMP_ERROR_OVERFLOW;
    }

    encoded_size = smp_message_encode(msg, msgbuf->data, msgbuf->maxsize);
    if (encoded_size < 0) {
        ret = encoded_size;
        goto done;
    }

    /* step 2: encode the message for the serial */
    if (ctx->serial_tx != NULL) {
        serial_buf = ctx->serial_tx->data;
        serial_bufsize = ctx->serial_tx->maxsize;
    }

    encoded_size = smp_serial_protocol_encode(msgbuf->data, encoded_size,
            &serial_buf, serial_bufsize);
    if (encoded_size < 0) {
        ret = encoded_size;
        goto done;
    }

    /* step 3: send it over the serial */
    wbytes = smp_serial_device_write(&ctx->device, serial_buf, encoded_size);
    if (wbytes < 0) {
        ret = wbytes;
        goto done;
    } else if (wbytes != encoded_size) {
        ret = SMP_ERROR_IO;
        goto done;
    }

    ret = 0;

done:
    if (ctx->msg_tx == NULL) {
        /* we have allocated buffer so free it */
        smp_buffer_free(msgbuf);
    }

    if (ctx->serial_tx == NULL) {
        /* we have allocated buffer so free it */
        free(serial_buf);
    }

    return (int) ret;
}

/**
 * \ingroup context
 * Process incoming data on the serial file descriptor.
 * Decoded frame or errors during decoding are passed to their respective
 * callbacks.
 *
 * @param[in] ctx the SmpContext
 *
 * @return 0 on success, a SmpError otherwise.
 */
int smp_context_process_fd(SmpContext *ctx)
{
    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(ctx->opened, SMP_ERROR_BAD_FD);

    while (1) {
        ssize_t rbytes;
        char c;
        uint8_t *frame;
        size_t framesize;
        int ret;

        rbytes = smp_serial_device_read(&ctx->device, &c, 1);
        if (rbytes < 0) {
            if (rbytes == SMP_ERROR_WOULD_BLOCK)
                return 0;

            return (int) rbytes;
        } else if (rbytes == 0) {
            return 0;
        }

        ret = smp_serial_protocol_decoder_process_byte(ctx->decoder, c, &frame,
                &framesize);
        if (ret < 0)
            smp_context_notify_error(ctx, ret);

        if (frame != NULL)
            smp_context_process_serial_frame(ctx, frame, framesize);
    }
}

/**
 * \ingroup context
 * Wait for an event on the serial and process it.
 *
 * @param[in] ctx the SmpContext
 * @param[in] timeout_ms a timeout in milliseconds. A negative value means no
 *                       timeout
 *
 * @return 0 on success, a SmpError otherwise.
 */
int smp_context_wait_and_process(SmpContext *ctx, int timeout_ms)
{
    int ret;

    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(ctx->opened, SMP_ERROR_BAD_FD);

    ret = smp_serial_device_wait(&ctx->device, timeout_ms);
    if (ret == 0)
        ret = smp_context_process_fd(ctx);

    return ret;
}

/**
 * \ingroup context
 * Set decoder buffer maximum capacity. This value is used as a limit when
 * reallocating memory for the decoder. If the value is lower than the current
 * capacity, the buffer won't be reallocated
 *
 * @param[in] ctx the SmpContext
 * @param[in] max the maximum buffer capacity in bytes
 *
 * @return 0 on success, a SmpError otherwise.
 */
int smp_context_set_decoder_maximum_capacity(SmpContext *ctx, size_t max)
{
    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(max > 16, SMP_ERROR_INVALID_PARAM);

    return smp_serial_protocol_decoder_set_maximum_capacity(ctx->decoder, max);
}
