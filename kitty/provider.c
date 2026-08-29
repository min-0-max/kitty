#define _POSIX_C_SOURCE 200809L
#include "provider.h"
#include <Python.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef int (*FeedScreen)(PyObject *, const uint8_t *, size_t);
struct KittyProvider { PyObject *screen; PyObject *encode_pointer; FeedScreen feed; void *module_handle; };
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
    PyObject *module_file = module ? PyObject_GetAttrString(module, "__file__") : NULL;
    PyObject *encode_pointer = module ? PyObject_GetAttrString(module, "_provider_encode_mouse") : NULL;
    const char *module_path = module_file ? PyUnicode_AsUTF8(module_file) : NULL;
    void *module_handle = module_path ? dlopen(module_path, RTLD_NOW | RTLD_LOCAL) : NULL;
    Py_XDECREF(module_file); Py_XDECREF(type); Py_XDECREF(module);
    if (!screen || !encode_pointer || !PyCallable_Check(encode_pointer)) { Py_XDECREF(encode_pointer); Py_XDECREF(screen); failed(); PyGILState_Release(gil); return NULL; }
    KittyProvider *provider = calloc(1, sizeof(*provider));
    if (!provider) { if (module_handle) dlclose(module_handle); Py_DECREF(encode_pointer); Py_DECREF(screen); PyGILState_Release(gil); return NULL; }
    provider->screen = screen;
    provider->encode_pointer = encode_pointer;
    provider->module_handle = module_handle;
    void *feed_symbol = module_handle ? dlsym(module_handle, "kitty_provider_screen_feed") : NULL;
    _Static_assert(sizeof(provider->feed) == sizeof(feed_symbol), "dlsym pointer size");
    if (feed_symbol) memcpy(&provider->feed, &feed_symbol, sizeof(provider->feed));
    if (!provider->feed) { if (module_handle) dlclose(module_handle); Py_DECREF(encode_pointer); Py_DECREF(screen); free(provider); PyGILState_Release(gil); return NULL; }
    PyGILState_Release(gil);
    return provider;
}

void kitty_provider_free(KittyProvider *provider) {
    if (provider) {
        PyGILState_STATE gil = PyGILState_Ensure();
        Py_DECREF(provider->encode_pointer);
        Py_DECREF(provider->screen);
        if (provider->module_handle) dlclose(provider->module_handle);
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
    int ok = value && PyArg_ParseTuple(value, "IIIIIIIIK", &snapshot->columns, &snapshot->rows, &snapshot->history, &snapshot->cursor_x, &snapshot->cursor_y, &snapshot->modes, &snapshot->cursor_shape, &snapshot->cursor_blinking, &suppressed);
    snapshot->suppressed_replies = suppressed;
    Py_XDECREF(value); int answer = ok ? 0 : failed();
    PyGILState_Release(gil); return answer;
}

int kitty_provider_theme_overrides(KittyProvider *provider, KittyProviderThemeOverrides *overrides) {
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *value = PyObject_CallMethod(provider->screen, "provider_theme_overrides", NULL);
    PyObject *foreground = NULL, *background = NULL, *cursor = NULL, *palette = NULL;
    int ok = value && PyArg_ParseTuple(value, "OOOO", &foreground, &background, &cursor, &palette) && PyTuple_Check(palette) && PyTuple_GET_SIZE(palette) == 256;
    memset(overrides, 0, sizeof(*overrides));
    PyObject *dynamic[] = {foreground, background, cursor};
    uint8_t *presence[] = {&overrides->has_foreground, &overrides->has_background, &overrides->has_cursor};
    uint32_t *colors[] = {&overrides->foreground, &overrides->background, &overrides->cursor};
    for (size_t i = 0; ok && i < 3; i++) {
        if (dynamic[i] != Py_None) {
            unsigned long color = PyLong_AsUnsignedLong(dynamic[i]);
            if (PyErr_Occurred()) ok = 0;
            else {
                *presence[i] = 1;
                *colors[i] = (uint32_t)color;
            }
        }
    }
    for (size_t i = 0; ok && i < 256; i++) {
        PyObject *item = PyTuple_GET_ITEM(palette, i);
        if (item != Py_None) {
            unsigned long color = PyLong_AsUnsignedLong(item);
            if (PyErr_Occurred()) ok = 0;
            else {
                overrides->has_palette[i] = 1;
                overrides->palette[i] = (uint32_t)color;
            }
        }
    }
    Py_XDECREF(value);
    int answer = ok ? 0 : failed();
    PyGILState_Release(gil);
    return answer;
}

int kitty_provider_pointer(KittyProvider *provider, int32_t column, int32_t row, int32_t button, int32_t action, int32_t modifiers, uint8_t *output, size_t capacity, size_t *required) {
    if (!provider || !required) return -1;
    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *value = PyObject_CallFunction(provider->encode_pointer, "Oiiiii", provider->screen, column, row, button, action, modifiers);
    if (!value || !PyBytes_Check(value)) { Py_XDECREF(value); int answer = failed(); PyGILState_Release(gil); return answer; }
    const Py_ssize_t length = PyBytes_GET_SIZE(value);
    *required = (size_t)length;
    if (capacity < (size_t)length) { Py_DECREF(value); PyGILState_Release(gil); return 1; }
    if (length > 0 && output) memcpy(output, PyBytes_AS_STRING(value), (size_t)length);
    Py_DECREF(value); PyGILState_Release(gil); return 0;
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
