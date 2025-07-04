/*
 * Copyright © 2009 Dan Nicholson
 * SPDX-License-Identifier: MIT
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "xkbcommon/xkbcommon.h"
#include "test.h"
#include "test/utils-text.h"
#include "utils.h"

static void
test_explicit_actions(struct xkb_context *ctx)
{
    struct xkb_keymap *keymap;

    char *original = test_read_file("keymaps/explicit-actions.xkb");
    assert(original);
    char *tmp = uncomment(original, strlen(original), "//");
    assert(tmp);
    char *expected = strip_lines(tmp, strlen(tmp), "//");
    free(tmp);
    assert(expected);
    char *got = NULL;

    /* Try original */
    keymap = test_compile_string(ctx, XKB_KEYMAP_FORMAT_TEXT_V1, original);
    free(original);
    assert(keymap);
    got = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_USE_ORIGINAL_FORMAT);
    xkb_keymap_unref(keymap);
    assert_streq_not_null("Check output from original", expected, got);
    free(got);

    /* Try round-trip */
    keymap = test_compile_string(ctx, XKB_KEYMAP_FORMAT_TEXT_V1, expected);
    assert(keymap);
    got = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_USE_ORIGINAL_FORMAT);
    xkb_keymap_unref(keymap);
    assert_streq_not_null("Check roundtrip", expected, got);
    free(got);

    free(expected);
}

static struct xkb_keymap*
compile_string(struct xkb_context *context, enum xkb_keymap_format format,
               const char *buf, size_t len, void *private)
{
    return test_compile_string(context, format, buf);
}

int
main(int argc, char *argv[])
{
    test_init();

    bool update_output_files = false;
    if (argc > 1) {
        if (streq(argv[1], "update")) {
            /* Update files with *obtained* results */
            update_output_files = true;
        } else {
            fprintf(stderr, "ERROR: unsupported argument: \"%s\".\n", argv[1]);
            exit(EXIT_FAILURE);
        }
    }

    struct xkb_context *ctx = test_get_context(CONTEXT_NO_FLAG);
    struct xkb_keymap *keymap;
    char *dump, *dump2;

    assert(ctx);

    /* Make sure we can't (falsely claim to) compile an empty string. */
    keymap = test_compile_string(ctx, XKB_KEYMAP_FORMAT_TEXT_V1, "");
    assert(!keymap);

    /* Load in a prebuilt keymap, make sure we can compile it from a string,
     * then compare it to make sure we get the same result when dumping it
     * to a string. */
    static const struct {
        const char* path;
        enum xkb_keymap_format format;
    } data[] = {
        {
            .path = "keymaps/stringcomp-v1.data",
            .format = XKB_KEYMAP_FORMAT_TEXT_V1
        },
        {
            .path = "keymaps/stringcomp-v2.data",
            .format = XKB_KEYMAP_FORMAT_TEXT_V2
        },
    };
    for (unsigned int k = 0; k < ARRAY_SIZE(data); k++) {
        char *original = test_read_file(data[k].path);
        assert(original);
        assert(test_compile_output(ctx, data[k].format,
                                XKB_KEYMAP_USE_ORIGINAL_FORMAT,
                                compile_string, NULL, "Round-trip",
                                original, 0 /* unused */, data[k].path,
                                update_output_files));
        free(original);
    }

    /* Make sure we can recompile our output for a normal keymap from rules. */
    keymap = test_compile_rules(ctx, XKB_KEYMAP_FORMAT_TEXT_V1, NULL,
                                NULL, "ru,ca,de,us", ",multix,neo,intl", NULL);
    assert(keymap);
    dump = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_USE_ORIGINAL_FORMAT);
    assert(dump);
    xkb_keymap_unref(keymap);
    keymap = test_compile_string(ctx, XKB_KEYMAP_FORMAT_TEXT_V1, dump);
    assert(keymap);
    /* Now test that the dump of the dump is equal to the dump! */
    dump2 = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_USE_ORIGINAL_FORMAT);
    assert(dump2);
    assert(streq(dump, dump2));

    /* Test response to invalid formats and flags. */
    assert(!xkb_keymap_new_from_string(ctx, dump, 0, 0));
    assert(!xkb_keymap_new_from_string(ctx, dump, -1, 0));
    assert(!xkb_keymap_new_from_string(ctx, dump, XKB_KEYMAP_USE_ORIGINAL_FORMAT, 0));
    assert(!xkb_keymap_new_from_string(ctx, dump, XKB_KEYMAP_FORMAT_TEXT_V2+1, 0));
    assert(!xkb_keymap_new_from_string(ctx, dump, XKB_KEYMAP_FORMAT_TEXT_V1, -1));
    assert(!xkb_keymap_new_from_string(ctx, dump, XKB_KEYMAP_FORMAT_TEXT_V1, 1414));
    assert(!xkb_keymap_get_as_string(keymap, 0));
    assert(!xkb_keymap_get_as_string(keymap, 4893));

    xkb_keymap_unref(keymap);
    free(dump);
    free(dump2);

    test_explicit_actions(ctx);

    xkb_context_unref(ctx);

    return EXIT_SUCCESS;
}
