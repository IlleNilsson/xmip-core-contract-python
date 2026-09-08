/*
 * The shim that makes a Python contract a loadable Xmip module.
 *
 * The C entrypoint, contract table and lifecycle live here, as in the C
 * contract; the judgement is forwarded to the xmip_contract module through
 * the CPython API. The interpreter is embedded in-process (owner, 2026-09-07):
 * initialised at `start` if nothing else in the process has, with
 * XMIP_PYTHON_PATH, or the current directory, put first on sys.path. Every
 * call takes the GIL and releases it, so a host may validate on any thread.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "xmip_module.h"

#include <stdlib.h>
#include <string.h>

#define XMIP_PY_MESSAGE_MAX 512

typedef struct {
    char   *descriptor;
    size_t  descriptor_len;
} Contract;

typedef struct {
    int            started;
    XmipDiagnostic diagnostic;
    char           message[XMIP_PY_MESSAGE_MAX];
    char           error[XMIP_PY_MESSAGE_MAX];
    char           implied[XMIP_PY_MESSAGE_MAX];
} State;

static XmipStr str_of(const char *text, size_t len) {
    XmipStr s;
    s.ptr = (const uint8_t *)text;
    s.len = len;
    return s;
}

static void fail(State *s, const char *what) {
    strncpy(s->error, what, XMIP_PY_MESSAGE_MAX - 1);
    s->error[XMIP_PY_MESSAGE_MAX - 1] = '\0';
}

static XmipStatus configure(void *state, XmipStr toml) { (void)state; (void)toml; return XMIP_OK; }

static XmipStatus start(void *state) {
    State *s = (State *)state;
    const char *where = getenv("XMIP_PYTHON_PATH");
    PyGILState_STATE gil;
    PyObject *sys_path, *entry;
    if (s->started) return XMIP_OK;
    if (!Py_IsInitialized()) {
        Py_InitializeEx(0);
    }
    gil = PyGILState_Ensure();
    sys_path = PySys_GetObject("path");
    entry = PyUnicode_FromString((where && *where) ? where : ".");
    if (sys_path == NULL || entry == NULL || PyList_Insert(sys_path, 0, entry) != 0) {
        Py_XDECREF(entry);
        PyErr_Clear();
        PyGILState_Release(gil);
        fail(s, "sys.path could not be extended");
        return XMIP_E_UNAVAILABLE;
    }
    Py_DECREF(entry);
    PyGILState_Release(gil);
    s->started = 1;
    return XMIP_OK;
}

static XmipStatus stop(void *state) { (void)state; return XMIP_OK; }

static XmipStatus load(void *state, XmipStr descriptor, void **out_contract) {
    Contract *contract;
    (void)state;
    if (out_contract == NULL) return XMIP_E_INVALID;
    contract = (Contract *)calloc(1, sizeof *contract);
    if (contract == NULL) return XMIP_E_CAPACITY;
    if (descriptor.len > 0) {
        contract->descriptor = (char *)malloc(descriptor.len + 1);
        if (contract->descriptor == NULL) { free(contract); return XMIP_E_CAPACITY; }
        memcpy(contract->descriptor, descriptor.ptr, descriptor.len);
        contract->descriptor[descriptor.len] = '\0';
        contract->descriptor_len = descriptor.len;
    }
    *out_contract = contract;
    return XMIP_OK;
}

static void release(void *state, void *contract) {
    Contract *c = (Contract *)contract;
    (void)state;
    if (c == NULL) return;
    free(c->descriptor);
    free(c);
}

/* Call xmip_contract.<name>(descriptor, argument). Copies a str answer into
 * `into`; returns 1 for a str, 0 for None or empty, -1 on a Python error. */
static int call_contract(State *s, const char *name, const char *descriptor, PyObject *argument,
                         char *into, size_t cap) {
    PyObject *module, *function, *answer;
    int result = -1;
    module = PyImport_ImportModule("xmip_contract");
    if (module == NULL) { PyErr_Clear(); fail(s, "xmip_contract is not importable from XMIP_PYTHON_PATH"); return -1; }
    function = PyObject_GetAttrString(module, name);
    Py_DECREF(module);
    if (function == NULL) { PyErr_Clear(); fail(s, "the contract module lacks the function"); return -1; }
    answer = PyObject_CallFunction(function, "sO", descriptor ? descriptor : "", argument);
    Py_DECREF(function);
    if (answer == NULL) { PyErr_Clear(); fail(s, "the contract raised"); return -1; }
    if (answer == Py_None) {
        result = 0;
    } else if (PyUnicode_Check(answer)) {
        Py_ssize_t len = 0;
        const char *text = PyUnicode_AsUTF8AndSize(answer, &len);
        if (text == NULL) { PyErr_Clear(); fail(s, "the answer is not text"); }
        else {
            snprintf(into, cap, "%.*s", (int)(len < (Py_ssize_t)cap - 1 ? len : (Py_ssize_t)cap - 1), text);
            result = len > 0 ? 1 : 0;
        }
    } else {
        fail(s, "the answer is neither text nor None");
    }
    Py_DECREF(answer);
    return result;
}

static XmipStatus validate(void *state, void *contract, const XmipReader *in,
                           const XmipDiagnostic **out, size_t *out_len) {
    State *s = (State *)state;
    Contract *c = (Contract *)contract;
    uint8_t *bytes = NULL;
    size_t len = 0, cap = 0;
    PyGILState_STATE gil;
    PyObject *data;
    int answered;
    if (s == NULL || c == NULL || in == NULL || in->read == NULL || out == NULL || out_len == NULL) {
        return XMIP_E_INVALID;
    }
    *out = NULL;
    *out_len = 0;
    if (!s->started) { fail(s, "validate before start"); return XMIP_E_STATE; }
    for (;;) {
        int64_t got;
        if (cap - len < 4096) {
            size_t grown = cap == 0 ? 8192 : cap * 2;
            uint8_t *bigger = (uint8_t *)realloc(bytes, grown);
            if (bigger == NULL) { free(bytes); return XMIP_E_CAPACITY; }
            bytes = bigger;
            cap = grown;
        }
        got = in->read(in->ctx, bytes + len, cap - len);
        if (got < 0) { free(bytes); return (XmipStatus)got; }
        if (got == 0) break;
        len += (size_t)got;
    }
    gil = PyGILState_Ensure();
    data = PyBytes_FromStringAndSize((const char *)bytes, (Py_ssize_t)len);
    free(bytes);
    if (data == NULL) { PyErr_Clear(); PyGILState_Release(gil); fail(s, "out of memory"); return XMIP_E_CAPACITY; }
    answered = call_contract(s, "validate", c->descriptor, data, s->message, sizeof s->message);
    Py_DECREF(data);
    PyGILState_Release(gil);
    if (answered < 0) return XMIP_E_INTERNAL;
    if (answered == 0) return XMIP_OK;
    s->diagnostic.code = XMIP_E_CONTRACT;
    s->diagnostic.message = str_of(s->message, strlen(s->message));
    s->diagnostic.location = str_of("", 0);
    s->diagnostic.offset = UINT64_MAX;
    *out = &s->diagnostic;
    *out_len = 1;
    return XMIP_E_CONTRACT;
}

static XmipStatus implies(void *state, void *contract, XmipStr key, XmipStr *out) {
    State *s = (State *)state;
    Contract *c = (Contract *)contract;
    PyGILState_STATE gil;
    PyObject *pykey;
    int answered;
    if (s == NULL || c == NULL || out == NULL) return XMIP_E_INVALID;
    if (!s->started) { fail(s, "implies before start"); return XMIP_E_STATE; }
    gil = PyGILState_Ensure();
    pykey = PyUnicode_FromStringAndSize((const char *)key.ptr, (Py_ssize_t)key.len);
    if (pykey == NULL) { PyErr_Clear(); PyGILState_Release(gil); return XMIP_E_MALFORMED; }
    answered = call_contract(s, "implies", c->descriptor, pykey, s->implied, sizeof s->implied);
    Py_DECREF(pykey);
    PyGILState_Release(gil);
    if (answered < 0) return XMIP_E_INTERNAL;
    if (answered == 0) return XMIP_E_NOT_FOUND;
    *out = str_of(s->implied, strlen(s->implied));
    return XMIP_OK;
}

static XmipStr last_error(void *state) {
    State *s = (State *)state;
    return str_of(s->error, strlen(s->error));
}

/* The interpreter outlives the module; finalising it would take every other
 * Python module in the process down with it. */
static void destroy(void *state) { free(state); }

static const XmipContractVtable VTABLE = {
    { 1u, 0u, configure, start, stop },
    load, release, validate, implies
};

XMIP_EXPORT XmipStatus xmip_create_module_v1(const XmipHost *host, XmipModule *out) {
    State *state;
    if (host == NULL || out == NULL) return XMIP_E_INVALID;
    if (host->abi_version != XMIP_ABI_VERSION) return XMIP_E_UNSUPPORTED;
    state = (State *)calloc(1, sizeof *state);
    if (state == NULL) return XMIP_E_CAPACITY;
    out->descriptor.abi_version = XMIP_ABI_VERSION;
    out->descriptor.provider = str_of("core", 4);
    out->descriptor.module = str_of("contract", 8);
    out->descriptor.standard = str_of("python", 6);
    out->descriptor.trait_major = 1u;
    out->descriptor.trait_minor = 0u;
    out->descriptor.module_major = 0u;
    out->descriptor.module_minor = 1u;
    out->descriptor.module_patch = 0u;
    out->state = state;
    out->vtable = &VTABLE;
    out->last_error = last_error;
    out->destroy = destroy;
    return XMIP_OK;
}
