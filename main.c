#include "kg.h"

typedef struct {
    const char *name;
    int (*fn)(KgContext *);
} KgCmd;

static const KgCmd cmds[] = {
    { "ps", kg_cmd_ps },
    { "trace", kg_cmd_trace },
    { "stalker", kg_cmd_stalker },
    { "discover", kg_cmd_discover },
    { "scan", kg_cmd_scan },
    { "patch", kg_cmd_patch },
    { "modules", kg_cmd_modules },
    { "threads", kg_cmd_threads },
    { "kill", kg_cmd_kill },
    { "except", kg_cmd_except },
    { "cloak", kg_cmd_cloak },
    { "watch", kg_cmd_watch },
    { "bt", kg_cmd_bt },
    { "load", kg_cmd_load },
    { "inject", kg_cmd_inject },
    { "repl", kg_cmd_repl },
    { NULL, NULL }
};

int
main(int argc, char **argv)
{
    gum_init_embedded();

    if (argc < 2) {
        g_printerr("usage: kedge <");
        for (const KgCmd *c = cmds; c->name; c++)
            g_printerr("%s ", c->name);
        g_printerr(">\n");
        gum_deinit_embedded();
        return 1;
    }

    KgContext ctx = { .argc = argc - 2, .argv = argv + 2 };
    int ret = 1;
    gboolean found = FALSE;

    for (const KgCmd *c = cmds; c->name; c++) {
        if (g_strcmp0(argv[1], c->name) == 0) {
            ret = c->fn(&ctx);
            found = TRUE;
            break;
        }
    }

    if (!found) {
        g_printerr("unknown command: %s\n", argv[1]);
        ret = 1;
    }

    gum_deinit_embedded();
    return ret;
}