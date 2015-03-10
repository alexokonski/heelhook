/* inlist - a collection of macros for doing in-place linked lists
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

/*
 * TODO:Make this prettier and more like:
 *
 * http://www.makelinux.net/ldd3/chp-11-sect-5
 */

#define INLIST_INIT_HEAD(n, next_name, prev_name) \
    do\
    {\
        if ((n) != NULL) (n)->next_name = NULL;\
        if ((n) != NULL) (n)->prev_name = NULL;\
    } while (0)

#define INLIST_INIT_LIST(list, head, tail) \
    do\
    {\
        (list)->head = NULL;\
        (list)->tail = NULL;\
    } while (0)

#define INLIST_INSERT_AFTER(n, new_node, next_name, prev_name) \
    if ((n) == NULL)\
    {\
        INLIST_INIT_HEAD(new_node, next_name, prev_name);\
        (n) = (new_node);\
    }\
    else\
    {\
        (new_node)->next_name = (n)->next_name;\
        (new_node)->prev_name = (n);\
        if ((n)->next_name != NULL) (n)->next_name->prev_name = (new_node);\
        (n)->next_name = (new_node);\
    }

#define INLIST_INSERT_BEFORE(n, new_node, next_name, prev_name) \
    if ((n) == NULL)\
    {\
        INLIST_INIT_HEAD(new_node, next_name, prev_name);\
        (n) = (new_node);\
    }\
    else\
    {\
        (new_node)->next_name = (n);\
        (new_node)->prev_name = (n)->prev_name;\
        if ((n)->prev_name != NULL) (n)->prev_name->next_name = (new_node);\
        (n)->prev_name = new_node;\
    }

#define INLIST_APPEND(list, new_node, next, prev, head, tail) \
    do\
    {\
        INLIST_INSERT_AFTER((list)->tail, new_node, next, prev);\
        if ((list)->head == NULL) (list)->head = (new_node);\
        (list)->tail = (new_node);\
    } while (0)

#define INLIST_PREPEND(list, new_node, next, prev, head, tail) \
    do\
    {\
        INLIST_INSERT_BEFORE((list)->head, new_node, next, prev);\
        if ((list)->tail == NULL) (list)->tail = (new_node);\
        (list)->head = (new_node);\
    } while (0)

#define INLIST_REMOVE(list, n, next_name, prev_name, head, tail) \
    do\
    {\
        if ((n) != NULL)\
        {\
            if ((n)->prev_name != NULL)\
            {\
                (n)->prev_name->next_name = (n)->next_name;\
            }\
            if ((n)->next_name != NULL)\
            {\
                (n)->next_name->prev_name = (n)->prev_name;\
            }\
            if ((list)->head == (n)) list->head = (n)->next_name;\
            if ((list)->tail == (n)) list->tail = (n)->prev_name;\
            (n)->next_name = NULL;\
            (n)->prev_name = NULL;\
        }\
    } while (0)

/* move an existing node to the back of a list */
#define INLIST_MOVE_BACK(list, n, next, prev, head, tail) \
    INLIST_REMOVE(list, n, next, prev, head, tail);\
    INLIST_APPEND(list, n, next, prev, head, tail);

/* safe to delete curr */
#define INLIST_FOREACH(list, type, curr, next, prev, head, tail) \
    for (type* curr = list->head,\
            *next_node = (curr == NULL ? NULL : curr->next);\
         curr != NULL;\
         curr = next_node,\
            next_node = (next_node == NULL ? NULL : next_node->next))

