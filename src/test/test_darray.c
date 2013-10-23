/* test_darray - Test the most basic functionality of the darray module
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

#include "../darray.h"
#include "../util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define EXIT_IF_FAIL(cond, test, file, line)\
    if (!(cond))\
    {\
        test_failed_exit(test, file, line);\
    }

static void test_failed_exit(const char* test, const char* file, int line)
{
    printf("%s failed: %s, line %d\n", test, file, line);
    exit(1);
}

static void test_len(
    darray* array,
    size_t len,
    const char* test,
    const char* file,
    int line
)
{
    EXIT_IF_FAIL(darray_get_len(array) == len, test, file, line);
}

static void test_size_reserved(
    darray* array,
    int size_reserved,
    const char* test,
    const char* file,
    int line
)
{
    EXIT_IF_FAIL(darray_get_size_reserved(array) == size_reserved,
            test, file, line);
}

static void test_data(
    darray* array,
    void* data,
    size_t data_len,
    const char* test,
    const char* file,
    int line
)
{
    int result = memcmp(darray_get_data(array), data, data_len);
    EXIT_IF_FAIL(result == 0, test, file, line);
}

int main(void)
{
    #define END_ARGS cur_test, __FILE__, __LINE__

    /* 0 size array */
    const char* cur_test = "0 size array";
    darray* array = darray_create(sizeof(char), 0);
    test_len(array, 0, END_ARGS);
    test_size_reserved(array, 0, END_ARGS);
    if (darray_get_data(array) != NULL)
        test_failed_exit(cur_test, __FILE__, __LINE__);
    darray_destroy(array);

    /* create with size >1 */
    cur_test = "create";
    int size = 11;
    array = darray_create(sizeof(int), size);
    test_len(array, 0, END_ARGS);
    test_size_reserved(array, size, END_ARGS);

    /* append */
    cur_test = "append";
    int arr[] = {5, 4, 3, 33};
    int arr_size = hhcountof(arr);

    int arr2[] = {15, 255, 1023, 2047, 4095};
    int arr_size2 = hhcountof(arr2);

    int arr_both[] = {5, 4, 3, 33, 15, 255, 1023, 2047, 4095};
    int arr_both_size = hhcountof(arr_both);

    darray_append(&array, arr, arr_size);
    test_data(
        array,
        arr,
        arr_size * sizeof(int),
        END_ARGS
    );
    darray_destroy(array);

    /* create_data */
    cur_test = "create_data";
    array = darray_create_data(arr, sizeof(int), arr_size, arr_size);
    test_len(array, arr_size, END_ARGS);
    test_size_reserved(array, arr_size, END_ARGS);
    test_data(array, arr, arr_size * sizeof(int), END_ARGS);

    /* append when it resizes array */
    cur_test = "append_resize";
    darray_append(&array, arr2, arr_size2);
    test_data(array, arr_both, arr_both_size * sizeof(int), END_ARGS);
    test_len(array, arr_both_size, END_ARGS);
    test_size_reserved(array, 12, END_ARGS);
    darray_destroy(array);

    exit(0);
}
