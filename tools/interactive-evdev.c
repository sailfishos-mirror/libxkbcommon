/*
 * Copyright © 2012 Ran Benita <ran234@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "config.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>

#include "xkbcommon/xkbcommon.h"

#include "src/utils.h"
#include "src/keymap-formats.h"
#include "tools-common.h"

struct keyboard {
    char *path;
    int fd;
    struct xkb_state *state;
    struct xkb_compose_state *compose_state;
    struct keyboard *next;
};

static bool verbose = false;
static bool terminate;
static int evdev_offset = 8;
static bool report_state_changes;
static bool with_compose;
static enum xkb_consumed_mode consumed_mode = XKB_CONSUMED_MODE_XKB;

print_state_fields_mask_t print_fields = PRINT_ALL_FIELDS;

#define DEFAULT_INCLUDE_PATH_PLACEHOLDER "__defaults__"
#define NLONGS(n) (((n) + LONG_BIT - 1) / LONG_BIT)

static bool
evdev_bit_is_set(const unsigned long *array, int bit)
{
    return array[bit / LONG_BIT] & (1ULL << (bit % LONG_BIT));
}

/* Some heuristics to see if the device is a keyboard. */
static bool
is_keyboard(int fd)
{
    int i;
    unsigned long evbits[NLONGS(EV_CNT)] = { 0 };
    unsigned long keybits[NLONGS(KEY_CNT)] = { 0 };

    errno = 0;
    ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    if (errno)
        return false;

    if (!evdev_bit_is_set(evbits, EV_KEY))
        return false;

    errno = 0;
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    if (errno)
        return false;

    for (i = KEY_RESERVED; i <= KEY_MIN_INTERESTING; i++)
        if (evdev_bit_is_set(keybits, i))
            return true;

    return false;
}

static int
keyboard_new(struct dirent *ent, struct xkb_keymap *keymap,
             struct xkb_compose_table *compose_table, struct keyboard **out)
{
    int ret;
    char *path;
    int fd;
    struct xkb_state *state;
    struct xkb_compose_state *compose_state = NULL;
    struct keyboard *kbd;

    ret = asprintf(&path, "/dev/input/%s", ent->d_name);
    if (ret < 0)
        return -ENOMEM;

    fd = open(path, O_NONBLOCK | O_CLOEXEC | O_RDONLY);
    if (fd < 0) {
        ret = -errno;
        goto err_path;
    }

    if (!is_keyboard(fd)) {
        /* Dummy "skip this device" value. */
        ret = -ENOTSUP;
        goto err_fd;
    }

    state = xkb_state_new(keymap);
    if (!state) {
        fprintf(stderr, "Couldn't create xkb state for %s\n", path);
        ret = -EFAULT;
        goto err_fd;
    }

    if (with_compose) {
        compose_state = xkb_compose_state_new(compose_table,
                                              XKB_COMPOSE_STATE_NO_FLAGS);
        if (!compose_state) {
            fprintf(stderr, "Couldn't create compose state for %s\n", path);
            ret = -EFAULT;
            goto err_state;
        }
    }

    kbd = calloc(1, sizeof(*kbd));
    if (!kbd) {
        ret = -ENOMEM;
        goto err_compose_state;
    }

    kbd->path = path;
    kbd->fd = fd;
    kbd->state = state;
    kbd->compose_state = compose_state;
    *out = kbd;
    return 0;

err_compose_state:
    xkb_compose_state_unref(compose_state);
err_state:
    xkb_state_unref(state);
err_fd:
    close(fd);
err_path:
    free(path);
    return ret;
}

static void
keyboard_free(struct keyboard *kbd)
{
    if (!kbd)
        return;
    if (kbd->fd >= 0)
        close(kbd->fd);
    free(kbd->path);
    xkb_state_unref(kbd->state);
    xkb_compose_state_unref(kbd->compose_state);
    free(kbd);
}

static int
filter_device_name(const struct dirent *ent)
{
    return !fnmatch("event*", ent->d_name, 0);
}

static struct keyboard *
get_keyboards(struct xkb_keymap *keymap,
              struct xkb_compose_table *compose_table)
{
    int ret, i, nents;
    struct dirent **ents;
    struct keyboard *kbds = NULL, *kbd = NULL;

    nents = scandir("/dev/input", &ents, filter_device_name, alphasort);
    if (nents < 0) {
        fprintf(stderr, "Couldn't scan /dev/input: %s\n", strerror(errno));
        return NULL;
    }

    for (i = 0; i < nents; i++) {
        ret = keyboard_new(ents[i], keymap, compose_table, &kbd);
        if (ret) {
            if (ret == -EACCES) {
                fprintf(stderr, "Couldn't open /dev/input/%s: %s. "
                                "You probably need root to run this.\n",
                        ents[i]->d_name, strerror(-ret));
                break;
            }
            if (ret != -ENOTSUP) {
                fprintf(stderr, "Couldn't open /dev/input/%s: %s. Skipping.\n",
                        ents[i]->d_name, strerror(-ret));
            }
            continue;
        }

        assert(kbd != NULL);
        kbd->next = kbds;
        kbds = kbd;
    }

    if (!kbds) {
        fprintf(stderr, "Couldn't find any keyboards I can use! Quitting.\n");
        goto err;
    }

err:
    for (i = 0; i < nents; i++)
        free(ents[i]);
    free(ents);
    return kbds;
}

static void
free_keyboards(struct keyboard *kbds)
{
    struct keyboard *next;

    while (kbds) {
        next = kbds->next;
        keyboard_free(kbds);
        kbds = next;
    }
}

/* The meaning of the input_event 'value' field. */
enum {
    KEY_STATE_RELEASE = 0,
    KEY_STATE_PRESS = 1,
    KEY_STATE_REPEAT = 2,
};

static void
process_event(struct keyboard *kbd, uint16_t type, uint16_t code, int32_t value)
{
    xkb_keycode_t keycode;
    struct xkb_keymap *keymap;
    enum xkb_state_component changed;
    enum xkb_compose_status status;

    if (type != EV_KEY)
        return;

    keycode = evdev_offset + code;
    keymap = xkb_state_get_keymap(kbd->state);

    if (value == KEY_STATE_REPEAT && !xkb_keymap_key_repeats(keymap, keycode))
        return;

    if (with_compose && value != KEY_STATE_RELEASE) {
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(kbd->state, keycode);
        xkb_compose_state_feed(kbd->compose_state, keysym);
    }

    if (value != KEY_STATE_RELEASE) {
        tools_print_keycode_state(
            NULL, kbd->state, kbd->compose_state, keycode,
            consumed_mode, print_fields
        );
    }

    if (with_compose) {
        status = xkb_compose_state_get_status(kbd->compose_state);
        if (status == XKB_COMPOSE_CANCELLED || status == XKB_COMPOSE_COMPOSED)
            xkb_compose_state_reset(kbd->compose_state);
    }

    if (value == KEY_STATE_RELEASE)
        changed = xkb_state_update_key(kbd->state, keycode, XKB_KEY_UP);
    else
        changed = xkb_state_update_key(kbd->state, keycode, XKB_KEY_DOWN);

    if (report_state_changes)
        tools_print_state_changes(changed);
}

static int
read_keyboard(struct keyboard *kbd)
{
    ssize_t len;
    struct input_event evs[16];

    /* No fancy error checking here. */
    while ((len = read(kbd->fd, &evs, sizeof(evs))) > 0) {
        const size_t nevs = len / sizeof(struct input_event);
        for (size_t i = 0; i < nevs; i++)
            process_event(kbd, evs[i].type, evs[i].code, evs[i].value);
    }

    if (len < 0 && errno != EWOULDBLOCK) {
        fprintf(stderr, "Couldn't read %s: %s\n", kbd->path, strerror(errno));
        return 1;
    }

    return 0;
}

static int
loop(struct keyboard *kbds)
{
    int ret = -1;
    struct keyboard *kbd;
    nfds_t nfds, i;
    struct pollfd *fds = NULL;

    for (kbd = kbds, nfds = 0; kbd; kbd = kbd->next, nfds++) {}
    fds = calloc(nfds, sizeof(*fds));
    if (fds == NULL) {
        fprintf(stderr, "Out of memory");
        goto out;
    }

    for (i = 0, kbd = kbds; kbd; kbd = kbd->next, i++) {
        fds[i].fd = kbd->fd;
        fds[i].events = POLLIN;
    }

    while (!terminate) {
        ret = poll(fds, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "Couldn't poll for events: %s\n",
                    strerror(errno));
            goto out;
        }

        for (i = 0, kbd = kbds; kbd; kbd = kbd->next, i++) {
            if (fds[i].revents != 0) {
                ret = read_keyboard(kbd);
                if (ret) {
                    goto out;
                }
            }
        }
    }

    ret = 0;
out:
    free(fds);
    return ret;
}

static void
sigintr_handler(int signum)
{
    terminate = true;
}

static void
usage(FILE *fp, char *progname)
{
        fprintf(fp, "Usage: %s [--include=<path>] [--include-defaults] [--format=<format>]"
                "[--rules=<rules>] [--model=<model>] [--layout=<layout>] "
                "[--variant=<variant>] [--options=<options>] "
                "[--enable-environment-names]\n",
                progname);
        fprintf(fp, "   or: %s --keymap <path to keymap file>\n",
                progname);
        fprintf(fp, "For both:\n"
                        "          --format <FORMAT> (use keymap format FORMAT)\n"
                        "          --verbose (enable verbose debugging output)\n"
                        "          --short (do not print layout nor Unicode keysym translation)\n"
                        "          --report-state-changes (report changes to the state)\n"
                        "          --enable-compose (enable Compose)\n"
                        "          --consumed-mode={xkb|gtk} (select the consumed modifiers mode, default: xkb)\n"
                        "          --without-x11-offset (don't add X11 keycode offset)\n"
                    "Other:\n"
                        "          --help (display this help and exit)\n"
        );
}

int
main(int argc, char *argv[])
{
    int ret = EXIT_FAILURE;
    struct keyboard *kbds;
    struct xkb_context *ctx = NULL;
    struct xkb_keymap *keymap = NULL;
    struct xkb_compose_table *compose_table = NULL;
    const char *includes[64];
    size_t num_includes = 0;
    bool use_env_names = false;
    enum xkb_keymap_format keymap_format = DEFAULT_INPUT_KEYMAP_FORMAT;
    const char *rules = NULL;
    const char *model = NULL;
    const char *layout = NULL;
    const char *variant = NULL;
    const char *options = NULL;
    const char *keymap_path = NULL;
    const char *locale;
    struct sigaction act;
    enum options {
        OPT_VERBOSE,
        OPT_INCLUDE,
        OPT_INCLUDE_DEFAULTS,
        OPT_ENABLE_ENV_NAMES,
        OPT_KEYMAP_FORMAT,
        OPT_RULES,
        OPT_MODEL,
        OPT_LAYOUT,
        OPT_VARIANT,
        OPT_OPTION,
        OPT_KEYMAP,
        OPT_WITHOUT_X11_OFFSET,
        OPT_CONSUMED_MODE,
        OPT_COMPOSE,
        OPT_SHORT,
        OPT_REPORT_STATE,
    };
    static struct option opts[] = {
        {"help",                 no_argument,            0, 'h'},
        {"verbose",              no_argument,            0, OPT_VERBOSE},
        {"include",              required_argument,      0, OPT_INCLUDE},
        {"include-defaults",     no_argument,            0, OPT_INCLUDE_DEFAULTS},
        {"enable-environment-names", no_argument,        0, OPT_ENABLE_ENV_NAMES},
        {"format",               required_argument,      0, OPT_KEYMAP_FORMAT},
        {"rules",                required_argument,      0, OPT_RULES},
        {"model",                required_argument,      0, OPT_MODEL},
        {"layout",               required_argument,      0, OPT_LAYOUT},
        {"variant",              required_argument,      0, OPT_VARIANT},
        {"options",              required_argument,      0, OPT_OPTION},
        {"keymap",               required_argument,      0, OPT_KEYMAP},
        {"consumed-mode",        required_argument,      0, OPT_CONSUMED_MODE},
        {"enable-compose",       no_argument,            0, OPT_COMPOSE},
        {"short",                no_argument,            0, OPT_SHORT},
        {"report-state-changes", no_argument,            0, OPT_REPORT_STATE},
        {"without-x11-offset",   no_argument,            0, OPT_WITHOUT_X11_OFFSET},
        {0, 0, 0, 0},
    };

    setlocale(LC_ALL, "");

    bool has_rmlvo_options = false;
    while (1) {
        int option_index = 0;
        int opt = getopt_long(argc, argv, "h", opts, &option_index);
        if (opt == -1)
            break;

        switch (opt) {
        case OPT_VERBOSE:
            verbose = true;
            break;
        case OPT_INCLUDE:
            if (num_includes >= ARRAY_SIZE(includes))
                goto too_many_includes;
            includes[num_includes++] = optarg;
            break;
        case OPT_INCLUDE_DEFAULTS:
            if (num_includes >= ARRAY_SIZE(includes))
                goto too_many_includes;
            includes[num_includes++] = DEFAULT_INCLUDE_PATH_PLACEHOLDER;
            break;
        case OPT_ENABLE_ENV_NAMES:
            use_env_names = true;
            break;
        case OPT_KEYMAP_FORMAT:
            keymap_format = xkb_keymap_parse_format(optarg);
            if (!keymap_format) {
                fprintf(stderr, "ERROR: invalid --format \"%s\"\n", optarg);
                usage(stderr, argv[0]);
                return EXIT_INVALID_USAGE;
            }
            break;
        case OPT_RULES:
            if (keymap_path)
                goto input_format_error;
            rules = optarg;
            has_rmlvo_options = true;
            break;
        case OPT_MODEL:
            if (keymap_path)
                goto input_format_error;
            model = optarg;
            has_rmlvo_options = true;
            break;
        case OPT_LAYOUT:
            if (keymap_path)
                goto input_format_error;
            layout = optarg;
            has_rmlvo_options = true;
            break;
        case OPT_VARIANT:
            if (keymap_path)
                goto input_format_error;
            variant = optarg;
            has_rmlvo_options = true;
            break;
        case OPT_OPTION:
            if (keymap_path)
                goto input_format_error;
            options = optarg;
            has_rmlvo_options = true;
            break;
        case OPT_KEYMAP:
            if (has_rmlvo_options)
                goto input_format_error;
            keymap_path = optarg;
            break;
        case OPT_WITHOUT_X11_OFFSET:
            evdev_offset = 0;
            break;
        case OPT_REPORT_STATE:
            report_state_changes = true;
            break;
        case OPT_COMPOSE:
            with_compose = true;
            break;
        case OPT_SHORT:
            print_fields &= ~PRINT_VERBOSE_FIELDS;
            break;
        case OPT_CONSUMED_MODE:
            if (strcmp(optarg, "gtk") == 0) {
                consumed_mode = XKB_CONSUMED_MODE_GTK;
            } else if (strcmp(optarg, "xkb") == 0) {
                consumed_mode = XKB_CONSUMED_MODE_XKB;
            } else {
                fprintf(stderr, "ERROR: invalid --consumed-mode \"%s\"\n", optarg);
                usage(stderr, argv[0]);
                return EXIT_INVALID_USAGE;
            }
            break;
#ifdef ENABLE_PRIVATE_APIS
        case OPT_PRINT_MODMAPS:
            print_modmaps = true;
            break;
#endif
        case 'h':
            usage(stdout, argv[0]);
            return EXIT_SUCCESS;
        default:
            usage(stderr, argv[0]);
            return EXIT_INVALID_USAGE;
        }
    }

    if (optind < argc && !isempty(argv[optind])) {
        /* Some positional arguments left: use as a keymap input */
        if (keymap_path || has_rmlvo_options)
            goto too_much_arguments;
        keymap_path = argv[optind++];
        if (optind < argc) {
too_much_arguments:
            fprintf(stderr, "ERROR: Too much positional arguments\n");
            usage(stderr, argv[0]);
            exit(EXIT_INVALID_USAGE);
        }
    }

    enum xkb_context_flags ctx_flags = XKB_CONTEXT_NO_DEFAULT_INCLUDES;
    if (!use_env_names)
        ctx_flags |= XKB_CONTEXT_NO_ENVIRONMENT_NAMES;

    ctx = xkb_context_new(ctx_flags);
    if (!ctx) {
        fprintf(stderr, "ERROR: Couldn't create xkb context\n");
        goto out;
    }

    if (verbose) {
        xkb_context_set_log_level(ctx, XKB_LOG_LEVEL_DEBUG);
        xkb_context_set_log_verbosity(ctx, 10);
    }

    if (num_includes == 0)
        includes[num_includes++] = DEFAULT_INCLUDE_PATH_PLACEHOLDER;

    for (size_t i = 0; i < num_includes; i++) {
        const char *include = includes[i];
        if (strcmp(include, DEFAULT_INCLUDE_PATH_PLACEHOLDER) == 0)
            xkb_context_include_path_append_default(ctx);
        else
            xkb_context_include_path_append(ctx, include);
    }

    if (keymap_path) {
        FILE *file = fopen(keymap_path, "rb");
        if (!file) {
            fprintf(stderr, "ERROR: Couldn't open '%s': %s\n",
                    keymap_path, strerror(errno));
            goto out;
        }
        keymap = xkb_keymap_new_from_file(ctx, file, keymap_format,
                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
        fclose(file);
    }
    else {
        struct xkb_rule_names rmlvo = {
            .rules = (isempty(rules)) ? NULL : rules,
            .model = (isempty(model)) ? NULL : model,
            .layout = (isempty(layout)) ? NULL : layout,
            .variant = (isempty(variant)) ? NULL : variant,
            .options = (isempty(options)) ? NULL : options
        };

        if (!rules && !model && !layout && !variant && !options)
            keymap = xkb_keymap_new_from_names2(ctx, NULL, keymap_format,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
        else
            keymap = xkb_keymap_new_from_names2(ctx, &rmlvo, keymap_format,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);

        if (!keymap) {
            fprintf(stderr,
                    "ERROR: Failed to compile RMLVO: "
                    "'%s', '%s', '%s', '%s', '%s'\n",
                    rules, model, layout, variant, options);
            goto out;
        }
    }

    if (!keymap) {
        fprintf(stderr, "ERROR: Couldn't create xkb keymap\n");
        goto out;
    }

    if (with_compose) {
        locale = setlocale(LC_CTYPE, NULL);
        compose_table =
            xkb_compose_table_new_from_locale(ctx, locale,
                                              XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (!compose_table) {
            fprintf(stderr, "ERROR: Couldn't create compose from locale\n");
            goto out;
        }
    }

    kbds = get_keyboards(keymap, compose_table);
    if (!kbds) {
        goto out;
    }

#ifdef ENABLE_PRIVATE_APIS
    if (print_modmaps) {
        print_keys_modmaps(keymap);
        putchar('\n');
        print_modifiers_encodings(keymap);
        putchar('\n');
    }
#endif

    act.sa_handler = sigintr_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    tools_disable_stdin_echo();
    ret = loop(kbds);
    tools_enable_stdin_echo();

    free_keyboards(kbds);
out:
    xkb_compose_table_unref(compose_table);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    return ret;

too_many_includes:
    fprintf(stderr, "ERROR: too many includes (max: %zu)\n",
            ARRAY_SIZE(includes));
    exit(EXIT_INVALID_USAGE);

input_format_error:
    fprintf(stderr, "ERROR: Cannot use RMLVO options with keymap input\n");
    usage(stderr, argv[0]);
    exit(EXIT_INVALID_USAGE);
}
