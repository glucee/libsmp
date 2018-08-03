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

#include "context.h"
#include "libsmp-private.h"
#include <stdlib.h>

#include "serial-device.h"

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
    SmpMessage msg;
    int ret;

    ret = smp_message_init_from_buffer(&msg, frame, framesize);
    if (ret < 0) {
        smp_context_notify_error(ctx, ret);
        return;
    };

    smp_context_notify_new_message(ctx, &msg);
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

    return_val_if_fail(cbs != NULL, NULL);

    ctx = smp_new(SmpContext);
    if (ctx == NULL)
        return NULL;

    ctx->decoder = smp_serial_protocol_decoder_new(0);
    if (ctx->decoder == NULL) {
        free(ctx);
        return NULL;
    }

    ctx->cbs = *cbs;
    ctx->userdata = userdata;
    ctx->opened = false;
    return ctx;
}

/**
 * \ingroup context
 * Free a SmpContext object.
 *
 * @param[in] ctx the SmpContext
 */
void smp_context_free(SmpContext *ctx)
{
    return_if_fail(ctx != NULL);

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
    uint8_t *msgbuf;
    size_t msgsize;
    uint8_t *serial_buf = NULL;
    ssize_t encoded_size;
    ssize_t wbytes;
    int ret;

    return_val_if_fail(ctx != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(msg != NULL, SMP_ERROR_INVALID_PARAM);
    return_val_if_fail(ctx->opened, SMP_ERROR_BAD_FD);

    /* step 1: encode the message */
    msgsize = smp_message_get_encoded_size(msg);
    msgbuf = malloc(msgsize);
    if (msgbuf == NULL)
        return SMP_ERROR_NO_MEM;

    encoded_size = smp_message_encode(msg, msgbuf, msgsize);
    if (encoded_size < 0) {
        ret = encoded_size;
        goto done;
    }

    /* step 2: encode the message for the serial */
    encoded_size = smp_serial_protocol_encode(msgbuf, encoded_size, &serial_buf,
            0);
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
    free(serial_buf);
    free(msgbuf);
    return ret;
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

            return rbytes;
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