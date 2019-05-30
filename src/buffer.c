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
 * \defgroup buffer Buffer
 */

#define SMP_ENABLE_STATIC_API
#include "buffer.h"
#include "libsmp-private.h"

#include <stdlib.h>

SMP_STATIC_ASSERT(sizeof(SmpBuffer) == sizeof(SmpStaticBuffer));

static void smp_buffer_init(SmpBuffer *buffer, uint8_t *data, size_t maxsize,
        SmpBufferFreeFunc free_func, bool statically_allocated)
{
    buffer->data = data;
    buffer->maxsize = maxsize;
    buffer->free = free_func;
    buffer->statically_allocated = statically_allocated;
}

static void smp_buffer_free_default(SmpBuffer *buffer)
{
    free(buffer->data);
}

/* API */
SmpBuffer *smp_buffer_new_allocate(size_t size)
{
    SmpBuffer *buffer;
    uint8_t *data;

    buffer = smp_new(SmpBuffer);
    if (buffer == NULL)
        return NULL;

    data = calloc(size, 1);
    if (data == NULL) {
        free(buffer);
        return NULL;
    }

    smp_buffer_init(buffer, data, size, smp_buffer_free_default, false);
    return buffer;
}

/**
 * \ingroup buffer
 * Create a new buffer from static storage.
 *
 * @param[in] sbuffer a SmpStaticBuffer
 * @param[in] struct_size the size of sbuffer
 * @param[in] data pointer to a memory space to use in buffer
 * @param[in] maxsize the size of data
 * @param[in] free_func the free function to use to liberate data
 *
 * @return a new SmpBuffer or NULL on error.
 */
SmpBuffer *smp_buffer_new_from_static(SmpStaticBuffer *sbuffer,
        size_t struct_size, uint8_t *data, size_t maxsize,
        SmpBufferFreeFunc free_func)
{
    SmpBuffer *buffer = (SmpBuffer *) sbuffer;

    return_val_if_fail(sbuffer != NULL, NULL);
    return_val_if_fail(struct_size >= sizeof(SmpBuffer), NULL);
    return_val_if_fail(buffer != NULL, NULL);
    return_val_if_fail(data != NULL, NULL);
    return_val_if_fail(maxsize > 0, NULL);

    smp_buffer_init(buffer, data, maxsize, free_func, true);
    return buffer;
}

void smp_buffer_free(SmpBuffer *buffer)
{
    return_if_fail(buffer != NULL);

    if (buffer->free != NULL)
        buffer->free(buffer);

    if (buffer->statically_allocated)
        return;

    free(buffer);
}
