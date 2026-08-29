#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct KittyProvider KittyProvider;
typedef struct { uint32_t columns, rows, history, cursor_x, cursor_y, modes, cursor_shape, cursor_blinking; uint64_t suppressed_replies; } KittyProviderSnapshot;
typedef struct { uint32_t fg, bg; uint16_t attrs; uint8_t wide, spacer, wrapline; } KittyProviderCell;
typedef struct {
    uint32_t foreground, background, cursor, palette[256];
    uint8_t has_foreground, has_background, has_cursor, has_palette[256];
} KittyProviderThemeOverrides;

int kitty_provider_runtime_init(const char *sdk_root);
KittyProvider *kitty_provider_new(uint16_t columns, uint16_t rows, uint32_t scrollback);
void kitty_provider_free(KittyProvider *provider);
int kitty_provider_feed(KittyProvider *provider, const uint8_t *data, size_t length);
int kitty_provider_resize(KittyProvider *provider, uint16_t columns, uint16_t rows);
int kitty_provider_snapshot(KittyProvider *provider, KittyProviderSnapshot *snapshot);
int kitty_provider_theme_overrides(KittyProvider *provider, KittyProviderThemeOverrides *overrides);
int kitty_provider_pointer(KittyProvider *provider, int32_t column, int32_t row, int32_t button, int32_t action, int32_t modifiers, uint8_t *output, size_t capacity, size_t *required);
int kitty_provider_cell(KittyProvider *provider, int32_t row, uint16_t column, KittyProviderCell *cell, uint32_t *codepoints, size_t capacity, size_t *required);
