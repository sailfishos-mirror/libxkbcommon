/*
 * Copyright © 2012 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Author: Daniel Stone <daniel@fooishbar.org>
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "evdev-scancodes.h"
#include "keymap.h"
#include "test.h"
#include "utils.h"
#include "xkbcommon/xkbcommon-keysyms.h"
#include "xkbcommon/xkbcommon.h"
#include "src/keysym.h"

/* Offset between evdev keycodes (where KEY_ESCAPE is 1), and the evdev XKB
 * keycode set (where ESC is 9). */
#define EVDEV_OFFSET 8

/* S sharp
 * • U+00DF ß: lower case
 * •       SS: upper case (special mapping, not handled by us)
 * • U+1E9E ẞ: upper case, only for capitals
 */
#ifndef XKB_KEY_Ssharp
#define XKB_KEY_Ssharp (XKB_KEYSYM_UNICODE_OFFSET + 0x1E9E)
#endif

/* Reference implementation from XkbAdjustGroup in Xorg xserver */
static int32_t
group_wrap_ref(int32_t g, int32_t num_groups)
{
    assert(num_groups >= 0);
    if (num_groups == 0) {
        return 0;
    } else if (g < 0) {
        while (g < 0)
            g += num_groups;
    } else if (g >= num_groups) {
        g %= num_groups;
    }
    return g;
}

/* Function extracted from XkbWrapGroupIntoRange (current) */
static int32_t
group_wrap(int32_t g, int32_t num_groups)
{
    assert(num_groups >= 0);
    if (num_groups == 0)
        return 0;
    if (g >= 0 && g < num_groups)
        return g;
    const int32_t remainder = g % num_groups;
    return (remainder < 0) ? num_groups + remainder : remainder;
}

/* Old bogus implementation */
static int32_t
group_wrap_old(int32_t g, int32_t num_groups)
{
    assert(num_groups >= 0);
    if (num_groups == 0)
        return 0;
    if (g >= 0 && g < num_groups)
        return g;
    /* Invalid modulus arithmetic (see comment in XkbWrapGroupIntoRange) */
    const int32_t remainder = g % num_groups;
    return (g < 0) ? num_groups + remainder : remainder;
}

static bool
is_valid_group(int32_t g, int32_t num_groups)
{
    assert(num_groups >= 0);
    return (num_groups > 0 && g >= 0 && g < num_groups);
}

static void
test_group_wrap(struct xkb_context *ctx)
{
    /* Compare wrap function with reference implementation */
    for (int32_t G = 0; G <= XKB_MAX_GROUPS; G++) {
        for (int32_t g = - 3 * (G + 1); g <= 3 * (G + 1); g++) {
            /* Same as xserver */
            assert(group_wrap(g, G) == group_wrap_ref(g, G));
            /* Old implementation */
            const int32_t old = group_wrap_old(g, G);
            const int32_t new = group_wrap(g, G);
            assert((old == new) ^ (G > 0 && g < 0 && ((-g) % G == 0)));
        }
    }

    /* Check some special cases */
    assert(group_wrap(-2, 0) == 0);
    assert(group_wrap(-1, 0) == 0);
    assert(group_wrap(0, 0) == 0);
    assert(group_wrap(1, 0) == 0);
    assert(group_wrap(2, 0) == 0);

    assert(group_wrap(-2, 1) == 0);
    assert(group_wrap(-1, 1) == 0);
    assert(group_wrap(0, 1) == 0);
    assert(group_wrap(1, 1) == 0);
    assert(group_wrap(2, 1) == 0);

    assert(group_wrap(-6, 2) == 0);
    assert(group_wrap(-5, 2) == 1);
    assert(group_wrap(-4, 2) == 0);
    assert(group_wrap(-3, 2) == 1);
    assert(group_wrap(-2, 2) == 0);
    assert(group_wrap(-1, 2) == 1);
    assert(group_wrap(0, 2) == 0);
    assert(group_wrap(1, 2) == 1);
    assert(group_wrap(2, 2) == 0);
    assert(group_wrap(3, 2) == 1);
    assert(group_wrap(4, 2) == 0);
    assert(group_wrap(5, 2) == 1);
    assert(group_wrap(6, 2) == 0);

    assert(group_wrap(-7, 3) == 2);
    assert(group_wrap(-6, 3) == 0);
    assert(group_wrap(-5, 3) == 1);
    assert(group_wrap(-4, 3) == 2);
    assert(group_wrap(-3, 3) == 0);
    assert(group_wrap(-2, 3) == 1);
    assert(group_wrap(-1, 3) == 2);
    assert(group_wrap(0, 3) == 0);
    assert(group_wrap(1, 3) == 1);
    assert(group_wrap(2, 3) == 2);
    assert(group_wrap(3, 3) == 0);
    assert(group_wrap(4, 3) == 1);
    assert(group_wrap(5, 3) == 2);
    assert(group_wrap(6, 3) == 0);
    assert(group_wrap(7, 3) == 1);

    assert(group_wrap(-9, 4) == 3);
    assert(group_wrap(-8, 4) == 0);
    assert(group_wrap(-7, 4) == 1);
    assert(group_wrap(-6, 4) == 2);
    assert(group_wrap(-5, 4) == 3);
    assert(group_wrap(-4, 4) == 0);
    assert(group_wrap(-3, 4) == 1);
    assert(group_wrap(-2, 4) == 2);
    assert(group_wrap(-1, 4) == 3);
    assert(group_wrap(0, 4) == 0);
    assert(group_wrap(1, 4) == 1);
    assert(group_wrap(2, 4) == 2);
    assert(group_wrap(3, 4) == 3);
    assert(group_wrap(4, 4) == 0);
    assert(group_wrap(5, 4) == 1);
    assert(group_wrap(6, 4) == 2);
    assert(group_wrap(7, 4) == 3);
    assert(group_wrap(8, 4) == 0);
    assert(group_wrap(9, 4) == 1);

    /* Check state group computation */
    const char* keymaps[] = {
        /* 0 group */
        "default xkb_keymap {\n"
        "    xkb_keycodes { <> = 1; };\n"
        "    xkb_types { type \"ONE_LEVEL\" { map[none] = 1; }; };\n"
        "};",
        /* 1 group */
        "default xkb_keymap {\n"
        "    xkb_keycodes { <> = 1; };\n"
        "    xkb_types { type \"ONE_LEVEL\" { map[none] = 1; }; };\n"
        "    xkb_symbols {\n"
        "        key <> { [a] };\n"
        "    };\n"
        "};",
        /* 2 groups */
        "default xkb_keymap {\n"
        "    xkb_keycodes { <> = 1; };\n"
        "    xkb_types { type \"ONE_LEVEL\" { map[none] = 1; }; };\n"
        "    xkb_symbols {\n"
        "        key <> { [a], [b] };\n"
        "    };\n"
        "};",
        /* 3 groups */
        "default xkb_keymap {\n"
        "    xkb_keycodes { <> = 1; };\n"
        "    xkb_types { type \"ONE_LEVEL\" { map[none] = 1; }; };\n"
        "    xkb_symbols {\n"
        "        key <> { [a], [b], [c] };\n"
        "    };\n"
        "};",
        /* 4 groups */
        "default xkb_keymap {\n"
        "    xkb_keycodes { <> = 1; };\n"
        "    xkb_types { type \"ONE_LEVEL\" { map[none] = 1; }; };\n"
        "    xkb_symbols {\n"
        "        key <> { [a], [b], [c], [d] };\n"
        "    };\n"
        "};",
    };

    for (int32_t g = 0; g < (int32_t)ARRAY_SIZE(keymaps); g++) {
        fprintf(stderr, "------\n*** %s: #%"PRId32" groups ***\n", __func__, g);
        struct xkb_keymap *keymap =
            test_compile_buffer(ctx, keymaps[g], strlen(keymaps[g]));
        assert(keymap);
        struct xkb_state *state = xkb_state_new(keymap);
        assert(state);

        const xkb_keycode_t keycode = xkb_keymap_key_by_name(keymap, "");
        assert(keycode == 1);

        for (int32_t base = -2*(g + 1); base <= 2*(g + 1); base++) {
            for (int32_t latched = -2*(g + 1); latched <= 2*(g + 1); latched++) {
                for (int32_t locked = -2*(g + 1); locked <= 2*(g + 2); locked++) {
                    xkb_state_update_mask(state, 0, 0, 0, base, latched, locked);

                    xkb_layout_index_t got;
                    xkb_layout_index_t expected;

                    /* Base layout should be unchanged */
                    got = xkb_state_serialize_layout(state,
                                                     XKB_STATE_LAYOUT_DEPRESSED);
                    expected = (xkb_layout_index_t) base;
                    assert_printf(got == expected,
                                  "Base layout: expected %"PRIu32", "
                                  "got: %"PRIu32"\n",
                                  expected, got);

                    /* Latched layout should be unchanged */
                    got = xkb_state_serialize_layout(state,
                                                     XKB_STATE_LAYOUT_LATCHED);
                    expected = (xkb_layout_index_t) latched;
                    assert_printf(got == expected,
                                  "Latched layout: expected %"PRIu32", "
                                  "got: %"PRIu32"\n",
                                  expected, got);

                    /* Locked layout should be wrapped */
                    got = xkb_state_serialize_layout(state,
                                                     XKB_STATE_LAYOUT_LOCKED);
                    const xkb_layout_index_t locked_expected =
                        group_wrap(locked, g);
                    expected = locked_expected;
                    assert_printf(got == expected,
                                  "Locked layout: expected %"PRIu32", "
                                  "got: %"PRIu32"\n",
                                  expected, got);

                    /* Effective layout should be wrapped */
                    got = xkb_state_serialize_layout(state,
                                                     XKB_STATE_LAYOUT_EFFECTIVE);
                    const xkb_layout_index_t effective_expected =
                        group_wrap(base + latched + (int32_t) locked_expected, g);
                    expected = effective_expected;
                    assert_printf(got == expected,
                                  "Effective layout: expected %"PRIu32", "
                                  "got: %"PRIu32"\n",
                                  expected, got);

                    /*
                     * Ensure all API using a layout index do not segfault
                     */

                    xkb_keymap_layout_get_name(keymap, base);

                    const xkb_level_index_t num_levels =
                        xkb_keymap_num_levels_for_key(keymap, keycode, base);

                    const xkb_level_index_t num_levels_expected = (g > 0);
                    assert_printf(num_levels == num_levels_expected,
                                  "Group=%"PRId32"/%"PRId32": "
                                  "Expected %"PRIu32", got: %"PRIu32"\n",
                                  base + 1, g, num_levels_expected, num_levels);

                    xkb_mod_mask_t masks[1] = {0};
                    const size_t size =
                        xkb_keymap_key_get_mods_for_level(keymap, keycode, base, 0,
                                                          masks, ARRAY_SIZE(masks));
                    const size_t size_expected = (g > 0);
                    assert(size == size_expected && masks[0] == 0);

                    const xkb_keysym_t *keysyms = NULL;
                    const int num_keysyms =
                        xkb_keymap_key_get_syms_by_level(keymap, keycode, base,
                                                         0, &keysyms);
                    const int num_keysyms_expected = (g > 0);
                    assert(num_keysyms == num_keysyms_expected &&
                           (g == 0 || keysyms[0] != XKB_KEY_NoSymbol));

                    const xkb_level_index_t level =
                        xkb_state_key_get_level(state, keycode, base);
                    const xkb_level_index_t level_expected =
                        is_valid_group(base, g) ? 0 : XKB_LEVEL_INVALID;
                    assert_printf(level == level_expected,
                                  "Group=%"PRId32"/%"PRId32": "
                                  "Expected %"PRIu32", got: %"PRIu32"\n",
                                  base + 1, g, level_expected, level);

                    int is_active, is_active_expected;
                    is_active = xkb_state_layout_index_is_active(
                        state, base, XKB_STATE_LAYOUT_DEPRESSED
                    );
                    is_active_expected = is_valid_group(base, g) ? 1 : -1;
                    assert(is_active == is_active_expected);

                    is_active = xkb_state_layout_index_is_active(
                        state, latched, XKB_STATE_LAYOUT_LATCHED
                    );
                    is_active_expected = is_valid_group(latched, g) ? 1 : -1;
                    assert(is_active == is_active_expected);

                    is_active = xkb_state_layout_index_is_active(
                        state, locked, XKB_STATE_LAYOUT_LOCKED
                    );
                    is_active_expected =
                        is_valid_group(locked, g) ? 1 : -1;
                    assert(is_active == is_active_expected);

                    is_active = xkb_state_layout_index_is_active(
                        state, locked_expected, XKB_STATE_LAYOUT_LOCKED
                    );
                    assert(
                        is_valid_group((int32_t) locked_expected, g) == (g > 0)
                    );
                    is_active_expected =
                        is_valid_group((int32_t) locked_expected, g) ? 1 : -1;
                    assert(is_active == is_active_expected);

                    is_active = xkb_state_layout_index_is_active(
                        state, effective_expected, XKB_STATE_LAYOUT_EFFECTIVE
                    );
                    assert(
                        is_valid_group((int32_t) effective_expected, g) == (g > 0)
                    );
                    is_active_expected =
                        is_valid_group((int32_t) effective_expected, g) ? 1 : -1;
                    assert(is_active == is_active_expected);
                }
            }
        }

        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
    }
}

static inline xkb_mod_index_t
_xkb_keymap_mod_get_index(struct xkb_keymap *keymap, const char *name)
{
    xkb_mod_index_t mod = xkb_keymap_mod_get_index(keymap, name);
    assert(mod != XKB_MOD_INVALID);
    return mod;
}

static inline xkb_led_index_t
_xkb_keymap_led_get_index(struct xkb_keymap *keymap, const char *name)
{
    xkb_mod_index_t led = xkb_keymap_led_get_index(keymap, name);
    assert(led != XKB_LED_INVALID);
    return led;
}

static void
print_state(struct xkb_state *state)
{
    struct xkb_keymap *keymap;
    xkb_layout_index_t group;
    xkb_mod_index_t mod;
    xkb_led_index_t led;

    group = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
    mod = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
    /* led = xkb_state_serialize_leds(state, XKB_STATE_LEDS); */
    if (!group && !mod /* && !led */) {
        fprintf(stderr, "\tno state\n");
        return;
    }

    keymap = xkb_state_get_keymap(state);

    for (group = 0; group < xkb_keymap_num_layouts(keymap); group++) {
        if (xkb_state_layout_index_is_active(state, group,
                                             XKB_STATE_LAYOUT_EFFECTIVE |
                                             XKB_STATE_LAYOUT_DEPRESSED |
                                             XKB_STATE_LAYOUT_LATCHED |
                                             XKB_STATE_LAYOUT_LOCKED) <= 0)
            continue;
        fprintf(stderr, "\tgroup %s (%d): %s%s%s%s\n",
                xkb_keymap_layout_get_name(keymap, group),
                group,
                xkb_state_layout_index_is_active(state, group, XKB_STATE_LAYOUT_EFFECTIVE) > 0 ?
                    "effective " : "",
                xkb_state_layout_index_is_active(state, group, XKB_STATE_LAYOUT_DEPRESSED) > 0 ?
                    "depressed " : "",
                xkb_state_layout_index_is_active(state, group, XKB_STATE_LAYOUT_LATCHED) > 0 ?
                    "latched " : "",
                xkb_state_layout_index_is_active(state, group, XKB_STATE_LAYOUT_LOCKED) > 0 ?
                    "locked " : "");
    }

    for (mod = 0; mod < xkb_keymap_num_mods(keymap); mod++) {
        if (xkb_state_mod_index_is_active(state, mod,
                                          XKB_STATE_MODS_EFFECTIVE |
                                          XKB_STATE_MODS_DEPRESSED |
                                          XKB_STATE_MODS_LATCHED |
                                          XKB_STATE_MODS_LOCKED) <= 0)
            continue;
        fprintf(stderr, "\tmod %s (%d): %s%s%s%s\n",
                xkb_keymap_mod_get_name(keymap, mod),
                mod,
                xkb_state_mod_index_is_active(state, mod, XKB_STATE_MODS_EFFECTIVE) > 0 ?
                    "effective " : "",
                xkb_state_mod_index_is_active(state, mod, XKB_STATE_MODS_DEPRESSED) > 0 ?
                    "depressed " : "",
                xkb_state_mod_index_is_active(state, mod, XKB_STATE_MODS_LATCHED) > 0 ?
                    "latched " : "",
                xkb_state_mod_index_is_active(state, mod, XKB_STATE_MODS_LOCKED) > 0 ?
                    "locked " : "");
    }

    for (led = 0; led < xkb_keymap_num_leds(keymap); led++) {
        if (xkb_state_led_index_is_active(state, led) <= 0)
            continue;
        fprintf(stderr, "\tled %s (%d): active\n",
                xkb_keymap_led_get_name(keymap, led),
                led);
    }
}

static void
test_update_key(struct xkb_keymap *keymap)
{
    struct xkb_state *state = xkb_state_new(keymap);
    const xkb_keysym_t *syms;
    xkb_keysym_t one_sym;
    int num_syms;

    assert(state);

    xkb_mod_index_t ctrl = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    xkb_mod_index_t mod1 = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD1);
    xkb_mod_index_t alt  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    xkb_mod_index_t meta = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);

    /* LCtrl down */
    xkb_state_update_key(state, KEY_LEFTCTRL + EVDEV_OFFSET, XKB_KEY_DOWN);
    fprintf(stderr, "dumping state for LCtrl down:\n");
    print_state(state);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                        XKB_STATE_MODS_DEPRESSED) > 0);

    /* LCtrl + RAlt down */
    xkb_state_update_key(state, KEY_RIGHTALT + EVDEV_OFFSET, XKB_KEY_DOWN);
    fprintf(stderr, "dumping state for LCtrl + RAlt down:\n");
    print_state(state);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_MOD1,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_ALT,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_META,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL,
                                          XKB_MOD_NAME_CTRL,
                                          XKB_MOD_NAME_MOD1,
                                          XKB_VMOD_NAME_ALT,
                                          XKB_VMOD_NAME_META,
                                          NULL) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                            XKB_STATE_MATCH_ALL,
                                            ctrl, mod1, alt, meta,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL,
                                          XKB_MOD_NAME_MOD1,
                                          NULL) == 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL,
                                          XKB_VMOD_NAME_ALT,
                                          NULL) == 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL,
                                          XKB_VMOD_NAME_META,
                                          NULL) == 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL |
                                          XKB_STATE_MATCH_NON_EXCLUSIVE,
                                          XKB_MOD_NAME_MOD1,
                                          NULL) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL |
                                          XKB_STATE_MATCH_NON_EXCLUSIVE,
                                          XKB_VMOD_NAME_ALT,
                                          NULL) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ALL |
                                          XKB_STATE_MATCH_NON_EXCLUSIVE,
                                          XKB_VMOD_NAME_META,
                                          NULL) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          (XKB_STATE_MATCH_ANY |
                                           XKB_STATE_MATCH_NON_EXCLUSIVE),
                                          XKB_MOD_NAME_MOD1,
                                          NULL) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          (XKB_STATE_MATCH_ANY |
                                           XKB_STATE_MATCH_NON_EXCLUSIVE),
                                          XKB_VMOD_NAME_ALT,
                                          NULL) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          (XKB_STATE_MATCH_ANY |
                                           XKB_STATE_MATCH_NON_EXCLUSIVE),
                                          XKB_VMOD_NAME_META,
                                          NULL) > 0);

    /* RAlt down */
    xkb_state_update_key(state, KEY_LEFTCTRL + EVDEV_OFFSET, XKB_KEY_UP);
    fprintf(stderr, "dumping state for RAlt down:\n");
    print_state(state);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                        XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_MOD1,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_ALT,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_META,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_DEPRESSED,
                                          XKB_STATE_MATCH_ANY,
                                          XKB_MOD_NAME_CTRL,
                                          XKB_MOD_NAME_MOD1,
                                          XKB_VMOD_NAME_ALT,
                                          XKB_VMOD_NAME_META,
                                          NULL) > 0);
    assert(xkb_state_mod_names_are_active(state, XKB_STATE_MODS_LATCHED,
                                          XKB_STATE_MATCH_ANY,
                                          XKB_MOD_NAME_CTRL,
                                          XKB_MOD_NAME_MOD1,
                                          XKB_VMOD_NAME_ALT,
                                          XKB_VMOD_NAME_META,
                                          NULL) == 0);

    /* none down */
    xkb_state_update_key(state, KEY_RIGHTALT + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_MOD1,
                                        XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_ALT,
                                        XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_META,
                                        XKB_STATE_MODS_EFFECTIVE) == 0);

    /* Caps locked */
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                        XKB_STATE_MODS_DEPRESSED) > 0);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    fprintf(stderr, "dumping state for Caps Lock:\n");
    print_state(state);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                        XKB_STATE_MODS_DEPRESSED) == 0);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                        XKB_STATE_MODS_LOCKED) > 0);
    assert(xkb_state_led_name_is_active(state, XKB_LED_NAME_CAPS) > 0);
    num_syms = xkb_state_key_get_syms(state, KEY_Q + EVDEV_OFFSET, &syms);
    assert(num_syms == 1 && syms[0] == XKB_KEY_Q);

    /* Num Lock locked */
    xkb_state_update_key(state, KEY_NUMLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_NUMLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    fprintf(stderr, "dumping state for Caps Lock + Num Lock:\n");
    print_state(state);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                        XKB_STATE_MODS_LOCKED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_MOD2,
                                        XKB_STATE_MODS_LOCKED) > 0);
    assert(xkb_state_mod_name_is_active(state, XKB_VMOD_NAME_NUM,
                                        XKB_STATE_MODS_LOCKED) > 0);
    num_syms = xkb_state_key_get_syms(state, KEY_KP1 + EVDEV_OFFSET, &syms);
    assert(num_syms == 1 && syms[0] == XKB_KEY_KP_1);
    assert(xkb_state_led_name_is_active(state, XKB_LED_NAME_NUM) > 0);

    /* Num Lock unlocked */
    xkb_state_update_key(state, KEY_NUMLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_NUMLOCK + EVDEV_OFFSET, XKB_KEY_UP);

    /* Switch to group 2 */
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_led_name_is_active(state, "Group 2") > 0);
    assert(xkb_state_led_name_is_active(state, XKB_LED_NAME_NUM) == 0);

    /* Switch back to group 1. */
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_UP);

    /* Caps unlocked */
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                        XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_led_name_is_active(state, XKB_LED_NAME_CAPS) == 0);
    num_syms = xkb_state_key_get_syms(state, KEY_Q + EVDEV_OFFSET, &syms);
    assert(num_syms == 1 && syms[0] == XKB_KEY_q);

    /* Multiple symbols */
    num_syms = xkb_state_key_get_syms(state, KEY_6 + EVDEV_OFFSET, &syms);
    assert(num_syms == 5 &&
           syms[0] == XKB_KEY_H && syms[1] == XKB_KEY_E &&
           syms[2] == XKB_KEY_L && syms[3] == XKB_KEY_L &&
           syms[4] == XKB_KEY_O);
    one_sym = xkb_state_key_get_one_sym(state, KEY_6 + EVDEV_OFFSET);
    assert(one_sym == XKB_KEY_NoSymbol);
    xkb_state_update_key(state, KEY_6 + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_6 + EVDEV_OFFSET, XKB_KEY_UP);

    one_sym = xkb_state_key_get_one_sym(state, KEY_5 + EVDEV_OFFSET);
    assert(one_sym == XKB_KEY_5);

    xkb_state_unref(state);
}

struct test_active_mods_entry {
    xkb_mod_mask_t state;
    xkb_mod_mask_t active;
};

static void
test_serialisation(struct xkb_keymap *keymap)
{
    struct xkb_state *state = xkb_state_new(keymap);
    xkb_mod_mask_t base_mods;
    xkb_mod_mask_t latched_mods;
    xkb_mod_mask_t locked_mods;
    xkb_mod_mask_t effective_mods;
    xkb_layout_index_t base_group = 0;
    xkb_layout_index_t latched_group = 0;
    xkb_layout_index_t locked_group = 0;

    assert(state);

    xkb_mod_index_t shiftIdx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t capsIdx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    xkb_mod_index_t ctrlIdx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    xkb_mod_index_t mod1Idx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD1);
    xkb_mod_index_t mod2Idx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD2);
    xkb_mod_index_t mod3Idx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD3);
    xkb_mod_index_t mod4Idx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD4);
    xkb_mod_index_t mod5Idx   = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD5);
    xkb_mod_index_t altIdx    = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    xkb_mod_index_t metaIdx   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);
    xkb_mod_index_t superIdx  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_SUPER);
    xkb_mod_index_t hyperIdx  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_HYPER);
    xkb_mod_index_t numIdx    = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_NUM);
    xkb_mod_index_t level3Idx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_LEVEL3);
    xkb_mod_index_t altGrIdx  = _xkb_keymap_mod_get_index(keymap, "AltGr");
    xkb_mod_mask_t shift  = (UINT32_C(1) << shiftIdx);
    xkb_mod_mask_t caps   = (UINT32_C(1) << capsIdx);
    xkb_mod_mask_t ctrl   = (UINT32_C(1) << ctrlIdx);
    xkb_mod_mask_t mod1   = (UINT32_C(1) << mod1Idx);
    xkb_mod_mask_t mod2   = (UINT32_C(1) << mod2Idx);
    xkb_mod_mask_t mod3   = (UINT32_C(1) << mod3Idx);
    xkb_mod_mask_t mod4   = (UINT32_C(1) << mod4Idx);
    xkb_mod_mask_t mod5   = (UINT32_C(1) << mod5Idx);
    xkb_mod_mask_t alt    = (UINT32_C(1) << altIdx);
    xkb_mod_mask_t meta   = (UINT32_C(1) << metaIdx);
    xkb_mod_mask_t super  = (UINT32_C(1) << superIdx);
    xkb_mod_mask_t hyper  = (UINT32_C(1) << hyperIdx);
    xkb_mod_mask_t num    = (UINT32_C(1) << numIdx);
    xkb_mod_mask_t level3 = (UINT32_C(1) << level3Idx);
    xkb_mod_mask_t altGr  = (UINT32_C(1) << altGrIdx);

    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    base_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    assert(base_mods == 0);
    latched_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    assert(latched_mods == 0);
    locked_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    assert(locked_mods == caps);
    effective_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
    assert(effective_mods == locked_mods);

    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    base_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
    assert(base_mods == shift);
    latched_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
    assert(latched_mods == 0);
    locked_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    assert(locked_mods == caps);
    effective_mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
    assert(effective_mods == (base_mods | locked_mods));

    base_mods |= ctrl;
    xkb_state_update_mask(state, base_mods, latched_mods, locked_mods,
                          base_group, latched_group, locked_group);

    assert(xkb_state_mod_index_is_active(state, ctrlIdx, XKB_STATE_MODS_DEPRESSED) > 0);
    assert(xkb_state_mod_index_is_active(state, ctrlIdx, XKB_STATE_MODS_EFFECTIVE) > 0);

    const struct test_active_mods_entry test_data[] = {
        { .state = 0,            .active = 0                         },
        { .state = shift,        .active = shift                     },
        { .state = caps,         .active = caps                      },
        { .state = ctrl,         .active = ctrl                      },
        { .state = mod1,         .active = mod1 | alt | meta         },
        { .state = mod2,         .active = mod2 | num                },
        { .state = mod3,         .active = mod3                      },
        { .state = mod4,         .active = mod4 | super | hyper      },
        { .state = mod5,         .active = mod5 | level3 | altGr     },
        { .state = shift | mod1, .active = shift | mod1 | alt | meta },
        { .state = shift | mod2, .active = shift | mod2 | num        },
    };

    for (unsigned k = 0; k < ARRAY_SIZE(test_data); k++) {
        const struct test_active_mods_entry *entry = &test_data[k];
#define check_mods(keymap, state_, entry, type)                                     \
        for (xkb_mod_index_t idx = 0; idx < xkb_keymap_num_mods(keymap); idx++) {   \
            xkb_mod_mask_t mask = UINT32_C(1) << idx;                               \
            bool expected = !!(mask & (entry)->active);                             \
            bool got = !!xkb_state_mod_index_is_active(state_, idx, type);          \
            fprintf(stderr, "#%u State 0x%x, mod: %s (%u): expected %u, got: %u\n", \
                    k, (entry)->state, xkb_keymap_mod_get_name(keymap, idx), idx,   \
                    expected, got);                                                 \
            assert_printf(got == expected,                                          \
                          "xkb_state_mod_index_is_active, " STRINGIFY2(type) "\n"); \
            got = !!xkb_state_mod_index_is_active(state_, idx,                      \
                                                  XKB_STATE_MODS_EFFECTIVE);        \
            assert_printf(got == expected, "xkb_state_mod_index_is_active, "        \
                          STRINGIFY2(XKB_STATE_MODS_EFFECTIVE) "\n");               \
            got = !!xkb_state_mod_indices_are_active(                               \
                        state_, type,                                               \
                        XKB_STATE_MATCH_ALL | XKB_STATE_MATCH_NON_EXCLUSIVE,        \
                        idx, XKB_MOD_INVALID);                                      \
            assert_printf(got == expected, "xkb_state_mod_indices_are_active, "     \
                          STRINGIFY2(type) "\n");                                   \
            got = !!xkb_state_mod_indices_are_active(                               \
                        state_, XKB_STATE_MODS_EFFECTIVE,                           \
                        XKB_STATE_MATCH_ALL | XKB_STATE_MATCH_NON_EXCLUSIVE,        \
                        idx, XKB_MOD_INVALID);                                      \
            assert_printf(got == expected, "xkb_state_mod_indices_are_active, "     \
                          STRINGIFY2(XKB_STATE_MODS_EFFECTIVE) "\n");               \
        }
        xkb_state_update_mask(state, entry->state, 0, 0, 0, 0, 0);
        check_mods(keymap, state, entry, XKB_STATE_MODS_DEPRESSED);
        xkb_state_update_mask(state, 0, entry->state, 0, 0, 0, 0);
        check_mods(keymap, state, entry, XKB_STATE_MODS_LATCHED);
        xkb_state_update_mask(state, 0, 0, entry->state, 0, 0, 0);
        check_mods(keymap, state, entry, XKB_STATE_MODS_LOCKED);
    }

    xkb_state_unref(state);
}

static void
test_update_mask_mods(struct xkb_keymap *keymap)
{
    enum xkb_state_component changed;
    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);

    xkb_mod_index_t capsIdx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    xkb_mod_index_t shiftIdx = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t mod1Idx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD1);
    xkb_mod_index_t mod2Idx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD2);
    xkb_mod_index_t altIdx   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    xkb_mod_index_t metaIdx  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);
    xkb_mod_index_t numIdx   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_NUM);
    xkb_mod_mask_t caps  = (UINT32_C(1) << capsIdx);
    xkb_mod_mask_t shift = (UINT32_C(1) << shiftIdx);
    xkb_mod_mask_t mod1  = (UINT32_C(1) << mod1Idx);
    xkb_mod_mask_t mod2  = (UINT32_C(1) << mod2Idx);
    xkb_mod_mask_t alt   = (UINT32_C(1) << altIdx);
    xkb_mod_mask_t meta  = (UINT32_C(1) << metaIdx);
    xkb_mod_mask_t num   = (UINT32_C(1) << numIdx);

    changed = xkb_state_update_mask(state, caps, 0, 0, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_EFFECTIVE));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) == caps);

    changed = xkb_state_update_mask(state, caps, 0, shift, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_LOCKED | XKB_STATE_MODS_EFFECTIVE |
                       XKB_STATE_LEDS));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) ==
           (caps | shift));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) == caps);
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED) == 0);
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED) == shift);

    changed = xkb_state_update_mask(state, 0, 0, 0, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LOCKED |
                       XKB_STATE_MODS_EFFECTIVE | XKB_STATE_LEDS));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) == 0);

    changed = xkb_state_update_mask(state, alt, 0, 0, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_EFFECTIVE));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) ==
           (alt | mod1));

    changed = xkb_state_update_mask(state, meta, 0, 0, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_EFFECTIVE));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) ==
           (meta | mod1));

    changed = xkb_state_update_mask(state, 0, 0, num, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LOCKED |
                       XKB_STATE_MODS_EFFECTIVE | XKB_STATE_LEDS));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) ==
           (num | mod2));

    xkb_state_update_mask(state, 0, 0, 0, 0, 0, 0);

    changed = xkb_state_update_mask(state, mod2, 0, num, 0, 0, 0);
    assert(changed == (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LOCKED |
                       XKB_STATE_MODS_EFFECTIVE | XKB_STATE_LEDS));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) ==
           (mod2 | num));
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED) ==
           mod2);
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED) ==
           (num | mod2));

    xkb_state_unref(state);
}

static void
test_repeat(struct xkb_keymap *keymap)
{
    assert(!xkb_keymap_key_repeats(keymap, KEY_LEFTSHIFT + EVDEV_OFFSET));
    assert(xkb_keymap_key_repeats(keymap, KEY_A + EVDEV_OFFSET));
    assert(xkb_keymap_key_repeats(keymap, KEY_8 + EVDEV_OFFSET));
    assert(xkb_keymap_key_repeats(keymap, KEY_DOWN + EVDEV_OFFSET));
    assert(xkb_keymap_key_repeats(keymap, KEY_KBDILLUMDOWN + EVDEV_OFFSET));
}

static void
test_consume(struct xkb_keymap *keymap)
{
    xkb_mod_mask_t mask;
    xkb_mod_index_t shift = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t caps  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    xkb_mod_index_t ctrl  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    xkb_mod_index_t mod1  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD1);
    xkb_mod_index_t mod2  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD2);
    xkb_mod_index_t mod5  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD5);
    xkb_mod_index_t alt   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    xkb_mod_index_t meta  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);
    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);

    /* Test remove_consumed() */
    xkb_state_update_key(state, KEY_LEFTALT + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_EQUAL + EVDEV_OFFSET, XKB_KEY_DOWN);

    fprintf(stderr, "dumping state for Alt-Shift-+\n");
    print_state(state);

    mask = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
    assert(mask == ((UINT32_C(1) << mod1) | (UINT32_C(1) << shift)));
    mask = xkb_state_mod_mask_remove_consumed(state, KEY_EQUAL + EVDEV_OFFSET,
                                              mask);
    assert(mask == (UINT32_C(1) << mod1));

    /* Test get_consumed_mods() */
    mask = xkb_state_key_get_consumed_mods(state, KEY_EQUAL + EVDEV_OFFSET);
    assert(mask == (UINT32_C(1) << shift));

    mask = xkb_state_key_get_consumed_mods(state, KEY_ESC + EVDEV_OFFSET);
    assert(mask == 0);

    assert(xkb_state_mod_index_is_consumed(state, KEY_EQUAL + EVDEV_OFFSET, shift) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_EQUAL + EVDEV_OFFSET, mod1) == 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_EQUAL + EVDEV_OFFSET, alt) == 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_EQUAL + EVDEV_OFFSET, meta) == 0);

    xkb_state_unref(state);

    /* Test is_consumed() - simple ALPHABETIC type. */
    state = xkb_state_new(keymap);
    assert(state);

    mask = xkb_state_key_get_consumed_mods(state, KEY_A + EVDEV_OFFSET);
    assert(mask == ((UINT32_C(1) << shift) | (UINT32_C(1) << caps)));

    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, caps) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, shift) > 0);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, caps) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, shift) > 0);
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, caps) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, shift) > 0);
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, caps) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_A + EVDEV_OFFSET, shift) > 0);

    xkb_state_unref(state);

    /* More complicated - CTRL+ALT */
    state = xkb_state_new(keymap);
    assert(state);

    mask = xkb_state_key_get_consumed_mods(state, KEY_F1 + EVDEV_OFFSET);
    assert(mask == ((UINT32_C(1) << shift) | (UINT32_C(1) << mod1) |
                    (UINT32_C(1) << ctrl) | (UINT32_C(1) << mod5)));

    /* Shift is preserved. */
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    mask = xkb_state_key_get_consumed_mods(state, KEY_F1 + EVDEV_OFFSET);
    assert(mask == ((UINT32_C(1) << mod1) | (UINT32_C(1) << ctrl) |
                    (UINT32_C(1) << mod5)));
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);

    mask = xkb_state_key_get_consumed_mods(state, KEY_F1 + EVDEV_OFFSET);
    assert(mask == ((UINT32_C(1) << shift) | (UINT32_C(1) << mod1) |
                    (UINT32_C(1) << ctrl) | (UINT32_C(1) << mod5)));

    xkb_state_unref(state);

    /* Test XKB_CONSUMED_MODE_GTK, CTRL+ALT */
    state = xkb_state_new(keymap);
    assert(state);

    mask = xkb_state_key_get_consumed_mods2(state, KEY_F1 + EVDEV_OFFSET,
                                            XKB_CONSUMED_MODE_GTK);
    assert(mask == 0);

    xkb_state_update_key(state, KEY_LEFTCTRL + EVDEV_OFFSET, XKB_KEY_DOWN);
    mask = xkb_state_key_get_consumed_mods2(state, KEY_F1 + EVDEV_OFFSET,
                                            XKB_CONSUMED_MODE_GTK);
    assert(mask == 0);

    xkb_state_update_key(state, KEY_LEFTALT + EVDEV_OFFSET, XKB_KEY_DOWN);
    mask = xkb_state_key_get_consumed_mods2(state, KEY_F1 + EVDEV_OFFSET,
                                            XKB_CONSUMED_MODE_GTK);
    assert(mask == ((UINT32_C(1) << mod1) | (UINT32_C(1) << ctrl)));
    assert(xkb_state_mod_index_is_consumed(state, KEY_F1 + EVDEV_OFFSET, shift) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_F1 + EVDEV_OFFSET, ctrl) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_F1 + EVDEV_OFFSET, mod1) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_F1 + EVDEV_OFFSET, alt) > 0);
    assert(xkb_state_mod_index_is_consumed(state, KEY_F1 + EVDEV_OFFSET, meta) > 0);
    mask = (UINT32_C(1) << ctrl) | (UINT32_C(1) << mod1) | (UINT32_C(1) << mod2);
    mask = xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, mask);
    assert(mask == (UINT32_C(1) << mod2));
    mask = (UINT32_C(1) << ctrl) | (UINT32_C(1) << alt) | (UINT32_C(1) << meta) |
           (UINT32_C(1) << mod2);
    mask = xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, mask);
    assert(mask == (UINT32_C(1) << mod2));

    xkb_state_unref(state);

    /* Test XKB_CONSUMED_MODE_GTK, Simple Shift */
    state = xkb_state_new(keymap);
    assert(state);

    mask = xkb_state_key_get_consumed_mods2(state, KEY_A + EVDEV_OFFSET,
                                            XKB_CONSUMED_MODE_GTK);
    assert(mask == ((UINT32_C(1) << shift) | (UINT32_C(1) << caps)));

    xkb_state_update_key(state, KEY_LEFTALT + EVDEV_OFFSET, XKB_KEY_DOWN);
    mask = xkb_state_key_get_consumed_mods2(state, KEY_A + EVDEV_OFFSET,
                                            XKB_CONSUMED_MODE_GTK);
    assert(mask == ((UINT32_C(1) << shift) | (UINT32_C(1) << caps)));

    xkb_state_unref(state);
}

static void
test_overlapping_mods(struct xkb_context *context)
{
    struct xkb_keymap *keymap;
    struct xkb_state *state;

    /* Super and Hyper are overlapping (full overlap) */
    keymap = test_compile_rules(context, "evdev", NULL, "us", NULL,
                                "overlapping_modifiers:super_hyper,"
                                "grp:win_space_toggle");
    assert(keymap);
    xkb_mod_index_t shiftIdx = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t capsIdx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    xkb_mod_index_t ctrlIdx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    xkb_mod_index_t mod1Idx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD1);
    xkb_mod_index_t mod3Idx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD3);
    xkb_mod_index_t mod4Idx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD4);
    xkb_mod_index_t mod5Idx  = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_MOD5);
    xkb_mod_index_t altIdx   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    xkb_mod_index_t metaIdx  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);
    xkb_mod_index_t superIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_SUPER);
    xkb_mod_index_t hyperIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_HYPER);
    /* Note: not mapped */
    xkb_mod_index_t scrollIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_SCROLL);
    xkb_mod_mask_t shift = (UINT32_C(1) << shiftIdx);
    xkb_mod_mask_t ctrl  = (UINT32_C(1) << ctrlIdx);
    xkb_mod_mask_t mod1  = (UINT32_C(1) << mod1Idx);
    xkb_mod_mask_t mod3  = (UINT32_C(1) << mod3Idx);
    xkb_mod_mask_t mod4  = (UINT32_C(1) << mod4Idx);
    xkb_mod_mask_t mod5  = (UINT32_C(1) << mod5Idx);
    xkb_mod_mask_t alt   = (UINT32_C(1) << altIdx);
    xkb_mod_mask_t meta  = (UINT32_C(1) << metaIdx);
    xkb_mod_mask_t super = (UINT32_C(1) << superIdx);
    xkb_mod_mask_t hyper = (UINT32_C(1) << hyperIdx);
    state = xkb_state_new(keymap);
    assert(state);

    const struct test_active_mods_entry test_data1[] = {
        { .state = 0,           .active = 0                           },
        { .state = mod1,        .active = mod1 | alt | meta           },
        { .state = mod3,        .active = mod3                        },
        { .state = mod4,        .active = mod4                        },
        { .state = alt,         .active = mod1 | alt | meta           },
        { .state = meta,        .active = mod1 | alt | meta           },
        { .state = super,       .active = mod3 | mod4 | super | hyper },
        { .state = hyper,       .active = mod3 | mod4 | super | hyper },
        { .state = mod3 | mod4, .active = mod3 | mod4 | super | hyper },
    };

    for (unsigned k = 0; k < ARRAY_SIZE(test_data1); k++) {
        const struct test_active_mods_entry *entry = &test_data1[k];
        xkb_state_update_mask(state, entry->state, 0, 0, 0, 0, 0);
        check_mods(keymap, state, entry, XKB_STATE_MODS_DEPRESSED);
    }
    assert(xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE) == (mod3 | mod4));
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod3Idx, mod4Idx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod3Idx, mod4Idx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_key_get_consumed_mods2(state, KEY_F1 + EVDEV_OFFSET, XKB_CONSUMED_MODE_XKB) ==
           (shift | ctrl | mod1 | mod5));
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, (mod1 | mod4 | mod5)) == mod4);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, (alt | super)) == (mod3 | mod4));
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, shiftIdx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, capsIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, ctrlIdx,   XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, mod1Idx,   XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, mod5Idx,   XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, altIdx,    XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, metaIdx,   XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, superIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, hyperIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, scrollIdx, XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_key_get_consumed_mods2(state, KEY_SPACE + EVDEV_OFFSET, XKB_CONSUMED_MODE_XKB) == mod4);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_SPACE + EVDEV_OFFSET, (mod3 | mod4)) == mod3);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_SPACE + EVDEV_OFFSET, (super | hyper)) == mod3);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, shiftIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, capsIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, ctrlIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, mod1Idx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, mod5Idx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, altIdx,    XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, metaIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, superIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, hyperIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, scrollIdx, XKB_CONSUMED_MODE_XKB) == 0);
    xkb_state_update_mask(state, mod4, 0, 0, 0, 0, 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, shiftIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, capsIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, ctrlIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, mod1Idx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, mod5Idx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, altIdx,    XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, metaIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, superIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, hyperIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, scrollIdx, XKB_CONSUMED_MODE_XKB) == 0);
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);

    /* Super and Hyper are overlapping (full overlap).
     * Alt overlaps with Meta (incomplete overlap) */
    keymap = test_compile_rules(context, "evdev", NULL, "us", NULL,
                                "overlapping_modifiers:meta,"
                                "grp:win_space_toggle");
    assert(keymap);
    altIdx   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    metaIdx  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);
    superIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_SUPER);
    hyperIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_HYPER);
    alt   = (UINT32_C(1) << altIdx);
    meta  = (UINT32_C(1) << metaIdx);
    super = (UINT32_C(1) << superIdx);
    hyper = (UINT32_C(1) << hyperIdx);
    state = xkb_state_new(keymap);
    assert(state);

    const struct test_active_mods_entry test_data2[] = {
        { .state = 0,                  .active = 0                                               },
        { .state = mod1,               .active = mod1 | alt                                      },
        { .state = mod3,               .active = mod3                                            },
        { .state = mod4,               .active = mod4 | hyper | super                            },
        { .state = alt,                .active = mod1 | alt                                      },
        { .state = meta,               .active = mod1 | mod3 | alt | meta                        },
        { .state = super,              .active = mod4 | hyper | super                            },
        { .state = hyper,              .active = mod4 | hyper | super                            },
        { .state = mod1 | mod3,        .active = mod1 | mod3 | alt | meta                        },
        { .state = mod1 | mod4,        .active = mod1 | mod4 | alt | super | hyper               },
        { .state = mod3 | mod4,        .active = mod3 | mod4 | super | hyper                     },
        { .state = mod1 | mod3 | mod4, .active = mod1 | mod3 | mod4 | alt | meta | super | hyper },
    };

    for (unsigned k = 0; k < ARRAY_SIZE(test_data2); k++) {
        const struct test_active_mods_entry *entry = &test_data2[k];
        xkb_state_update_mask(state, entry->state, 0, 0, 0, 0, 0);
        check_mods(keymap, state, entry, XKB_STATE_MODS_DEPRESSED);
    }
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod1Idx, mod3Idx, mod4Idx, altIdx,
                                            metaIdx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod1Idx, mod3Idx, mod4Idx, altIdx,
                                            metaIdx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_key_get_consumed_mods2(state, KEY_F1 + EVDEV_OFFSET, XKB_CONSUMED_MODE_XKB) ==
           (shift | ctrl | mod1 | mod5));
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, (mod1 | mod4 | mod5)) == mod4);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, (alt | super)) == mod4);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, shiftIdx, XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, capsIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, ctrlIdx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, mod1Idx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, mod5Idx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, altIdx,   XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, metaIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, superIdx, XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, hyperIdx, XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_key_get_consumed_mods2(state, KEY_SPACE + EVDEV_OFFSET, XKB_CONSUMED_MODE_XKB) ==
           mod4);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_SPACE + EVDEV_OFFSET, (mod3 | mod4)) == mod3);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_SPACE + EVDEV_OFFSET, (super | hyper)) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, shiftIdx, XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, capsIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, ctrlIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, mod1Idx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, mod5Idx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, altIdx,   XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, metaIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, superIdx, XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_SPACE + EVDEV_OFFSET, hyperIdx, XKB_CONSUMED_MODE_XKB) > 0);
    xkb_state_update_mask(state, mod1, 0, 0, 0, 0, 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod1Idx, altIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod1Idx, altIdx,
                                            XKB_MOD_INVALID) > 0);
    xkb_state_update_mask(state, mod1 | mod3, 0, 0, 0, 0, 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod1Idx, mod3Idx, altIdx, metaIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod1Idx, mod3Idx, altIdx, metaIdx,
                                            XKB_MOD_INVALID) > 0);
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);

    /* Super and Hyper overlaps with Meta; Alt overlaps with Meta */
    keymap = test_compile_rules(context, "evdev", NULL, "us", NULL,
                                "overlapping_modifiers:super_hyper,"
                                "overlapping_modifiers:meta");
    assert(keymap);
    altIdx   = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_ALT);
    metaIdx  = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_META);
    superIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_SUPER);
    hyperIdx = _xkb_keymap_mod_get_index(keymap, XKB_VMOD_NAME_HYPER);
    alt   = (UINT32_C(1) << altIdx);
    meta  = (UINT32_C(1) << metaIdx);
    super = (UINT32_C(1) << superIdx);
    hyper = (UINT32_C(1) << hyperIdx);
    state = xkb_state_new(keymap);
    assert(state);

    const struct test_active_mods_entry test_data3[] = {
        { .state = 0,                  .active = 0                                               },
        { .state = mod1,               .active = mod1 | alt                                      },
        { .state = mod3,               .active = mod3                                            },
        { .state = mod4,               .active = mod4                                            },
        { .state = alt,                .active = mod1 | alt                                      },
        { .state = meta,               .active = mod1 | mod3 | alt | meta                        },
        { .state = super,              .active = mod3 | mod4 | super | hyper                     },
        { .state = hyper,              .active = mod3 | mod4 | super | hyper                     },
        { .state = mod1 | mod3,        .active = mod1 | mod3 | alt | meta                        },
        { .state = mod1 | mod3,        .active = mod1 | mod3 | alt | meta                        },
        { .state = mod1 | mod4,        .active = mod1 | mod4 | alt                               },
        { .state = mod3 | mod4,        .active = mod3 | mod4 | super | hyper                     },
        { .state = mod1 | mod3 | mod4, .active = mod1 | mod3 | mod4 | alt | meta | super | hyper },
    };

    for (unsigned k = 0; k < ARRAY_SIZE(test_data3); k++) {
        const struct test_active_mods_entry *entry = &test_data3[k];
        xkb_state_update_mask(state, entry->state, 0, 0, 0, 0, 0);
        check_mods(keymap, state, entry, XKB_STATE_MODS_DEPRESSED);
    }
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod1Idx, mod3Idx, mod4Idx, altIdx,
                                            metaIdx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod1Idx, mod3Idx, mod4Idx, altIdx,
                                            metaIdx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_key_get_consumed_mods2(state, KEY_F1 + EVDEV_OFFSET, XKB_CONSUMED_MODE_XKB) ==
           (shift | ctrl | mod1 | mod5));
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, (mod1 | mod4 | mod5)) == mod4);
    assert(xkb_state_mod_mask_remove_consumed(state, KEY_F1 + EVDEV_OFFSET, (alt | super)) == (mod3 | mod4));
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, shiftIdx, XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, capsIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, ctrlIdx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, mod1Idx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, mod5Idx,  XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, altIdx,   XKB_CONSUMED_MODE_XKB) > 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, metaIdx,  XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, superIdx, XKB_CONSUMED_MODE_XKB) == 0);
    assert(xkb_state_mod_index_is_consumed2(state, KEY_F1 + EVDEV_OFFSET, hyperIdx, XKB_CONSUMED_MODE_XKB) == 0);
    xkb_state_update_mask(state, mod1 | mod3, 0, 0, 0, 0, 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod1Idx, mod3Idx, altIdx, metaIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod1Idx, mod3Idx, altIdx, metaIdx,
                                            XKB_MOD_INVALID) > 0);
    xkb_state_update_mask(state, mod1 | mod4, 0, 0, 0, 0, 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod1Idx, mod4Idx, altIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod1Idx, mod4Idx, altIdx,
                                            XKB_MOD_INVALID) > 0);
    xkb_state_update_mask(state, mod3 | mod4, 0, 0, 0, 0, 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ANY,
                                            mod3Idx, mod4Idx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    assert(xkb_state_mod_indices_are_active(state, XKB_STATE_MODS_EFFECTIVE,
                                            XKB_STATE_MATCH_ALL,
                                            mod3Idx, mod4Idx, superIdx, hyperIdx,
                                            XKB_MOD_INVALID) > 0);
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
}

static void
key_iter(struct xkb_keymap *keymap, xkb_keycode_t key, void *data)
{
    xkb_keycode_t *counter = data;

    assert(*counter == key);
    (*counter)++;
}

static void
test_range(struct xkb_keymap *keymap)
{
    xkb_keycode_t counter;

    assert(xkb_keymap_min_keycode(keymap) == 9);
    assert(xkb_keymap_max_keycode(keymap) == 569);

    counter = xkb_keymap_min_keycode(keymap);
    xkb_keymap_key_for_each(keymap, key_iter, &counter);
    assert(counter == xkb_keymap_max_keycode(keymap) + 1);
}

static void
test_caps_keysym_transformation(struct xkb_context *context)
{
    int nsyms;
    xkb_keysym_t sym;
    const xkb_keysym_t *syms;

    const char keymap_str[] =
        "xkb_keymap {\n"
        " xkb_keycodes { include \"evdev\" };\n"
        " xkb_compat { include \"basic\" };\n"
        " xkb_types { include \"complete\" };\n"
        " xkb_symbols {\n"
        "  include \"pc+ch(fr)\"\n"
        "  key <AE13> { [{oe, ssharp}, {ae, s, s}] };"
        "  key <AB11> { [{3, ntilde}] };"
        "  replace key <RCTL> { [{Control_R, ISO_Next_Group}] };"
        " };"
        "};";

    struct xkb_keymap* const keymap =
        test_compile_buffer(context, keymap_str, sizeof(keymap_str));
    assert(keymap);

    xkb_mod_index_t shift = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t caps = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);

    /* See xkb_state_key_get_one_sym() for what's this all about. */

    assert(xkb_state_key_get_layout(state, KEY_A + EVDEV_OFFSET) == 0);
    assert(xkb_state_key_get_layout(state, KEY_SEMICOLON + EVDEV_OFFSET) == 0);

    /* Without caps, no transformation. */
    assert(xkb_state_mod_index_is_active(state, caps, XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_mod_index_is_active(state, shift, XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_key_get_level(state, KEY_A + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_A + EVDEV_OFFSET);
    assert(sym == XKB_KEY_a);
    assert(xkb_state_key_get_level(state, KEY_SEMICOLON + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_SEMICOLON + EVDEV_OFFSET);
    assert(sym == XKB_KEY_eacute);
    nsyms = xkb_state_key_get_syms(state, KEY_SEMICOLON + EVDEV_OFFSET, &syms);
    assert(nsyms == 1 && syms[0] == XKB_KEY_eacute);
    assert(xkb_state_key_get_level(state, KEY_YEN + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_YEN + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_YEN + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_oe && syms[1] == XKB_KEY_ssharp);
    assert(xkb_state_key_get_level(state, KEY_RO + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_RO + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_RO + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_3 && syms[1] == XKB_KEY_ntilde);
    assert(xkb_state_key_get_level(state, KEY_RIGHTCTRL + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_RIGHTCTRL + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_RIGHTCTRL + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_Control_R && syms[1] == XKB_KEY_ISO_Next_Group);

    /* With shift, no transformation (only different level). */
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_mod_index_is_active(state, caps, XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_mod_index_is_active(state, shift, XKB_STATE_MODS_EFFECTIVE) > 0);
    assert(xkb_state_key_get_level(state, KEY_A + EVDEV_OFFSET, 0) == 1);
    sym = xkb_state_key_get_one_sym(state, KEY_A + EVDEV_OFFSET);
    assert(sym == XKB_KEY_A);
    sym = xkb_state_key_get_one_sym(state, KEY_SEMICOLON + EVDEV_OFFSET);
    assert(sym == XKB_KEY_odiaeresis);
    nsyms = xkb_state_key_get_syms(state, KEY_SEMICOLON + EVDEV_OFFSET, &syms);
    assert(nsyms == 1 && syms[0] == XKB_KEY_odiaeresis);
    assert(xkb_state_key_get_level(state, KEY_YEN + EVDEV_OFFSET, 0) == 1);
    sym = xkb_state_key_get_one_sym(state, KEY_YEN + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_YEN + EVDEV_OFFSET, &syms);
    assert(nsyms == 3 && syms[0] == XKB_KEY_ae && syms[1] == XKB_KEY_s && syms[2] == XKB_KEY_s);
    assert(xkb_state_key_get_level(state, KEY_RO + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_RO + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_RO + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_3 && syms[1] == XKB_KEY_ntilde);
    assert(xkb_state_key_get_level(state, KEY_RIGHTCTRL + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_RIGHTCTRL + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_RIGHTCTRL + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_Control_R && syms[1] == XKB_KEY_ISO_Next_Group);
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_index_is_active(state, shift, XKB_STATE_MODS_EFFECTIVE) == 0);

    /* With caps, transform in same level. */
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_index_is_active(state, caps, XKB_STATE_MODS_EFFECTIVE) > 0);
    assert(xkb_state_mod_index_is_active(state, shift, XKB_STATE_MODS_EFFECTIVE) == 0);
    assert(xkb_state_key_get_level(state, KEY_A + EVDEV_OFFSET, 0) == 1);
    sym = xkb_state_key_get_one_sym(state, KEY_A + EVDEV_OFFSET);
    assert(sym == XKB_KEY_A);
    assert(xkb_state_key_get_level(state, KEY_SEMICOLON + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_SEMICOLON + EVDEV_OFFSET);
    assert(sym == XKB_KEY_Eacute);
    nsyms = xkb_state_key_get_syms(state, KEY_SEMICOLON + EVDEV_OFFSET, &syms);
    assert(nsyms == 1 && syms[0] == XKB_KEY_Eacute);
    assert(xkb_state_key_get_level(state, KEY_YEN + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_YEN + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_YEN + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_OE && syms[1] == XKB_KEY_Ssharp);
    assert(xkb_state_key_get_level(state, KEY_RO + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_RO + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_RO + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_3 && syms[1] == XKB_KEY_Ntilde);
    assert(xkb_state_key_get_level(state, KEY_RIGHTCTRL + EVDEV_OFFSET, 0) == 0);
    sym = xkb_state_key_get_one_sym(state, KEY_RIGHTCTRL + EVDEV_OFFSET);
    assert(sym == XKB_KEY_NoSymbol);
    nsyms = xkb_state_key_get_syms(state, KEY_RIGHTCTRL + EVDEV_OFFSET, &syms);
    assert(nsyms == 2 && syms[0] == XKB_KEY_Control_R && syms[1] == XKB_KEY_ISO_Next_Group);
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_mod_index_is_active(state, shift, XKB_STATE_MODS_EFFECTIVE) == 0);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
}

static void
test_get_utf8_utf32(struct xkb_keymap *keymap)
{
    char buf[256];
    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);

#define TEST_KEY(key, expected_utf8, expected_utf32) do { \
    assert(xkb_state_key_get_utf8(state, (key) + EVDEV_OFFSET, NULL, 0) == strlen(expected_utf8)); \
    assert(xkb_state_key_get_utf8(state, (key) + EVDEV_OFFSET, buf, sizeof(buf)) == strlen(expected_utf8)); \
    assert(memcmp(buf, expected_utf8, sizeof(expected_utf8)) == 0); \
    assert(xkb_state_key_get_utf32(state, (key) + EVDEV_OFFSET) == (expected_utf32)); \
} while (0)

    /* Simple ASCII. */
    TEST_KEY(KEY_A, "a", 0x61);
    TEST_KEY(KEY_ESC, "\x1B", 0x1B);
    TEST_KEY(KEY_1, "1", 0x31);

    /* Invalid. */
    TEST_KEY(XKB_KEYCODE_INVALID - 8, "", 0);
    TEST_KEY(300, "", 0);

    /* No string. */
    TEST_KEY(KEY_LEFTCTRL, "", 0);
    TEST_KEY(KEY_NUMLOCK, "", 0);

    /* Multiple keysyms. */
    TEST_KEY(KEY_6, "HELLO", 0);
    TEST_KEY(KEY_7, "YES THIS IS DOG", 0);

    /* Check truncation. */
    memset(buf, 'X', sizeof(buf));
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 0) == strlen("HELLO"));
    assert(memcmp(buf, "X", 1) == 0);
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 1) == strlen("HELLO"));
    assert(memcmp(buf, "", 1) == 0);
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 2) == strlen("HELLO"));
    assert(memcmp(buf, "H", 2) == 0);
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 3) == strlen("HELLO"));
    assert(memcmp(buf, "HE", 3) == 0);
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 5) == strlen("HELLO"));
    assert(memcmp(buf, "HELL", 5) == 0);
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 6) == strlen("HELLO"));
    assert(memcmp(buf, "HELLO", 6) == 0);
    assert(xkb_state_key_get_utf8(state, KEY_6 + EVDEV_OFFSET, buf, 7) == strlen("HELLO"));
    assert(memcmp(buf, "HELLO\0X", 7) == 0);

    /* Switch to ru layout */
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_key_get_layout(state, KEY_A + EVDEV_OFFSET) == 1);

    /* Non ASCII. */
    TEST_KEY(KEY_ESC, "\x1B", 0x1B);
    TEST_KEY(KEY_A, "ф", 0x0444);
    TEST_KEY(KEY_Z, "я", 0x044F);

    /* Switch back to us layout */
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_key_get_layout(state, KEY_A + EVDEV_OFFSET) == 0);

    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    TEST_KEY(KEY_A, "A", 0x41);
    TEST_KEY(KEY_ESC, "\x1B", 0x1B);
    TEST_KEY(KEY_1, "!", 0x21);
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);

    TEST_KEY(KEY_6, "HELLO", 0);
    TEST_KEY(KEY_7, "YES THIS IS DOG", 0);

    xkb_state_unref(state);
}

static void
test_ctrl_string_transformation(struct xkb_keymap *keymap)
{
    char buf[256];
    xkb_mod_index_t ctrl = _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);

    /* See xkb_state_key_get_utf8() for what's this all about. */


    /* First without. */
    TEST_KEY(KEY_A, "a", 0x61);
    TEST_KEY(KEY_B, "b", 0x62);
    TEST_KEY(KEY_C, "c", 0x63);
    TEST_KEY(KEY_ESC, "\x1B", 0x1B);
    TEST_KEY(KEY_1, "1", 0x31);

    /* And with. */
    xkb_state_update_key(state, KEY_RIGHTCTRL + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_mod_index_is_active(state, ctrl, XKB_STATE_MODS_EFFECTIVE) > 0);
    TEST_KEY(KEY_A, "\x01", 0x01);
    TEST_KEY(KEY_B, "\x02", 0x02);
    TEST_KEY(KEY_C, "\x03", 0x03);
    TEST_KEY(KEY_ESC, "\x1B", 0x1B);
    TEST_KEY(KEY_1, "1", 0x31);
    xkb_state_update_key(state, KEY_RIGHTCTRL + EVDEV_OFFSET, XKB_KEY_UP);

    /* Switch to ru layout */
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_key_get_layout(state, KEY_A + EVDEV_OFFSET) == 1);

    /* Non ASCII. */
    xkb_state_update_key(state, KEY_RIGHTCTRL + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_mod_index_is_active(state, ctrl, XKB_STATE_MODS_EFFECTIVE) > 0);
    TEST_KEY(KEY_A, "\x01", 0x01);
    TEST_KEY(KEY_B, "\x02", 0x02);
    xkb_state_update_key(state, KEY_RIGHTCTRL + EVDEV_OFFSET, XKB_KEY_UP);

    xkb_state_unref(state);
}

static bool
test_active_leds(struct xkb_state *state, xkb_led_mask_t leds_expected)
{
    struct xkb_keymap *keymap = xkb_state_get_keymap(state);
    bool ret = true;
    xkb_led_mask_t leds_got = 0;
    for (xkb_led_index_t led = 0; led < xkb_keymap_num_leds(keymap); led++) {
        const int status = xkb_state_led_index_is_active(state, led);
        if (status < 0)
            continue;
        const xkb_led_mask_t mask = (UINT32_C(1) << led);
        const bool expected = !!(leds_expected & mask);
        if (status)
            leds_got |= mask;
        if (!!status ^ expected) {
            fprintf(stderr, "ERROR: LED \"%s\" status: expected %d, got %d\n",
                    xkb_keymap_led_get_name(keymap, led), expected, !!status);
            ret = false;
        }
    }
    if (!ret) {
        fprintf(stderr, "ERROR: LEDs: expected 0x%x, got 0x%x\n",
                leds_expected, leds_got);
    }
    return ret;
}

static void
test_leds(struct xkb_context *ctx)
{
    const char buf[] =
        "xkb_keymap {\n"
        "    xkb_keycodes { include \"evdev\" };\n"
        "    xkb_types { include \"basic\" };\n"
        "    xkb_compat {\n"
        "        include \"leds(groups)\"\n"
        "        interpret ISO_Group_Shift { action= SetGroup(group=+1); };\n"
        "        interpret ISO_Group_Latch { action= LatchGroup(group=+1); };\n"
        "        interpret ISO_Group_Lock  { action= LockGroup(group=+1); };\n"
        "    };\n"
        "    xkb_symbols {\n"
        "        key <AD01> { [ q, Q ], [w, W], [e, E] };\n"
        "        key <LFSH> { [ ISO_Group_Shift ] };\n"
        "        key <MENU> { [ ISO_Group_Latch ] };\n"
        "        key <CAPS> { [ ISO_Group_Lock ] };\n"
        "    };\n"
        "};";

    struct xkb_keymap *keymap = test_compile_buffer(ctx, buf, ARRAY_SIZE(buf));
    assert(keymap);

    const xkb_led_index_t caps_idx = _xkb_keymap_led_get_index(keymap, XKB_LED_NAME_CAPS);
    const xkb_led_index_t num_idx = _xkb_keymap_led_get_index(keymap, XKB_LED_NAME_NUM);
    const xkb_led_index_t scroll_idx = _xkb_keymap_led_get_index(keymap, XKB_LED_NAME_SCROLL);
    const xkb_led_index_t compose_idx = _xkb_keymap_led_get_index(keymap, XKB_LED_NAME_COMPOSE);
    const xkb_led_index_t sleep_idx = _xkb_keymap_led_get_index(keymap, "Sleep");
    const xkb_led_index_t mute_idx = _xkb_keymap_led_get_index(keymap, "Mute");
    const xkb_led_index_t misc_idx = _xkb_keymap_led_get_index(keymap, "Misc");
    const xkb_led_index_t mail_idx = _xkb_keymap_led_get_index(keymap, "Mail");
    const xkb_led_index_t charging_idx = _xkb_keymap_led_get_index(keymap, "Charging");

    const xkb_led_mask_t caps = UINT32_C(1) << caps_idx;
    const xkb_led_mask_t num = UINT32_C(1) << num_idx;
    const xkb_led_mask_t scroll = UINT32_C(1) << scroll_idx;
    const xkb_led_mask_t compose = UINT32_C(1) << compose_idx;
    const xkb_led_mask_t sleep = UINT32_C(1) << sleep_idx;
    const xkb_led_mask_t mute = UINT32_C(1) << mute_idx;
    const xkb_led_mask_t misc = UINT32_C(1) << misc_idx;
    const xkb_led_mask_t mail = UINT32_C(1) << mail_idx;
    const xkb_led_mask_t charging = UINT32_C(1) << charging_idx;

    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);

    xkb_state_update_key(state, KEY_Q + EVDEV_OFFSET, XKB_KEY_UP);
    assert(test_active_leds(state, (caps | scroll)));

    /* SetGroup */
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) == 0x1);
    assert(test_active_leds(state, (num | scroll | mute | misc)));
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);

    /* LatchGroup */
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_COMPOSE + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) == 0x1);
    assert(test_active_leds(state, (caps | compose | mute | misc | charging)));
    xkb_state_update_key(state, KEY_Q + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_Q + EVDEV_OFFSET, XKB_KEY_UP);

    /* LockGroup 2 */
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) == 0x1);
    assert(test_active_leds(state, (caps | scroll | sleep | mute | mail)));

    /* LockGroup 2 + SetGroup */
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) == 0x2);
    assert(test_active_leds(state, (num | scroll | sleep | mute | misc | mail | charging)));
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);

    /* LockGroup 3 */
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_DOWN);
    xkb_state_update_key(state, KEY_CAPSLOCK + EVDEV_OFFSET, XKB_KEY_UP);
    assert(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) == 0x2);
    assert(test_active_leds(state, (caps | scroll | sleep | mute | charging)));

    /* LockGroup 3 + SetGroup */
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_DOWN);
    assert(xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) == 0x0);
    assert(test_active_leds(state, (num | scroll | sleep | misc | charging)));
    xkb_state_update_key(state, KEY_LEFTSHIFT + EVDEV_OFFSET, XKB_KEY_UP);

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
}

static void
test_multiple_actions(struct xkb_context *ctx)
{
    /* Check that we can trigger 2 actions on the same levels, with both
     * explicit (defined via the key statement) and explicit (defined via
     * interpret). The actions set the Control modifier and may change the
     * group. The idea is to enable keyboard shortcuts to always target the
     * same layout. Because SetGroup() does not work well with absolute values,
     * we define the modifiers on each 2 groups. */
    const char keymap_str[] =
        "xkb_keymap {\n"
        "  xkb_keycodes {\n"
        "    <AD01> = 24;\n"
	    "    <LCTL> = 37;\n"
	    "    <RCTL> = 105;\n"
        "  };\n"
        "  xkb_compat {\n"
        /* Right Control has its actions set implicitly via interpret */
        "    interpret 1 {\n"
        "      action = {SetMods(modifiers=Control)};\n"
        "    };\n"
        "    interpret 2 {\n"
        "      action = {SetMods(modifiers=Control), SetGroup(group=-1)};\n"
        "    };\n"
        "    interpret 3 {\n"
        "      action = {SetMods(modifiers=Control), SetGroup(group=-2)};\n"
        "    };\n"
        "    interpret 4 {\n"
        "      action = {SetMods(modifiers=Control), SetGroup(group=-3)};\n"
        "    };\n"
        "  };\n"
        "  xkb_symbols {\n"
        "    key <AD01> { [q], [Arabic_dad], [c_h], [Thai_maiyamok] };\n"
        /* Left Control has its actions set explicitly */
        "    key <LCTL> {\n"
        "      symbols[1] = [Control_L],\n"
        "      actions[1] = [{SetMods(modifiers=Control)}],\n"
        "      actions[2] = [{SetMods(modifiers=Control), SetGroup(group=-1)}],\n"
        "      actions[3] = [{SetMods(modifiers=Control), SetGroup(group=-2)}],\n"
        "      actions[4] = [{SetMods(modifiers=Control), SetGroup(group=-3)}]\n"
        "    };\n"
        "    key <RCTL> { [1], [2], [3], [4] };\n"
        "  };\n"
        "};";
    struct xkb_keymap *keymap = test_compile_buffer(ctx, keymap_str,
                                                    sizeof(keymap_str));
    assert(keymap);
    struct xkb_state *state = xkb_state_new(keymap);
    assert(state);
    const xkb_mod_index_t ctrl_idx =
        _xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    const xkb_mod_mask_t ctrl = UINT32_C(1) << ctrl_idx;
    const xkb_keycode_t lcontrol = KEY_LEFTCTRL + EVDEV_OFFSET;
    const xkb_keycode_t rcontrol = KEY_RIGHTCTRL + EVDEV_OFFSET;
    const xkb_keycode_t q = KEY_Q + EVDEV_OFFSET;
    xkb_mod_mask_t mods;

    const xkb_keycode_t mod_keys[] = {lcontrol, rcontrol};
    const xkb_keysym_t ad01[] = {
        XKB_KEY_q,
        XKB_KEY_Arabic_dad,
        XKB_KEY_c_h,
        XKB_KEY_Thai_maiyamok
    };

    for (xkb_layout_index_t layout = 0; layout < ARRAY_SIZE(ad01); layout++) {
        /* Lock layout */
        xkb_state_update_mask(state, 0, 0, 0, 0, 0, layout);
        assert(xkb_state_key_get_layout(state, q) == layout);
        assert(xkb_state_key_get_one_sym(state, q) == ad01[layout]);
        for (unsigned k = 0; k < ARRAY_SIZE(mod_keys); k++) {
            /* Temporarily switch to first layout + set Control modifier */
            xkb_state_update_key(state, mod_keys[k], XKB_KEY_DOWN);
            assert(xkb_state_key_get_layout(state, q) == 0);
            mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
            assert(mods == ctrl);
            assert(xkb_state_key_get_one_sym(state, q) == XKB_KEY_q);
            /* Restore layout, unset Control */
            xkb_state_update_key(state, mod_keys[k], XKB_KEY_UP);
            assert(xkb_state_key_get_layout(state, q) == layout);
            mods = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);
            assert(mods == 0);
            assert(xkb_state_key_get_one_sym(state, q) == ad01[layout]);
        }
    }

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
}

int
main(void)
{
    test_init();

    struct xkb_context *context = test_get_context(CONTEXT_NO_FLAG);
    struct xkb_keymap *keymap;

    assert(context);

    /* Make sure these are allowed. */
    xkb_context_unref(NULL);
    xkb_keymap_unref(NULL);
    xkb_state_unref(NULL);

    test_group_wrap(context);

    keymap = test_compile_rules(context, "evdev", "pc104", "us,ru", NULL,
                                "grp:menu_toggle");
    assert(keymap);

    test_update_key(keymap);
    test_serialisation(keymap);
    test_update_mask_mods(keymap);
    test_repeat(keymap);
    test_consume(keymap);
    test_range(keymap);
    test_get_utf8_utf32(keymap);
    test_ctrl_string_transformation(keymap);
    test_overlapping_mods(context);

    xkb_keymap_unref(keymap);

    test_caps_keysym_transformation(context);

    test_leds(context);
    test_multiple_actions(context);

    xkb_context_unref(context);
}
