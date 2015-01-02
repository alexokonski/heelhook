/* pqueue - generic priority queue, based on the algorithms found in
 *          Cormen, Leiserson, Rivest, Stein.  Also includes support for
 *          constant time random access and iteration over the queue
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

#include "../pqueue.h"
#include "../util.h"
#include "../hhmemory.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEST_ARRAY_SIZE 100000

static int test_compare(pqueue_value a, pqueue_value b)
{
    if (a.i_val < b.i_val)
    {
        return -1;
    }
    else if (a.i_val > b.i_val)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

static pqueue_spec test_spec =
{
   .sort = PQUEUE_SORT_MIN,
   .cmp = test_compare
};

static int qsort_compare(const void* a, const void* b)
{
    const int* ia = a;
    const int* ib = b;

    if (*ia < *ib)
    {
        return -1;
    }
    else if (*ia > *ib)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

static void test_with_data(int* test_data, const int test_data_size)
{
    pqueue* q = pqueue_create(&test_spec);

    pqueue_elem_ref first = NULL;
    int first_val = 0;
    pqueue_elem_ref mid = NULL;
    int mid_val = 0;
    pqueue_elem_ref last = NULL;
    int last_val = 0;
    pqueue_elem_ref random_ref = NULL;
    int rand_val = 0;

    int rand_index = 0;
    while (rand_index == 0 || rand_index == test_data_size / 2 ||
           rand_index == test_data_size - 1)
    {
        rand_index = rand() % test_data_size;
    }

    pqueue_value val;
    for (int i = 0; i < test_data_size; i++)
    {
        val.i_val = test_data[i];
        if (i == 0)
        {
            first_val = test_data[i];
            first = pqueue_insert(q, val);
        }
        else if(i == test_data_size / 2)
        {
            mid_val = test_data[i];
            mid = pqueue_insert(q, val);
        }
        else if(i == test_data_size - 1)
        {
            last_val = test_data[i];
            last = pqueue_insert(q, val);
        }
        else if(i == rand_index)
        {
            rand_val = test_data[i];
            random_ref = pqueue_insert(q, val);
        }
        else
        {
            pqueue_insert(q, val);
        }
    }

    pqueue_iterator it;
    int i;
    for (pqueue_iter_begin(q, &it), i = 0;
         pqueue_iter_is_valid(q, &it);
         pqueue_iter_next(q, &it), i++)
    {
        val = pqueue_iter_get_value(&it);
        if (val.i_val != test_data[i])
        {
            printf(
                "ITERATE VALUE MISMATCH AT %d: %d %" PRId64 "\n",
                i,
                test_data[i],
                val.i_val
            );
            exit(EXIT_FAILURE);
        }
    }

    if (i != pqueue_get_size(q))
    {
        printf(
            "ITERATE COUNT MISMATCH i: %d size: %d\n",
            i,
            pqueue_get_size(q)
        );
        exit(EXIT_FAILURE);
    }

    /* basic test of delete and insert */
    pqueue_delete(q, first);
    pqueue_delete(q, mid);
    pqueue_delete(q, last);
    pqueue_delete(q, random_ref);

    val.i_val = first_val;
    pqueue_insert(q, val);

    val.i_val = mid_val;
    pqueue_insert(q, val);

    val.i_val = last_val;
    pqueue_insert(q, val);

    val.i_val = rand_val;
    pqueue_insert(q, val);

    int* orig_data = hhmalloc(test_data_size * sizeof(int));
    memcpy(orig_data, test_data, test_data_size * sizeof(int));

    qsort(
        test_data,
        test_data_size,
        sizeof(int),
        qsort_compare
    );


    if (test_data_size != pqueue_get_size(q))
    {
        printf(
            "SIZE MISMIATCH %zu %d\n",
            hhcountof(test_data),
            pqueue_get_size(q)
        );

        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < test_data_size; i++)
    {
        pqueue_value val = pqueue_pop(q);
        if (test_data[i] != val.i_val)
        {
            printf(
                "VALUE MISMATCH AT %d: %d %" PRId64 "\n",
                i,
                test_data[i],
                val.i_val
            );
            printf("{ ");
            for (int i = 0; i < test_data_size; i++)
            {
                printf("%d ", orig_data[i]);
            }
            printf(" }\n");

            exit(EXIT_FAILURE);
        }
    }

    hhfree(orig_data);
    pqueue_destroy(q);
}

int main(int argc, char **argv)
{
    hhunused(argc);
    hhunused(argv);

    int test_array[] = {543, 0, -234, 75, 66, 325, 13, 3245};
    test_with_data(test_array, (const int)hhcountof(test_array));

    int test_array2[] = {21, 86, 65, 41, 74, 28, 6, 1, 16, 42, 2, 91, 96, 34,
        15, 63, 66, 83, 86};
    test_with_data(test_array2, (const int)hhcountof(test_array2));

    int test_array3[] = {1, 54, 30, 17, 29, 9, 19, 80, 52, 81, 93, 0, 55, 62,
        8, 73, 35, 89, 35, 87, 0, 0, 4, 26, 90, 73, 55, 30, 56, 36, 36, 10, 90,
        18, 27, 20, 27, 47, 52, 31, 80, 97, 31, 36, 59, 91, 61, 46, 32, 48 };
    test_with_data(test_array3, (const int)hhcountof(test_array3));

    srand(time(NULL));
    int random_array[TEST_ARRAY_SIZE];
    for (int i = 0; i < TEST_ARRAY_SIZE; i++)
    {
        random_array[i] = rand() % 100;
    }
    test_with_data(random_array, (const int)hhcountof(random_array));

    exit(EXIT_SUCCESS);
}
