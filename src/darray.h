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

#ifndef __DARRAY_H_
#define __DARRAY_H_

#include <stdlib.h>
#include <unistd.h>

typedef struct darray darray;

/*
 * make a darray.  init_size_reserved is the number of elements you want
 * space for right now.
 */
darray* darray_create(size_t elem_size, size_t init_size_reserved);

/*
 * make a darray and initialize it with data.
 * init_size_reserved must be >= num_elemnts
 */
darray* darray_create_data(void* data, size_t elem_size, size_t num_elements,
                           size_t init_size_reserved);

/* create a new darray that's a copy of source */
darray* darray_create_copy(const darray* source);

/* copy source to existing darray dest */
void darray_copy(darray** dest, const darray* source);

/* free/destroy a darray */
void darray_destroy(darray* array);

/* get the raw data for the darray. use at your own risk */
void* darray_get_data(darray* array);

/* get the current length of the darray */
size_t darray_get_len(const darray* array);

/* get the number of additional elements available*/
size_t darray_get_size_reserved(const darray* array);

/* clear out the darray - set the len to 0 */
void darray_clear(darray* array);

/*
 * make the darray equal to the range [start, end). if end is -1,
 * slice to the end of the darray
 */
void darray_slice(darray* array, size_t start, ssize_t end);

/*
 * remove the range [start, end) from the darray. if end is -1, remove
 * to the end of the darray
 */
void darray_remove(darray* array, size_t start, ssize_t end);

/*
 * Trim the reserved space down to min_elems_reserved or the result of
 * darray_get_len, whicever is larger
 *
 * returns the new data pointer for the array (darray_get_data)
 */
void* darray_trim_reserved(darray** array, size_t min_elems_reserved);

/*
 * ensure the darray has room for this many additional elements. usage:
 *
 * darray_ensure(&my_array, 10);
 *
 * returns the new data pointer for array (darray_get_data)
 */
void* darray_ensure(darray** array, size_t num_elems);

/* add to the length of the darray - just arithmetic, doesn't move memory */
void darray_add_len(darray* array, size_t num_elems);

/*
 * subtract from the length of the darray - just arithmetic, doesn't move
 * memory
 */
void darray_sub_len(darray* array, size_t num_elems);

/*
 * append some elements to the array, expanding if needed. usage:
 *
 * darray_append(&my_array, my_data, 10);
 *
 * returns the new data pointer for array (darray_get_data)
 */
void* darray_append(darray** array, const void* data, size_t num_elems);

/* get element address by index */
void* darray_get_elem_addr(const darray* array, size_t index);

/* return the last element address of the darray */
void* darray_get_last_addr(const darray* array);

#endif /* __DARRAY_H_ */

