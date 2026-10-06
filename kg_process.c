#include "kg.h"

static void
kg_print_module(const GumModule *m, gpointer user_data)
{
    GumModule *module = (GumModule *) m;
    const GumMemoryRange *range = gum_module_get_range(module);
    const gchar *path = gum_module_get_path(module);
    const gchar *name = gum_module_get_name(module);

    if (range != NULL) {
        g_print("  %p-%p  %s\n",
                GSIZE_TO_POINTER(range->base_address),
                GSIZE_TO_POINTER(range->base_address + range->size),
                path ? path : (name ? name : "?"));
    }
}

static void
kg_print_thread(const GumThreadDetails *d, gpointer user_data)
{
    const gchar *state = "?";
    switch (d->state) {
        case GUM_THREAD_RUNNING: state = "running"; break;
        case GUM_THREAD_STOPPED: state = "stopped"; break;
        case GUM_THREAD_WAITING: state = "waiting"; break;
        case GUM_THREAD_UNINTERRUPTIBLE: state = "uninterruptible"; break;
        case GUM_THREAD_HALTED: state = "halted"; break;
    }
    g_print("  tid=%-8lu state=%-15s name=%s\n",
            (unsigned long) d->id, state, d->name ? d->name : "-");
}

int
kg_cmd_ps(KgContext *ctx)
{
    g_print("PID\tNAME\n");
    g_print("%d\t%s\n", gum_process_get_id(), "self");

    if (ctx->argc >= 1 && g_strcmp0(ctx->argv[0], "-m") == 0)
        gum_process_enumerate_modules((GumFoundModuleFunc) kg_print_module, NULL);

    if (ctx->argc >= 1 && g_strcmp0(ctx->argv[0], "-t") == 0)
        gum_process_enumerate_threads((GumFoundThreadFunc) kg_print_thread,
                                      NULL, GUM_THREAD_FLAGS_ALL);

    return 0;
}

int
kg_cmd_modules(KgContext *ctx)
{
    gum_process_enumerate_modules((GumFoundModuleFunc) kg_print_module, NULL);
    return 0;
}

int
kg_cmd_threads(KgContext *ctx)
{
    gum_process_enumerate_threads((GumFoundThreadFunc) kg_print_thread,
                                  NULL, GUM_THREAD_FLAGS_ALL);
    return 0;
}

int
kg_cmd_kill(KgContext *ctx)
{
    if (ctx->argc < 1) {
        g_printerr("usage: kedge kill <pid>\n");
        return 1;
    }
    pid_t pid = (pid_t) atoi(ctx->argv[0]);
    if (pid <= 0) {
        g_printerr("invalid pid\n");
        return 1;
    }
    if (kill(pid, SIGKILL) != 0) {
        g_printerr("kill failed: %s\n", strerror(errno));
        return 1;
    }
    g_print("killed %d\n", pid);
    return 0;
}