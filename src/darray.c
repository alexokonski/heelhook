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
    int len;
    int size_reserved;
    size_t elem_size;
    char data[];
};

/* 
 * make a darray.  init_size_reserved is the number of elements you want 
 * space for right now. 
 */
darray* darray_create(size_t elem_size, int init_size_reserved)
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
darray* darray_create_data(
    void* data, 
    size_t elem_size, 
    int num_elements,
    int init_size_reserved
)
{
    int valid_size = init_size_reserved >= num_elements;
    hhassert(valid_size);
    if (!valid_size)
    {
        return NULL;
    }

    darray* array = darray_create(elem_size, init_size_reserved);
    if (array == NULL) return NULL;

    array = darray_append(array, data, num_elements);
    return array;
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
int darray_get_len(darray* array)
{
    return array->len;
}

/* get the number of additional elements available*/
int darray_get_size_reserved(darray* array)
{
    return array->size_reserved;
}

/* 
 * ensure the darray has room for this many additional elements
 * returns a new address for the darray, assign the 'array' paramter to it:
 *
 * my_array = darray_ensure(my_array, 10);
 */ 

darray* darray_ensure(darray* array, int num_elems)
{
    int reserved = array->size_reserved;
    int len = array->len;
    darray* new_array = array;

    if ((reserved - len) < num_elems)
    {
        int num_extra = (len + num_elems) - reserved;
        int num_new_elems = hhmax(num_extra, reserved * 2);
        int new_size = 
            (array->elem_size * (reserved + num_new_elems)) + sizeof(darray);

        new_array = hhrealloc(array, new_size); 
        if (new_array == NULL) return NULL;

        new_array->size_reserved += num_elems;
    }

    return new_array;
}

/* add to the length of the darray - just arithmetic, doesn't move memory */
void darray_add_len(darray* array, int num_elems)
{
    hhassert((array->len + num_elems) <= array->size_reserved);
    array->len += num_elems;
}

/* 
 * append some elements to the array, expanding if needed 
 * returns a new address for the darray, assign the 'array' paramter to it:
 *
 * my_array = darray_append(my_array, my_data, 10);
 */ 
darray* darray_append(darray* array, void* data, int num_elems)
{
    darray* new_array;
    if((new_array = darray_ensure(array, num_elems)) == NULL)
    {
        return NULL;
    }

    void* dest = new_array->data + (new_array->len * new_array->elem_size);
    size_t data_size = num_elems * new_array->elem_size;
    memcpy(dest, data, data_size);
    new_array->len += num_elems;

    return array;
}

