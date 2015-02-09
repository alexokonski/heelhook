#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <signal.h>

#include "structmember.h"
#include <stddef.h>
#include "docstrings.h"

/* Core heelhook server library include */
#include "server.h"
#include "hhlog.h"

#define MOD_HEELHOOK "_heelhook"
#define HEELHOOK_VERSION "0.0" /* trololo */

static PyObject* g_HeelhookError = NULL;
static PyThreadState* g_threadState = NULL;
static int g_stop = 0;

typedef struct hh_ServerObj hh_ServerObj;

static bool
on_connect(server_conn* conn, int* subprotocol_out, int* extensions_out,
           hh_ServerObj* server_obj);
static void on_open(server_conn* conn, hh_ServerObj* server_obj);
static void on_message(server_conn* conn, endpoint_msg* msg,
                       hh_ServerObj* server_obj);
static void on_ping(server_conn* conn, char* payload, int payload_len,
                       hh_ServerObj* server_obj);
static void on_pong(server_conn* conn, char* payload, int payload_len,
                       hh_ServerObj* server_obj);
static void on_close(server_conn* conn, int code, const char* reason,
                     int reason_len, hh_ServerObj* server_obj);
static bool should_stop(server* serv, void* userdata);

static server_callbacks g_callbacks =
{
    .on_connect = (server_on_connect*)on_connect,
    .on_open = (server_on_open*)on_open,
    .on_message = (server_on_message*)on_message,
    .on_ping = (server_on_ping*)on_ping,
    .on_pong = (server_on_pong*)on_pong,
    .on_close = (server_on_close*)on_close,
    .should_stop = (server_should_stop*)should_stop
};

static hhlog_options g_log_options =
{
    .loglevel = HHLOG_LEVEL_INFO,
    .syslogident = NULL,
    .logfilepath = NULL,
    .log_to_stdout = true,
    .log_location = true
};

struct hh_ServerObj
{
    PyObject_HEAD
    config_server_options options;
    server* serv;
    PyObject* conn_class; /* subtype of ServerConn */
    /* PyObject* connections; PySet of ServerConn instances */
    struct sigaction old_term_act;
    struct sigaction old_int_act;
};

typedef struct
{
    PyObject_HEAD
    server_conn* conn;
} hh_ServerConnObj;

static void install_sighandler(hh_ServerObj* obj);
static void uninstall_sighandler(hh_ServerObj* obj);

static void Server_dealloc(hh_ServerObj* self);
static int Server_init(hh_ServerObj* self, PyObject* args,
                       PyObject* kwds);
static PyObject* Server_new(PyTypeObject* type, PyObject* args, PyObject* kwds);
static PyObject* Server_listen(hh_ServerObj* self, PyObject* args);
static PyObject* Server_stop(hh_ServerObj* self, PyObject* args);

static PyMethodDef hh_ServerMethods[] =
{
    { "listen", (PyCFunction)Server_listen, METH_NOARGS, Server_listen__doc__ },
    { "stop", (PyCFunction)Server_stop, METH_NOARGS, Server_stop__doc__ },
    { NULL }  /* Sentinel */
};

static PyMemberDef hh_ServerMembers[] =
{
    { "_connection_class", T_OBJECT_EX,
        offsetof(hh_ServerObj, conn_class), 0, "" },
    /*{ "_connections", T_OBJECT_EX,
        offsetof(hh_ServerObj, connections), 0, "" },*/
    { NULL } /* Sentinel */
};

PyTypeObject hh_ServerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    MOD_HEELHOOK ".Server",       /*tp_name*/
    sizeof(hh_ServerObj), /*tp_basicsize*/
    0,                            /*tp_itemsize*/
    (destructor)Server_dealloc,   /*tp_dealloc*/
    0,                            /*tp_print*/
    0,                            /*tp_getattr*/
    0,                            /*tp_setattr*/
    0,                            /*tp_compare*/
    0,                            /*tp_repr*/
    0,                            /*tp_as_number*/
    0,                            /*tp_as_sequence*/
    0,                            /*tp_as_mapping*/
    0,                            /*tp_hash */
    0,                            /*tp_call*/
    0,                            /*tp_str*/
    0,                            /*tp_getattro*/
    0,                            /*tp_setattro*/
    0,                            /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    Server__doc__,                /*tp_doc */
    0,                            /*tp_traverse */
    0,                            /*tp_clear */
    0,                            /*tp_richcompare */
    0,                            /*tp_weaklistoffset */
    0,                            /*tp_iter */
    0,                            /*tp_iternext */
    hh_ServerMethods,       /*tp_methods */
    hh_ServerMembers,       /*tp_members */
    0,                            /*tp_getset */
    0,                            /*tp_base */
    0,                            /*tp_dict */
    0,                            /*tp_descr_get */
    0,                            /*tp_descr_set */
    0,                            /*tp_dictoffset */
    (initproc)Server_init,        /*tp_init */
    0,                            /*tp_alloc */
    Server_new,                   /*tp_new */
};

static PyObject*
ServerConn_on_connect(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_on_open(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_on_message(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_on_ping(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_on_pong(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_on_close(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_send(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
send_ping_or_pong(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs,
               int is_ping);
static PyObject*
ServerConn_send_ping(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_send_pong(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_send_close(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs);
static PyObject*
ServerConn_get_resource(hh_ServerConnObj* self, PyObject* args,
                        PyObject* kwargs);
static PyObject*
ServerConn_get_sub_protocols(hh_ServerConnObj* self, PyObject* args,
                             PyObject* kwargs);
static PyObject*
ServerConn_get_extensions(hh_ServerConnObj* self, PyObject* args,
                          PyObject* kwargs);
static PyObject*
ServerConn_get_headers(hh_ServerConnObj* self, PyObject* args,
                       PyObject* kwargs);

static PyMethodDef hh_ServerConnMethods[] =
{
    { "on_connect", (PyCFunction)ServerConn_on_connect,
        METH_VARARGS | METH_KEYWORDS, ServerConn_on_connect__doc__ },

    { "on_open", (PyCFunction)ServerConn_on_open,
        METH_VARARGS | METH_KEYWORDS, ServerConn_on_open__doc__ },

    { "on_message", (PyCFunction)ServerConn_on_message,
        METH_VARARGS | METH_KEYWORDS, ServerConn_on_message__doc__ },

    { "on_ping", (PyCFunction)ServerConn_on_ping,
        METH_VARARGS | METH_KEYWORDS, ServerConn_on_ping__doc__ },

    { "on_pong", (PyCFunction)ServerConn_on_pong,
        METH_VARARGS | METH_KEYWORDS, ServerConn_on_pong__doc__ },

    { "on_close", (PyCFunction)ServerConn_on_close,
        METH_VARARGS | METH_KEYWORDS, ServerConn_on_close__doc__ },

    { "send", (PyCFunction)ServerConn_send,
        METH_VARARGS | METH_KEYWORDS, ServerConn_send__doc__ },

    { "send_ping", (PyCFunction)ServerConn_send_ping,
        METH_VARARGS | METH_KEYWORDS, ServerConn_send_ping__doc__ },

    { "send_ping", (PyCFunction)ServerConn_send_pong,
        METH_VARARGS | METH_KEYWORDS, ServerConn_send_pong__doc__ },

    { "send_close", (PyCFunction)ServerConn_send_close,
        METH_VARARGS | METH_KEYWORDS, ServerConn_send_close__doc__ },

    { "get_resource", (PyCFunction)ServerConn_get_resource,
        METH_NOARGS, ServerConn_get_resource__doc__ },

    { "get_sub_protocols", (PyCFunction)ServerConn_get_sub_protocols,
        METH_NOARGS, ServerConn_get_sub_protocols__doc__ },

    { "get_extensions", (PyCFunction)ServerConn_get_extensions,
        METH_NOARGS, ServerConn_get_extensions__doc__ },

    { "get_headers", (PyCFunction)ServerConn_get_headers,
        METH_NOARGS, ServerConn_get_headers__doc__ },

    { NULL }  /* Sentinel */
};

static void ServerConn_dealloc(hh_ServerObj* self);

static PyObject*
ServerConn_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

PyTypeObject hh_ServerConnType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    MOD_HEELHOOK ".ServerConn",       /*tp_name*/
    sizeof(hh_ServerConnObj),         /*tp_basicsize*/
    0,                                /*tp_itemsize*/
    (destructor)ServerConn_dealloc,   /*tp_dealloc*/
    0,                                /*tp_print*/
    0,                                /*tp_getattr*/
    0,                                /*tp_setattr*/
    0,                                /*tp_compare*/
    0,                                /*tp_repr*/
    0,                                /*tp_as_number*/
    0,                                /*tp_as_sequence*/
    0,                                /*tp_as_mapping*/
    0,                                /*tp_hash */
    0,                                /*tp_call*/
    0,                                /*tp_str*/
    0,                                /*tp_getattro*/
    0,                                /*tp_setattro*/
    0,                                /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    ServerConn__doc__,                /*tp_doc */
    0,                                /*tp_traverse */
    0,                                /*tp_clear */
    0,                                /*tp_richcompare */
    0,                                /*tp_weaklistoffset */
    0,                                /*tp_iter */
    0,                                /*tp_iternext */
    hh_ServerConnMethods,             /*tp_methods */
    0,                                /*tp_members */
    0,                                /*tp_getset */
    0,                                /*tp_base */
    0,                                /*tp_dict */
    0,                                /*tp_descr_get */
    0,                                /*tp_descr_set */
    0,                                /*tp_dictoffset */
    0,                                /*tp_init */
    0,                                /*tp_alloc */
    ServerConn_new,                   /*tp_new */
};

static void Server_dealloc(hh_ServerObj* self)
{
    if (self->serv != NULL)
    {
        server_destroy(self->serv);
        self->serv = NULL;
    }

    self->ob_type->tp_free((PyObject*)self);
}

static PyObject* Server_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    hh_ServerObj* self;
    self = (hh_ServerObj*)type->tp_alloc(type, 0);
    return (PyObject*)self;
}

static int Server_init(hh_ServerObj* self, PyObject* args,
                       PyObject* kwds)
{
    if (self->serv != NULL)
    {
        server_destroy(self->serv);
        self->serv = NULL;
    }

    static char* kwlist[] =
    {
        "bindaddr", "port", "connection_class", "max_clients", "debug",
        "heartbeat_interval_ms", "heartbeat_ttl_ms", "handshake_timeout_ms",
        "init_buffer_len", "write_max_frame_size", "read_max_msg_size",
        "read_max_num_frames", "max_handshake_size",
        NULL
    };

    const char* bindaddr = "0.0.0.0";
    int port = 9001;
    PyObject* connection_class = (PyObject*)(&hh_ServerConnType);
    int max_clients = 1024;
    int debug = 0;
    int heartbeat_interval_ms = 0;
    int heartbeat_ttl_ms = 0;
    int handshake_timeout_ms = 0;
    int init_buffer_len = 4096;
    int write_max_frame_size = -1;
    int read_max_msg_size = 1*1024*1024;
    int read_max_num_frames = -1;
    int max_handshake_size = -1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|siOiiiiiiiiii", kwlist,
            &bindaddr, &port, &connection_class, &max_clients, &debug,
            &heartbeat_interval_ms, &heartbeat_ttl_ms, &handshake_timeout_ms,
            &init_buffer_len, &write_max_frame_size, &read_max_msg_size,
            &read_max_num_frames, &max_handshake_size))
    {
        return -1;
    }

    if (debug)
    {
        g_log_options.loglevel = HHLOG_LEVEL_DEBUG;
    }

    PyObject* tmp = self->conn_class;
    Py_INCREF(connection_class);
    self->conn_class = connection_class;
    Py_XDECREF(tmp);

    config_server_options opts =
    {
        .bindaddr = bindaddr,
        .port = port,
        .max_clients = max_clients,
        .heartbeat_interval_ms = heartbeat_interval_ms,
        .heartbeat_ttl_ms = heartbeat_ttl_ms,
        .handshake_timeout_ms = handshake_timeout_ms,
        .endp_settings =
        {
            .conn_settings =
            {
                .write_max_frame_size = write_max_frame_size,
                .read_max_msg_size = read_max_msg_size,
                .read_max_num_frames = read_max_num_frames,
                .max_handshake_size = max_handshake_size,
                .init_buf_len = init_buffer_len,
                .rand_func = NULL
            }
        },

    };

    self->options = opts;

    self->serv = server_create(&self->options, &g_callbacks, self);
    if (self->serv == NULL)
    {
        return -1;
    }

    return 0;
}

static PyObject* Server_listen(hh_ServerObj* self, PyObject* args)
{
    g_threadState = PyEval_SaveThread();

    if (self->serv == NULL)
    {
        PyEval_RestoreThread(g_threadState);
        PyErr_SetString(PyExc_TypeError, "Server.__init__ not called!");
        return NULL;
    }

    install_sighandler(self);
    server_result r = server_listen(self->serv);
    uninstall_sighandler(self);
    PyEval_RestoreThread(g_threadState);

    if (PyErr_Occurred())
    {
        return NULL;
    }

    switch (r)
    {
    case SERVER_RESULT_SUCCESS:
        Py_RETURN_TRUE;
    case SERVER_RESULT_FAIL:
        Py_RETURN_FALSE;
    }

    Py_RETURN_FALSE;
}

static PyObject* Server_stop(hh_ServerObj* self, PyObject* args)
{
    server_stop(self->serv);
    Py_RETURN_NONE;
}

static void ServerConn_dealloc(hh_ServerObj* self)
{
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject*
ServerConn_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    hh_ServerConnObj* self;
    self = (hh_ServerConnObj*)type->tp_alloc(type, 0);
    return (PyObject*)self;
}

static PyObject*
ServerConn_on_connect(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    Py_RETURN_TRUE;
}

static PyObject*
ServerConn_on_open(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    Py_RETURN_NONE;
}

static PyObject*
ServerConn_on_message(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    Py_RETURN_NONE;
}

static PyObject*
ServerConn_on_ping(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    ServerConn_send_pong(self, args, kwargs);
    Py_RETURN_NONE;
}

static PyObject*
ServerConn_on_pong(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    Py_RETURN_NONE;
}

static PyObject*
ServerConn_on_close(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    Py_RETURN_NONE;
}

static PyObject*
ServerConn_send(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Connection was already closed, or was"
                " not created by Server");
        return NULL;
    }

    static char* kwlist[] =
    {
        "msg", "is_text", NULL
    };

    int is_text = 1;
    char* data = NULL;
    Py_ssize_t sz = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|i", kwlist, &data, &sz,
                                     &is_text))
    {
        return NULL;
    }

    endpoint_msg msg =
    {
        .is_text = !!is_text,
        .data = data,
        .msg_len = sz
    };

    server_result r;
    r = server_conn_send_msg(self->conn, &msg);
    if (r != SERVER_RESULT_SUCCESS)
    {
        PyErr_Format(g_HeelhookError, "Server error: %d. Check log.", r);
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject*
send_ping_or_pong(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs,
               int is_ping)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Object was not created by Server");
        return NULL;
    }

    static char* kwlist[] =
    {
        "msg", NULL
    };

    char* data = NULL;
    Py_ssize_t sz = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#", kwlist, &data, &sz))
    {
        return NULL;
    }

    if (sz > 125)
    {
        PyErr_SetString(g_HeelhookError,
                        "Control frames must be <= 125 bytes");
        return NULL;
    }

    server_result r;

    if (is_ping)
    {
        r = server_conn_send_ping(self->conn, data, sz);
    }
    else
    {
        r = server_conn_send_pong(self->conn, data, sz);
    }

    if (r != SERVER_RESULT_SUCCESS)
    {
        PyErr_Format(g_HeelhookError, "Server error: %d. Check log.", r);
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject*
ServerConn_send_ping(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    return send_ping_or_pong(self, args, kwargs, 1);
}

static PyObject*
ServerConn_send_pong(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    return send_ping_or_pong(self, args, kwargs, 0);
}

static PyObject*
ServerConn_send_close(hh_ServerConnObj* self, PyObject* args, PyObject* kwargs)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Object was not created by Server");
        return NULL;
    }

    static char* kwlist[] =
    {
        "code", "reason", NULL
    };

    char* data = NULL;
    int code = -1;
    Py_ssize_t sz = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|is#", kwlist, &code,
                &data, &sz))
    {
        return NULL;
    }

    if (sz > 125)
    {
        PyErr_SetString(g_HeelhookError,
                        "Control frames must be <= 125 bytes");
        return NULL;
    }

    server_result r;
    r = server_conn_close(self->conn, (uint16_t)code, data, sz);
    if (r != SERVER_RESULT_SUCCESS)
    {
        PyErr_Format(g_HeelhookError, "Server error: %d. Check log.", r);
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject*
ServerConn_get_resource(hh_ServerConnObj* self, PyObject* args,
                        PyObject* kwargs)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Object was not created by Server");
        return NULL;
    }

    const char* resource = server_get_resource(self->conn);
    if (resource == NULL)
    {
        resource = "";
    }

    return PyString_FromString(resource);
}

static PyObject*
ServerConn_get_sub_protocols(hh_ServerConnObj* self, PyObject* args,
                           PyObject* kwargs)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Object was not created by Server");
        return NULL;
    }

    server_conn* conn = self->conn;
    unsigned num_protocols = server_get_num_client_subprotocols(conn);
    PyObject* sub_protocols = PyList_New(num_protocols);
    if (sub_protocols == NULL)
    {
        return NULL;
    }

    for (unsigned i = 0; i < num_protocols; i++)
    {
        PyObject* protocol;
        protocol = Py_BuildValue("s", server_get_client_subprotocol(conn, i));
        PyList_SET_ITEM(sub_protocols, i, protocol);
    }

    return sub_protocols;
}

static PyObject*
ServerConn_get_extensions(hh_ServerConnObj* self, PyObject* args,
                           PyObject* kwargs)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Object was not created by Server");
        return NULL;
    }

    server_conn* conn = self->conn;
    unsigned num_extensions = server_get_num_client_extensions(conn);
    PyObject* extensions = PyList_New(num_extensions);
    if (extensions == NULL)
    {
        return NULL;
    }

    PyObject* ext;
    for (unsigned i = 0; i < num_extensions; i++)
    {
        ext = PyString_FromString(server_get_client_extension(conn, i));
        if (ext == NULL)
        {
            Py_DECREF(extensions);
            return NULL;
        }
        PyList_SET_ITEM(extensions, i, ext);
    }

    return extensions;
}

static PyObject*
ServerConn_get_headers(hh_ServerConnObj* self, PyObject* args,
                       PyObject* kwargs)
{
    if (self->conn == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Object was not created by Server");
        return NULL;
    }

    PyObject* dict = PyDict_New();
    if (dict == NULL)
    {
        return NULL;
    }

    unsigned num_headers = server_get_num_client_headers(self->conn);
    const char* name;
    const darray* values;
    size_t num_values;
    PyObject* values_list;
    PyObject* value_str;
    int r;
    for (unsigned i = 0; i < num_headers; i++)
    {
        name = server_get_header_name(self->conn, i);
        values = server_get_header_values(self->conn, i);
        num_values = darray_get_len(values);
        values_list = PyList_New(num_values);
        if (values_list == NULL)
        {
            Py_DECREF(dict);
            return NULL;
        }

        for (unsigned j = 0; j < num_values; j++)
        {
            const char** value = darray_get_elem_addr(values, j);
            value_str = PyString_FromString(*value);
            if (value_str == NULL)
            {
                Py_DECREF(values_list);
                Py_DECREF(dict);
                return NULL;
            }
            PyList_SET_ITEM(values_list, j, value_str);
        }

        r = PyDict_SetItemString(dict, name, values_list);
        if (r == -1)
        {
            Py_DECREF(values_list);
            Py_DECREF(dict);
            return NULL;
        }

        /* dict now owns values_list, we don't need it anymore */
        Py_DECREF(values_list);
    }

    return dict;
}

static bool
on_connect(server_conn* conn, int* subprotocol_out, int* extensions_out,
           hh_ServerObj* server_obj)
{
    /* Acquire GIL */
    PyEval_RestoreThread(g_threadState);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
        g_threadState = PyEval_SaveThread();
        return false;
    }

    PyObject* conn_obj = NULL;
    PyObject* ret = NULL;
    PyObject* sub_proto = NULL;
    PyObject* extensions = NULL;
    PyObject* fast_exts = NULL;
    PyObject* ext_obj = NULL;
    bool result = false;

    /*
     * allocate connection object that will represent this client as long as
     * he's connected
     */
    conn_obj = PyObject_CallObject(server_obj->conn_class, NULL);
    if (conn_obj == NULL)
    {
        goto fail;
    }

    if (PyObject_SetAttrString(conn_obj, "server", (PyObject*)server_obj) == -1)
    {
        goto fail;
    }

    hhlog(HHLOG_LEVEL_DEBUG, "SETTING CONN");
    server_conn_set_userdata(conn, conn_obj);
    ((hh_ServerConnObj*)conn_obj)->conn = conn;

    /* Call 'on_connect' callback */
    ret = PyObject_CallMethod(conn_obj, "on_connect", NULL);

    /*
     * See if this client was accepted or rejected, and optionally
     * figure out which subprotocols/extensions to return to him.
     */
    if (ret == NULL)
    {
        /* Nothing here... we're in trouble */
    }
    else if (PyBool_Check(ret) || ret == Py_None)
    {
        result = (ret == Py_True) || (ret == Py_None);
    }
    else if (PyObject_Length(ret) == 2)
    {
        /*
         * Expecting something like:
         * (str, [str, str, ...])
         */
        sub_proto = PySequence_GetItem(ret, 0);
        if (sub_proto == NULL)
        {
            goto fail;
        }

        if (sub_proto != Py_None)
        {
            char* proto = PyString_AsString(sub_proto);
            if (proto == NULL)
            {
                goto fail;
            }

            unsigned num_protocols = server_get_num_client_subprotocols(conn);
            for (unsigned i = 0; i < num_protocols; i++)
            {
                if (strcmp(proto, server_get_client_subprotocol(conn, i)) == 0)
                {
                    *subprotocol_out = i;
                    break;
                }
            }
        }

        extensions = PySequence_GetItem(ret, 1);
        if (extensions == NULL)
        {
            goto fail;
        }

        if (extensions != Py_None)
        {
            fast_exts =
                PySequence_Fast(extensions,
                                "Second value must be a sequence type");
            if (fast_exts == NULL)
            {
                goto fail;
            }

            Py_ssize_t ext_len = PySequence_Length(fast_exts);
            unsigned num_extensions = server_get_num_client_extensions(conn);
            for (unsigned i = 0; i < num_extensions; i++)
            {
                const char* ext = server_get_client_extension(conn, i);
                for (unsigned j = 0; j < ext_len; j++)
                {
                    ext_obj = PySequence_Fast_GET_ITEM(fast_exts, j);

                    char* user_ext = PyString_AsString(ext_obj);
                    if (user_ext == NULL)
                    {
                        goto fail;
                    }

                    if (strcmp(ext, user_ext) == 0)
                    {
                        *extensions_out = i;
                        extensions_out++;
                        break;
                    }
                }
            }
        }
        result = true;
    }
    else
    {
        PyErr_SetString(PyExc_TypeError, "on_connect returned an invalid type");
    }

leave:
    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
    }

    Py_XDECREF(fast_exts);
    Py_XDECREF(extensions);
    Py_XDECREF(sub_proto);
    Py_XDECREF(ret);

    /* Release GIL */
    g_threadState = PyEval_SaveThread();

    return result;

fail:
    Py_XDECREF(conn_obj);
    result = false;
    goto leave;
}

static void on_open(server_conn* conn, hh_ServerObj* server_obj)
{
    /* Acquire GIL */
    PyEval_RestoreThread(g_threadState);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
        g_threadState = PyEval_SaveThread();
        return;
    }

    PyObject* ret;
    PyObject* conn_obj = server_conn_get_userdata(conn);

    /* Call on_open() callback */
    ret = PyObject_CallMethod(conn_obj, "on_open", NULL);
    Py_XDECREF(ret);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
    }

    /* Release GIL */
    g_threadState = PyEval_SaveThread();
}

static void on_message(server_conn* conn, endpoint_msg* msg,
                       hh_ServerObj* server_obj)
{
    /* Acquire GIL */
    PyEval_RestoreThread(g_threadState);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
        g_threadState = PyEval_SaveThread();
        return;
    }

    PyObject* ret;
    PyObject* is_text = msg->is_text ? Py_True : Py_False;
    PyObject* conn_obj = server_conn_get_userdata(conn);

    /* Call on_message(msg, isText) */
    ret = PyObject_CallMethod(conn_obj, "on_message", "s#O", msg->data,
                              msg->msg_len, is_text);
    Py_XDECREF(ret);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
    }

    /* Release GIL */
    g_threadState = PyEval_SaveThread();
}

static void on_ping(server_conn* conn, char* payload, int payload_len,
                       hh_ServerObj* server_obj)
{
    /* Acquire GIL */
    PyEval_RestoreThread(g_threadState);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
        g_threadState = PyEval_SaveThread();
        return;
    }

    PyObject* ret;
    PyObject* conn_obj = server_conn_get_userdata(conn);

    /* Call on_ping(msg) */
    ret = PyObject_CallMethod(conn_obj, "on_ping", "s#", payload, payload_len);
    Py_XDECREF(ret);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
    }

    /* Release GIL */
    g_threadState = PyEval_SaveThread();
}

static void on_pong(server_conn* conn, char* payload, int payload_len,
                       hh_ServerObj* server_obj)
{
    /* Acquire GIL */
    PyEval_RestoreThread(g_threadState);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
        g_threadState = PyEval_SaveThread();
        return;
    }

    PyObject* ret;
    PyObject* conn_obj = server_conn_get_userdata(conn);

    /* Call on_pong(msg) */
    ret = PyObject_CallMethod(conn_obj, "on_pong", "s#", payload, payload_len);
    Py_XDECREF(ret);

    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
    }

    /* Release GIL */
    g_threadState = PyEval_SaveThread();
}

static void on_close(server_conn* conn, int code, const char* reason,
                     int reason_len, hh_ServerObj* server_obj)
{
    /* Acquire GIL */
    PyEval_RestoreThread(g_threadState);

    PyObject* ret = NULL;
    PyObject* conn_obj = server_conn_get_userdata(conn);
    if (conn_obj == NULL)
    {
        hhlog(HHLOG_LEVEL_ERROR, "on_close NULL USERDATA");
        goto leave;
    }

    if (PyErr_Occurred() == NULL)
    {
        /* Call on_close(msg) */
        if (reason_len > 0 && code > 0)
        {
            ret = PyObject_CallMethod(conn_obj, "on_close", "is#", code, reason,
                                      reason_len);
        }
        else if (code > 0)
        {
            ret = PyObject_CallMethod(conn_obj, "on_close", "iO", code,
                                      Py_None);
        }
        else
        {
            ret = PyObject_CallMethod(conn_obj, "on_close", "OO", Py_None,
                                      Py_None);
        }
    }

    server_conn_set_userdata(conn, NULL);
    ((hh_ServerConnObj*)conn_obj)->conn = NULL;

    Py_DECREF(conn_obj);
    Py_XDECREF(ret);

leave:
    if (PyErr_Occurred())
    {
        server_stop(server_obj->serv);
    }

    /* Release GIL */
    g_threadState = PyEval_SaveThread();
}

static void signal_handler(int sig)
{
    hhunused(sig);
    g_stop = 1;
}

static bool should_stop(server* serv, void* userdata)
{
    return g_stop;
}

static void install_sighandler(hh_ServerObj* obj)
{
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = signal_handler;
    sigaction(SIGTERM, &act, &obj->old_term_act);
    sigaction(SIGINT, &act, &obj->old_int_act);
}

static void uninstall_sighandler(hh_ServerObj* obj)
{
    sigaction(SIGTERM, &obj->old_term_act, NULL);
    sigaction(SIGINT, &obj->old_int_act, NULL);
}

PyMODINIT_FUNC init_heelhook(void)
{
    PyObject *module;

    if (PyType_Ready(&hh_ServerType) < 0)
    {
        return;
    }

    if (PyType_Ready(&hh_ServerConnType) < 0)
    {
        return;
    }

    module = Py_InitModule3(MOD_HEELHOOK, NULL, "");
    if (module == NULL)
    {
        return;
    }

    hhlog_set_options(&g_log_options);

    Py_INCREF(&hh_ServerType);
    PyModule_AddObject(module, "Server", (PyObject *)&hh_ServerType);

    Py_INCREF(&hh_ServerConnType);
    PyModule_AddObject(module, "ServerConn", (PyObject *)&hh_ServerConnType);

    g_HeelhookError =
        PyErr_NewException(MOD_HEELHOOK ".HeelhookError", NULL, NULL);
    PyModule_AddObject(module, "HeelhookError", g_HeelhookError);

    /* Version */
    PyModule_AddStringConstant(module, "__version__", HEELHOOK_VERSION);
}
