/* pqueue - generic priority queue, based on the algorithms found in
 *          Cormen, Leiserson, Rivest, Stein.
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

#include <inttypes.h>
#include "util.h"

typedef struct pqueue pqueue;
typedef struct pqueue_elem pqueue_elem;
typedef pqueue_elem* pqueue_elem_ref;

typedef struct
{
    pqueue_elem* current;
    pqueue_elem* next;
} pqueue_iterator;

typedef enum
{
    PQUEUE_SORT_MIN,
    PQUEUE_SORT_MAX
} pqueue_sort;

typedef union
{
    int64_t i_val;
    void* p_val;
} pqueue_value;

typedef int (pqueue_cmp)(pqueue_value a, pqueue_value b);

typedef struct
{
    pqueue_sort sort;
    pqueue_cmp* cmp; 
} pqueue_spec;

/*
 * create a pqueue
 */
pqueue* pqueue_create(pqueue_spec* spec);

/*
 * destroy a pqueue. does not touch value data passed in to insert.
 */
void pqueue_destroy(pqueue* q);

/*
 * insert a value into the pqueue. you will get a pqueue_elem_ref back
 * it you want to reference this element in later functions
 */
pqueue_elem_ref pqueue_insert(pqueue* q, pqueue_value data);

/*
 * get the value using the pqueue_elem_ref given to you by pqueue_insert
 */
pqueue_value pqueue_get_elem_data(pqueue* q, pqueue_elem_ref ref);

/*
 * Almost the functional equivalent of doing:
 *      pqueue_delete(q, ref);
 *      pqueue_insert(q, data_for_ref);
 *
 * But obviously preserves the existing pqueue_elem_ref, and is done slightly
 * more efficiently
 *
 */
void pqueue_update_element(pqueue* q, pqueue_elem_ref ref);

/*
 * get current size of the pqueue
 */
int pqueue_get_size(pqueue* q);

/*
 * peek at the element at the top (min or max depending on sort) of the pqueue
 */
pqueue_value pqueue_peek(pqueue* q);

/*
 * remove the top element and return it
 */
pqueue_value pqueue_pop(pqueue* q);

/*
 * delete an element from the pqueue.
 */
void pqueue_delete(pqueue* q, pqueue_elem_ref ref);


/*
 * Iterator interface. It is safe to delete the node that the iterator is 
 * currently pointing to, but no others. The iteration is in order
 * inserted.
 *
 * Canonical usage:
 * 
 * pqueue_iterator it;
 * for (pqueue_iter_begin(q, &it);
 *      pqueue_iter_is_valid(q, &it);
 *      pqueue_iter_next(q, &it))
 * {
 *     can do:
 * 
 *     pqueue_delete(q, pqueue_get_ref(q, &it))
 *
 *     safely
 * }
 *
 */

/*
 * Iterator interface - get first value.  Will be in order inserted from
 * first to last.
 */
void pqueue_iter_begin(pqueue* q, pqueue_iterator* it);

/*
 * Iterator interface - get the next value. Check with pqueue_iter_is_valid
 * before using
 */
void pqueue_iter_next(pqueue* q, pqueue_iterator* it);

/*
 * Iterator iterface - is the iterator valid 
 * (used to check if you're at the end)
 */
bool pqueue_iter_is_valid(pqueue* q, pqueue_iterator* it);

/*
 * Iterator interface - get the actual data for your iterator
 */
pqueue_value pqueue_iter_get_value(pqueue_iterator* it);

/*
 * Iterator interface - get the ref of the pqueue iterator element
 */
pqueue_elem_ref pqueue_iter_get_ref(pqueue* q, pqueue_iterator* it);

