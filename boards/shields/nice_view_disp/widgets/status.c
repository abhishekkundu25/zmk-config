/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#if __has_include(<zmk/events/keycode_state_changed.h>)
#include <zmk/events/keycode_state_changed.h>
#define HAVE_KEYCODE_EVENTS 1
#else
#include <zmk/events/position_state_changed.h>
#define HAVE_KEYCODE_EVENTS 0
#endif

#ifdef LV_SYMBOL_BLUETOOTH
#define BLE_CONNECTED_SYMBOL LV_SYMBOL_BLUETOOTH
#else
#define BLE_CONNECTED_SYMBOL "BT"
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static uint32_t last_keypress_render_ms;
static bool has_last_keypress_render;

#define KEYPRESS_RENDER_INTERVAL_MS 100

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

struct keypress_status_state {
#if HAVE_KEYCODE_EVENTS
    uint8_t usage_page;
    uint32_t keycode;
    uint8_t implicit_modifiers;
    uint8_t explicit_modifiers;
#else
    uint32_t position;
#endif
    bool pressed;
};

#if !HAVE_KEYCODE_EVENTS
static const char *const default_key_labels[] = {
    "TAB", "Q", "W", "E", "R", "T", "MUTE", "PP", "Y", "U", "I", "O", "P", "BSPC",
    "ESC", "A", "S", "D", "F", "G", "LALT", "RALT", "H", "J", "K", "L", ";", "'",
    "LSHFT", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "ENTER",
    "ALT", "LOWER", "LCTRL", "SPACE", "RAISE", "GUI",
};

static const char *label_for_position(uint32_t position) {
    if (position < ARRAY_SIZE(default_key_labels)) {
        return default_key_labels[position];
    }

    return NULL;
}
#endif

static bool format_key_label(char *out, size_t out_len, const struct keypress_status_state *state) {
#if HAVE_KEYCODE_EVENTS
    const uint8_t all_modifiers = state->implicit_modifiers | state->explicit_modifiers;
    const bool shifted = (all_modifiers & (BIT(1) | BIT(5))) != 0;

    if (state->usage_page == 0x07) {
        if (state->keycode >= 0x04 && state->keycode <= 0x1d) {
            char c = 'A' + (char)(state->keycode - 0x04);
            snprintf(out, out_len, "%c", c);
            return true;
        }

        switch (state->keycode) {
        case 0x1e:
            snprintf(out, out_len, "%s", shifted ? "!" : "1");
            return true;
        case 0x1f:
            snprintf(out, out_len, "%s", shifted ? "@" : "2");
            return true;
        case 0x20:
            snprintf(out, out_len, "%s", shifted ? "#" : "3");
            return true;
        case 0x21:
            snprintf(out, out_len, "%s", shifted ? "$" : "4");
            return true;
        case 0x22:
            snprintf(out, out_len, "%s", shifted ? "%" : "5");
            return true;
        case 0x23:
            snprintf(out, out_len, "%s", shifted ? "^" : "6");
            return true;
        case 0x24:
            snprintf(out, out_len, "%s", shifted ? "&" : "7");
            return true;
        case 0x25:
            snprintf(out, out_len, "%s", shifted ? "*" : "8");
            return true;
        case 0x26:
            snprintf(out, out_len, "%s", shifted ? "(" : "9");
            return true;
        case 0x27:
            snprintf(out, out_len, "%s", shifted ? ")" : "0");
            return true;
        case 0x28:
            snprintf(out, out_len, "ENTER");
            return true;
        case 0x29:
            snprintf(out, out_len, "ESC");
            return true;
        case 0x2a:
            snprintf(out, out_len, "BSPC");
            return true;
        case 0x2b:
            snprintf(out, out_len, "TAB");
            return true;
        case 0x2c:
            snprintf(out, out_len, "SPACE");
            return true;
        case 0x2d:
            snprintf(out, out_len, "%s", shifted ? "_" : "-");
            return true;
        case 0x2e:
            snprintf(out, out_len, "%s", shifted ? "+" : "=");
            return true;
        case 0x2f:
            snprintf(out, out_len, "%s", shifted ? "{" : "[");
            return true;
        case 0x30:
            snprintf(out, out_len, "%s", shifted ? "}" : "]");
            return true;
        case 0x31:
            snprintf(out, out_len, "%s", shifted ? "|" : "\\");
            return true;
        case 0x33:
            snprintf(out, out_len, "%s", shifted ? ":" : ";");
            return true;
        case 0x34:
            snprintf(out, out_len, "%s", shifted ? "\"" : "'");
            return true;
        case 0x35:
            snprintf(out, out_len, "%s", shifted ? "~" : "`");
            return true;
        case 0x36:
            snprintf(out, out_len, "%s", shifted ? "<" : ",");
            return true;
        case 0x37:
            snprintf(out, out_len, "%s", shifted ? ">" : ".");
            return true;
        case 0x38:
            snprintf(out, out_len, "%s", shifted ? "?" : "/");
            return true;
        case 0x3a:
            snprintf(out, out_len, "F1");
            return true;
        case 0x3b:
            snprintf(out, out_len, "F2");
            return true;
        case 0x3c:
            snprintf(out, out_len, "F3");
            return true;
        case 0x3d:
            snprintf(out, out_len, "F4");
            return true;
        case 0x3e:
            snprintf(out, out_len, "F5");
            return true;
        case 0x3f:
            snprintf(out, out_len, "F6");
            return true;
        case 0x40:
            snprintf(out, out_len, "F7");
            return true;
        case 0x41:
            snprintf(out, out_len, "F8");
            return true;
        case 0x42:
            snprintf(out, out_len, "F9");
            return true;
        case 0x43:
            snprintf(out, out_len, "F10");
            return true;
        case 0x44:
            snprintf(out, out_len, "F11");
            return true;
        case 0x45:
            snprintf(out, out_len, "F12");
            return true;
        case 0x4a:
            snprintf(out, out_len, "HOME");
            return true;
        case 0x4b:
            snprintf(out, out_len, "PGUP");
            return true;
        case 0x4c:
            snprintf(out, out_len, "DEL");
            return true;
        case 0x4d:
            snprintf(out, out_len, "END");
            return true;
        case 0x4e:
            snprintf(out, out_len, "PGDN");
            return true;
        case 0x4f:
            snprintf(out, out_len, "RIGHT");
            return true;
        case 0x50:
            snprintf(out, out_len, "LEFT");
            return true;
        case 0x51:
            snprintf(out, out_len, "DOWN");
            return true;
        case 0x52:
            snprintf(out, out_len, "UP");
            return true;
        case 0xe0:
            snprintf(out, out_len, "LCTRL");
            return true;
        case 0xe1:
            snprintf(out, out_len, "LSHFT");
            return true;
        case 0xe2:
            snprintf(out, out_len, "LALT");
            return true;
        case 0xe3:
            snprintf(out, out_len, "LGUI");
            return true;
        case 0xe4:
            snprintf(out, out_len, "RCTRL");
            return true;
        case 0xe5:
            snprintf(out, out_len, "RSHFT");
            return true;
        case 0xe6:
            snprintf(out, out_len, "RALT");
            return true;
        case 0xe7:
            snprintf(out, out_len, "RGUI");
            return true;
        default:
            break;
        }
    } else if (state->usage_page == 0x0c) {
        switch (state->keycode) {
        case 0xe2:
            snprintf(out, out_len, "MUTE");
            return true;
        case 0xe9:
            snprintf(out, out_len, "VOL+");
            return true;
        case 0xea:
            snprintf(out, out_len, "VOL-");
            return true;
        case 0xb5:
            snprintf(out, out_len, "NEXT");
            return true;
        case 0xb6:
            snprintf(out, out_len, "PREV");
            return true;
        case 0xcd:
            snprintf(out, out_len, "PLAY");
            return true;
        default:
            snprintf(out, out_len, "MEDIA");
            return true;
        }
    }
#else
    const char *label = label_for_position(state->position);
    if (label != NULL) {
        snprintf(out, out_len, "%s", label);
        return true;
    }
#endif

    return false;
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_label_dsc_t label_dsc_key;
    init_label_dsc(&label_dsc_key, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (state->active_profile_connected) {
                strcat(output_text, BLE_CONNECTED_SYMBOL);
            } else {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Draw last pressed key
    lv_canvas_draw_rect(canvas, 0, 21, 68, 42, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, 1, 22, 66, 40, &rect_black_dsc);

    if (state->show_last_key) {
        lv_canvas_draw_text(canvas, 0, 35, 68, &label_dsc_key, state->last_key);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 9);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw circles
    int circle_offsets[5][2] = {
        {13, 13}, {55, 13}, {34, 34}, {13, 55}, {55, 55},
    };

    for (int i = 0; i < 5; i++) {
        bool selected = i == state->active_profile_index;

        lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13, 0, 360,
                           &arc_dsc);

        if (selected) {
            lv_canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 9, 0, 359,
                               &arc_dsc_filled);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        lv_canvas_draw_text(canvas, circle_offsets[i][0] - 8, circle_offsets[i][1] - 10, 16,
                            (selected ? &label_dsc_black : &label_dsc), label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw layer
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[10] = {};

        sprintf(text, "LAYER %i", state->layer_index);

        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, text);
    } else {
        lv_canvas_draw_text(canvas, 0, 5, 68, &label_dsc, state->layer_label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;

    draw_top(widget->obj, widget->cbuf, &widget->state);
    draw_middle(widget->obj, widget->cbuf2, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, widget->cbuf3, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

static void set_keypress_status(struct zmk_widget_status *widget, struct keypress_status_state state) {
    if (!format_key_label(widget->state.last_key, sizeof(widget->state.last_key), &state)) {
#if HAVE_KEYCODE_EVENTS
        snprintf(widget->state.last_key, sizeof(widget->state.last_key), "KEY");
#else
        snprintf(widget->state.last_key, sizeof(widget->state.last_key), "K%u",
                 (unsigned int)state.position);
#endif
    }
    widget->state.show_last_key = true;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void keypress_status_update_cb(struct keypress_status_state state) {
    if (!state.pressed) {
        return;
    }

    uint32_t now = k_uptime_get_32();
    if (has_last_keypress_render && (now - last_keypress_render_ms) < KEYPRESS_RENDER_INTERVAL_MS) {
        return;
    }
    last_keypress_render_ms = now;
    has_last_keypress_render = true;

    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_keypress_status(widget, state); }
}

static struct keypress_status_state keypress_status_get_state(const zmk_event_t *eh) {
#if HAVE_KEYCODE_EVENTS
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    return (struct keypress_status_state){
        .usage_page = (ev != NULL) ? ev->usage_page : 0,
        .keycode = (ev != NULL) ? ev->keycode : 0,
        .implicit_modifiers = (ev != NULL) ? ev->implicit_modifiers : 0,
        .explicit_modifiers = (ev != NULL) ? ev->explicit_modifiers : 0,
        .pressed = (ev != NULL) ? ev->state : false,
    };
#else
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    return (struct keypress_status_state){
        .position = (ev != NULL) ? ev->position : 0,
        .pressed = (ev != NULL) ? ev->state : false,
    };
#endif
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_keypress_status, struct keypress_status_state,
                            keypress_status_update_cb, keypress_status_get_state)
#if HAVE_KEYCODE_EVENTS
ZMK_SUBSCRIPTION(widget_keypress_status, zmk_keycode_state_changed);
#else
ZMK_SUBSCRIPTION(widget_keypress_status, zmk_position_state_changed);
#endif

#ifdef CONFIG_NICE_VIEW_DISP_ROTATE_180 // sets positions for default and flipped canvases
int top_pos = 0;
int middle_pos = 68;
int bottom_pos = 136;
#else
int top_pos = 92;
int middle_pos = 24;
int bottom_pos = -44;
#endif

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_LEFT, top_pos, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, middle_pos, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, bottom_pos, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
    widget_keypress_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
