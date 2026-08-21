#include "provider.h"
#include <Python.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef int (*FeedScreen)(PyObject *, const uint8_t *, size_t);
struct KittyProvider { PyObject *screen; FeedScreen feed; };
static int initialized = 0;

static int failed(void) { if (PyErr_Occurred()) PyErr_Print(); return -1; }

int kitty_provider_runtime_init(const char *sdk_root) {
    if (initialized) return 0;
    char home[4096];
    if (snprintf(home, sizeof(home), "%s/../runtime", sdk_root) <= 0) return -1;
    setenv("PYTHONHOME", home, 1);
    Py_Initialize();
    PyObject *path = PySys_GetObject("path");
    PyObject *root = PyUnicode_FromString(sdk_root);
    if (!path || !root || PyList_Insert(path, 0, root) != 0) { Py_XDECREF(root); return failed(); }
    Py_DECREF(root);
    initialized = 1;
    PyEval_SaveThread();
    return 0;
}

KittyProvider *kitty_provider_new(uint16_t columns, uint16_t rows, uint32_t scrollback) {
    if (!initialized) return NULL;
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *module = PyImport_ImportModule("kitty.fast_data_types");
    PyObject *type = module ? PyObject_GetAttrString(module, "Screen") : NULL;
    PyObject *screen = type ? PyObject_CallFunction(type, "OIIIIIKO", Py_None, rows, columns, scrollback, 1u, 1u, 0ull, Py_None) : NULL;
    Py_XDECREF(type); Py_XDECREF(module);
    if (!screen) { failed(); PyGILState_Release(gil); return NULL; }
    KittyProvider *provider = calloc(1, sizeof(*provider));
    if (!provider) { Py_DECREF(screen); PyGILState_Release(gil); return NULL; }
    provider->screen = screen;
    provider->feed = (FeedScreen)dlsym(RTLD_DEFAULT, "kitty_provider_screen_feed");
    if (!provider->feed) { Py_DECREF(screen); free(provider); PyGILState_Release(gil); return NULL; }
    PyGILState_Release(gil);
    return provider;
}

void kitty_provider_free(KittyProvider *provider) {
    if (provider) {
        PyGILState_STATE gil = PyGILState_Ensure();
        Py_DECREF(provider->screen);
        PyGILState_Release(gil);
        free(provider);
    }
}

int kitty_provider_feed(KittyProvider *provider, const uint8_t *data, size_t length) {
    PyGILState_STATE gil = PyGILState_Ensure();
    int answer = provider->feed(provider->screen, data, length);
    if (answer != 0) answer = failed();
    PyGILState_Release(gil);
    return answer;
}

int kitty_provider_resize(KittyProvider *provider, uint16_t columns, uint16_t rows) {
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *result = PyObject_CallMethod(provider->screen, "resize", "II", rows, columns);
    Py_XDECREF(result); int answer = result ? 0 : failed();
    PyGILState_Release(gil); return answer;
}

int kitty_provider_snapshot(KittyProvider *provider, KittyProviderSnapshot *snapshot) {
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *value = PyObject_CallMethod(provider->screen, "provider_snapshot", NULL);
    unsigned long long suppressed = 0;
    int ok = value && PyArg_ParseTuple(value, "IIIIIIK", &snapshot->columns, &snapshot->rows, &snapshot->history, &snapshot->cursor_x, &snapshot->cursor_y, &snapshot->modes, &suppressed);
    snapshot->suppressed_replies = suppressed;
    Py_XDECREF(value); int answer = ok ? 0 : failed();
    PyGILState_Release(gil); return answer;
}

int kitty_provider_cell(KittyProvider *provider, int32_t row, uint16_t column, KittyProviderCell *cell, uint32_t *codepoints, size_t capacity, size_t *required) {
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *value = PyObject_CallMethod(provider->screen, "provider_cell", "iI", row, column);
    PyObject *text = NULL, *wide = NULL, *spacer = NULL, *wrap = NULL;
    if (!value || !PyArg_ParseTuple(value, "OIIHOOO", &text, &cell->fg, &cell->bg, &cell->attrs, &wide, &spacer, &wrap)) { Py_XDECREF(value); int answer = failed(); PyGILState_Release(gil); return answer; }
    cell->wide = PyObject_IsTrue(wide); cell->spacer = PyObject_IsTrue(spacer); cell->wrapline = PyObject_IsTrue(wrap);
    const Py_ssize_t length = PyUnicode_GetLength(text); *required = (size_t)length;
    if (capacity < (size_t)length) { Py_DECREF(value); PyGILState_Release(gil); return 1; }
    for (Py_ssize_t i = 0; i < length; i++) codepoints[i] = PyUnicode_ReadChar(text, i);
    Py_DECREF(value); PyGILState_Release(gil); return 0;
}
