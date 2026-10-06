#include "kg.h"

static void
kg_on_mem_access(GumMemoryAccessMonitor *monitor,
                 const GumMemoryAccessDetails *d,
                 gpointer user_data)
{
    const gchar *op = "?";
    switch (d->operation) {
        case GUM_MEMORY_OPERATION_OPEN:      op = "open";      break;
        case GUM_MEMORY_OPERATION_EXCLUSIVE: op = "exclusive"; break;
    }
    g_print("[mem] %s addr=%p from=%p tid=%lu page=%u/%u\n",
            op,
            GSIZE_TO_POINTER(d->address),
            GSIZE_TO_POINTER(d->from),
            (unsigned long) d->thread_id,
            d->page_index + 1,
            d->pages_total);
}

int
kg_cmd_watch(KgContext *ctx)
{
    if (ctx->argc < 2) {
        g_printerr("usage: kedge watch <addr-hex> <size-hex> [rw]\n");
        return 1;
    }

    GumMemoryRange range = {
        .base_address = g_ascii_strtoull(ctx->argv[0], NULL, 16),
        .size = g_ascii_strtoull(ctx->argv[1], NULL, 16),
    };

    GumPageProtection mask = GUM_PAGE_READ | GUM_PAGE_WRITE;
    if (ctx->argc >= 3) {
        mask = 0;
        if (strchr(ctx->argv[2], 'r'))
            mask |= GUM_PAGE_READ;
        if (strchr(ctx->argv[2], 'w'))
            mask |= GUM_PAGE_WRITE;
    }

    GumMemoryAccessMonitor *mon = gum_memory_access_monitor_new(
        &range, 1, mask, TRUE, kg_on_mem_access, NULL, NULL);

    GError *error = NULL;
    if (!gum_memory_access_monitor_enable(mon, &error)) {
        g_printerr("enable failed: %s\n", error->message);
        g_error_free(error);
        g_object_unref(mon);
        return 1;
    }

    g_print("[*] watching %p + 0x%llx, Ctrl-C to stop\n",
            GSIZE_TO_POINTER(range.base_address),
            (unsigned long long) range.size);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    gum_memory_access_monitor_disable(mon);
    g_object_unref(mon);
    g_main_loop_unref(loop);
    return 0;
}