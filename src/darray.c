/* darray - generic, dynamically sizing array type.  You can add/remove stuff
 *          to the raw data if you want, it's on you to call the proper
 *          functions
 *
 * Copyright (c) 2013, Alex O'Konski
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of heelhook nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "darray.h"
#include "hhassert.h"
#include "hhmemory.h"
#include "util.h"

#include <string.h>

struct darray
{
    size_t len;
    size_t size_reserved;
    size_t elem_size;
    char data[];
};

/* Only allocate a max of  2 MB at a time when resizing */
#define MAX_ENSURE_SIZE (2 * 1024 * 1024)

/*
 * make a darray.  init_size_reserved is the number of elements you want
 * space for right now.
 */
darray* darray_create(size_t elem_size, size_t init_size_reserved)
{
    size_t size = sizeof(darray) + (elem_size * init_size_reserved);
    darray* array = hhmalloc(size);
    if (array == NULL) return NULL;

    array->len = 0;
    array->size_reserved = init_size_reserved;
    array->elem_size = elem_size;
    return array;
}

/*
 * make a darray and initialize it with data.
 * init_size_reserved must be >= num_elemnts
 */
darray* darray_create_data(void* data, size_t elem_size, size_t num_elements,
                           size_t init_size_reserved)
{
    int valid_size = init_size_reserved >= num_elements;
    hhassert(valid_size);
    if (!valid_size)
    {
        return NULL;
    }

    darray* array = darray_create(elem_size, init_size_reserved);
    if (array == NULL) return NULL;

    darray_append(&array, data, num_elements);
    return array;
}

/* create a new darray that's a copy of source */
darray* darray_create_copy(const darray* source)
{
    size_t data_size = (source->elem_size * source->size_reserved);
    size_t size = sizeof(darray) + data_size;
    darray* array = hhmalloc(size);
    if (array == NULL) return NULL;

    array->len = source->len;
    array->size_reserved = source->size_reserved;
    array->elem_size = source->elem_size;
    memcpy(array->data, source->data, source->len * source->elem_size);
    return array;
}

/* copy source to existing darray dest */
void darray_copy(darray** dest, const darray* source)
{
    darray* arr = *dest;
    arr->len = source->len;
    arr->size_reserved = source->size_reserved;
    arr->elem_size = source->elem_size;
    size_t data_size = arr->size_reserved * arr->elem_size;
    arr = hhrealloc(arr, data_size + sizeof(darray));
    memmove(arr->data, source->data, source->len * source->elem_size);
    *dest = arr;
}

/* free/destroy a darray */
void darray_destroy(darray* array)
{
    hhfree(array);
}

/* get the raw data for the darray. use at your own risk */
void* darray_get_data(darray* array)
{
    if (array->size_reserved == 0) return NULL;
    return array->data;
}

/* get the current length of the darray */
size_t darray_get_len(const darray* array)
{
    return array->len;
}

/* get the number of additional elements available*/
size_t darray_get_size_reserved(const darray* array)
{
    return array->size_reserved;
}

/* clear out the darray - set the len to 0 */
void darray_clear(darray* array)
{
    array->len = 0;
}

/*
 * make the darray equal to the range [start, end).  if end is -1,
 * slice to the end of the darrray
 */
void darray_slice(darray* array, size_t start, ssize_t end)
{
    hhassert(end < 0 || (size_t)end <= array->len);
    hhassert(end < 0 || (size_t)end >= start);

    /* update array len */
    size_t uend = (end < 0) ? array->len : (size_t)end;
    array->len = (uend - start);

    /* copy region from [start, end) to the begining of the array */
    void* src = array->data + (start * array->elem_size);
    memmove(array->data, src, array->len * array->elem_size);
}

/*
 * remove the range [start, end) from the darray. if end is -1, remove
 * to the end of the darray
 */
void darray_remove(darray* array, size_t start, ssize_t end)
{
    hhassert(end < 0 || (size_t)end <= array->len);
    hhassert(end < 0 || (size_t)end >= start);

    size_t uend = (end < 0) ? array->len : (size_t)end;

    /* update array len */
    array->len -= (uend - start);

    /* copy [end, array_len) over [start, end)*/
    void* dest = array->data + (start * array->elem_size);
    void* src = array->data + (uend * array->elem_size);
    size_t move_len = (array->len - start) * array->elem_size;
    memmove(dest, src, move_len);
}

/*
 * Trim the reserved space down to num_elems or the result of darray_get_len,
 * whicever is larger
 */
void* darray_trim_reserved(darray** array, size_t min_elems_resesrved)
{
    darray* arr = *array;
    size_t new_reserved = hhmax(min_elems_resesrved, darray_get_len(*array));
    size_t new_size =
        (arr->elem_size * (new_reserved)) + sizeof(darray);

    arr = hhrealloc(arr, new_size);
    (*array) = arr;
    if (arr == NULL) return NULL;

    arr->size_reserved = new_reserved;

    return arr->data;
}

/*
 * ensure the darray has room for this many additional elements. usage:
 *
 * darray_ensure(&my_array, 10);
 *
 * returns the new data pointer for array (darray_get_data)
 */
void* darray_ensure(darray** array, size_t num_elems)
{
    darray* arr = *array;
    size_t reserved = arr->size_reserved;
    size_t len = arr->len;

    if ((reserved - len) < num_elems)
    {
        size_t num_extra = (len + num_elems) - reserved;
        size_t max_reserved_elems = reserved * 2;

        /* cap the amount of memory we'll use */
        if (MAX_ENSURE_SIZE < (max_reserved_elems * arr->elem_size))
        {
            max_reserved_elems = MAX_ENSURE_SIZE / arr->elem_size;
        }

        size_t num_new_elems = hhmax(num_extra, max_reserved_elems);
        size_t new_size =
            (arr->elem_size * (reserved + num_new_elems)) + sizeof(darray);

        arr = hhrealloc(arr, new_size);
        (*array) = arr;
        if (arr == NULL) return NULL;

        arr->size_reserved += num_new_elems;
    }

    return arr->data;
}

/* add to the length of the darray - just arithmetic, doesn't move memory */
void darray_add_len(darray* array, size_t num_elems)
{
    hhassert((array->len + num_elems) <= array->size_reserved);
    array->len += num_elems;
}

/*
 * subtract from the length of the darray - just arithmetic, doesn't move
 * memory
 */
void darray_sub_len(darray* array, size_t num_elems)
{
    hhassert(array->len >= num_elems);
    array->len -= num_elems;
}

/*
 * append some elements to the array, expanding if needed. usage:
 *
 * darray_append(&my_array, my_data, 10);
 *
 * returns the new data pointer for array (darray_get_data)
 */
void* darray_append(darray** array, const void* data, size_t num_elems)
{
    void* new_data = darray_ensure(array, num_elems);
    darray* arr = *array;
    if(arr == NULL)
    {
        return NULL;
    }

    void* dest = arr->data + (arr->len * arr->elem_size);
    size_t data_size = num_elems * arr->elem_size;
    memmove(dest, data, data_size);
    arr->len += num_elems;
    return new_data;
}

/* get element by index */
void* darray_get_elem_addr(const darray* array, size_t index)
{
    hhassert(array->len > 0);
    return (void*)(array->data + (array->elem_size * index));
}

/* return the last element of the darray */
void* darray_get_last_addr(const darray* array)
{
    return darray_get_elem_addr(array, array->len - 1);
}

