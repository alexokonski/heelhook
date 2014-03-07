/* pqueue - generic priority queue, based on the algorithms found in
 *          Cormen, Leiserson, Rivest, Stein
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
#include "inlist.h"
#include "pqueue.h"

#include <stdio.h>

struct pqueue_elem
{
    int heap_index;
    pqueue_value data;
    pqueue_elem* next;
    pqueue_elem* prev;
};

struct pqueue
{
    pqueue_spec* spec;
    pqueue_elem* elem_head;
    pqueue_elem* elem_tail;
    darray* heap; /* parallel darray of type pqueue_elem* */
};

static int pq_parent(int i)
{
    return (i - 1) / 2;
}

static int pq_left(int i)
{
    return (2 * i) + 1;
}

static int pq_right(int i)
{
    return (2 * i) + 2;
}

static int pq_compare(pqueue* q, pqueue_value a, pqueue_value b)
{
    pqueue_value temp;

    /*
     * swap a and b.  the rest of the code is written
     * like 'max heap', this will turn it into a 'min heap'
     */
    if (q->spec->sort == PQUEUE_SORT_MIN)
    {
        temp = a;
        a = b;
        b = temp;
    }

    return q->spec->cmp(a, b);
}

static void heapify(pqueue* q, int i)
{
    int largest;
    pqueue_elem** heap = darray_get_data(q->heap);
    do
    {
        largest = i;
        int left = pq_left(i);
        int right = pq_right(i);
        int size = pqueue_get_size(q);

        if (left < size &&
            pq_compare(q, heap[left]->data, heap[i]->data) > 0)
        {
            largest = left;
        }

        if (right < size &&
            pq_compare(q, heap[right]->data, heap[largest]->data) > 0)
        {
            largest = right;
        }

        if (largest != i)
        {
            pqueue_elem* temp = heap[i];
            heap[i] = heap[largest];
            heap[i]->heap_index = i;
            heap[largest] = temp;
            heap[largest]->heap_index = largest;

            i = largest;
        }
        else
        {
            break;
        }
    } while (1);
}

/*
 * you've 'decreased' the 'value' of the element for PQUEUE_SORT_MIN pqueues,
 * or 'increased' the 'value' of the element for PQUEUE_SORT_MAX pqueues,
 * and now you need to re-heapify the pqueue.  The 'generic'
 * implementation of 'HEAP-INCREASE-KEY' found in LCRS
 */
static void pqueue_resort_element(pqueue* q, pqueue_elem_ref ref)
{
    hhassert(ref->heap_index >= 0);
    int i = ref->heap_index;
    pqueue_elem** heap = darray_get_data(q->heap);
    while (i > 0 &&
           pq_compare(q, heap[pq_parent(i)]->data, heap[i]->data) < 0)
    {
        pqueue_elem* temp = heap[i];
        heap[i] = heap[pq_parent(i)];
        heap[i]->heap_index = i;
        heap[pq_parent(i)] = temp;
        heap[pq_parent(i)]->heap_index = pq_parent(i);
        i = pq_parent(i);
    }
}

static void pqueue_remove_element(pqueue* q, pqueue_elem_ref ref)
{
    pqueue_elem* elem = ref;
    hhassert(elem->heap_index >= 0);
    int heap_index = elem->heap_index;

    elem->heap_index = -1;

    /* fixup heap */
    pqueue_elem** heap = darray_get_data(q->heap);
    pqueue_elem** last = darray_get_last(q->heap);

    heap[heap_index] = (*last);
    heap[heap_index]->heap_index = heap_index;

    darray_add_len(q->heap, -1);

    heapify(q, heap_index);
}

static void heap_insert(pqueue* q, pqueue_elem* elem)
{
    elem->heap_index = darray_get_len(q->heap);
    darray_append(&q->heap, &elem, 1);
    pqueue_resort_element(q, elem);
}

#if 0
static bool heap_validate(pqueue* q)
{
    pqueue_elem** heap = darray_get_data(q->heap);

    for(size_t i = 1; i < darray_get_len(q->heap); i++)
    {
        int parent = pq_parent(i);
        if (heap[parent]->heap_index != parent)
        {
            printf("INVALID HEAP_INDEX AT %d: %d\n",
                   (int)parent, heap[parent]->heap_index);
            return false;
        }

        if (pq_compare(q, heap[parent]->data, heap[i]->data) < 0)
        {
            printf("heap[%d] (%" PRId64 "), parent of heap[%d] (%" PRId64
                   ") INVALID\n", parent, heap[parent]->data.i_val,
                   (int)i, heap[i]->data.i_val);
            return false;
        }
    }

    return true;
}
#else
#define heap_validate(q)
#endif

/*
 * create a pqueue. it will grow dynamically, but use hintsize if you
 * have an idea
 */
pqueue* pqueue_create(pqueue_spec* spec)
{
    pqueue* q = NULL;

    q = hhmalloc(sizeof(*q));
    if (q == NULL) goto create_err;

    q->heap = darray_create(sizeof(pqueue_elem*), 16);
    if (q->heap == NULL) goto create_err;

    q->spec = spec;
    q->elem_head = NULL;
    q->elem_tail = NULL;

    return q;

create_err:
    if (q != NULL && q->heap != NULL)
    {
        darray_destroy(q->heap);
    }

    if (q != NULL)
    {
        hhfree(q);
    }

    return NULL;
}

/*
 * destroy a pqueue
 */
void pqueue_destroy(pqueue* q)
{
    for (size_t i = 0; i < darray_get_len(q->heap); i++)
    {
        pqueue_elem** elem = darray_get_elem(q->heap, i);
        hhfree(*elem);
    }

    darray_destroy(q->heap);

    hhfree(q);
}

/*
 * insert a value into the pqueue. you will get a pqueue_elem_ref back
 * it you want to reference this element in later functions
 */
pqueue_elem_ref pqueue_insert(pqueue* q, pqueue_value data)
{
    /* allocate and initialize new element */
    pqueue_elem* elem = hhmalloc(sizeof(*elem));
    elem->heap_index = -1; /* will be set by heap_insert */
    elem->data = data;
    elem->next = NULL;
    elem->prev = NULL;

    /* put it on the list of elements */
    INLIST_APPEND(q, elem, next, prev, elem_head, elem_tail);

    /* insert it into the heap array */
    heap_insert(q, elem);

    heap_validate(q);

    return elem;
}

/*
 * get the value using the id given to you by pqueue_insert
 */
pqueue_value pqueue_get_elem_data(pqueue* q, pqueue_elem_ref ref)
{
    hhunused(q);
    return ref->data;
}

/*
 * Almost the functional equivalent of doing:
 *      pqueue_delete(q, ref);
 *      pqueue_insert(q, data_for_ref);
 *
 * But obviously preserves the existing pqueue_elem_ref, and is done slightly
 * more efficiently
 *
 */
void pqueue_update_element(pqueue* q, pqueue_elem_ref ref)
{
    /* take element out of heap */
    pqueue_remove_element(q, ref);

    /* put it back in */
    heap_insert(q, ref);
}

/*
 * get current size of the pqueue
 */
int pqueue_get_size(pqueue* q)
{
    return darray_get_len(q->heap);
}

/*
 * peek at the element at the top (min or max depending on sort) of the pqueue
 */
pqueue_value pqueue_peek(pqueue* q)
{
    hhassert(pqueue_get_size(q) > 0);
    pqueue_elem** elem = darray_get_elem(q->heap, 0);
    return (*elem)->data;
}

/*
 * remove the top element and return it
 */
pqueue_value pqueue_pop(pqueue* q)
{
    hhassert(pqueue_get_size(q) > 0);
    pqueue_value top = pqueue_peek(q);
    pqueue_elem** elem = darray_get_elem(q->heap, 0);
    pqueue_delete(q, (*elem));

    heap_validate(q);

    return top;
}

/*
 * delete an element from the pqueue.
 */
void pqueue_delete(pqueue* q, pqueue_elem_ref ref)
{
    pqueue_elem* elem = ref;

    /* remove element from the list */
    INLIST_REMOVE(q, elem, next, prev, elem_head, elem_tail);

    /* remove element from the heap */
    pqueue_remove_element(q, ref);

    /* free memory */
    hhfree(elem);
}

/*
 * Iterator interface - get first id.  Will be in order inserted from
 * first to last.  Returns -1 if queue is empty
 */
void pqueue_iter_begin(pqueue* q, pqueue_iterator* it)
{
    it->current = q->elem_head;
    it->next = (it->current != NULL) ? it->current->next : NULL;
}

/*
 * Iterator interface - get the next id.  Will return NULL when done.
 */
void pqueue_iter_next(pqueue* q, pqueue_iterator* it)
{
    hhunused(q);
    it->current = it->next;
    it->next = (it->current != NULL) ? it->current->next : NULL;
}

bool pqueue_iter_is_valid(pqueue* q, pqueue_iterator* it)
{
    hhunused(q);
    return (it->current != NULL);
}

/*
 * Iterator interface - get the actual data from your element
 */
pqueue_value pqueue_iter_get_value(pqueue_iterator* it)
{
    return it->current->data;
}

/*
 * Iterator interface - get the ref of the pqueue iterator element
 */
pqueue_elem_ref pqueue_iter_get_ref(pqueue* q, pqueue_iterator* it)
{
    hhunused(q);
    return it->current;
}

