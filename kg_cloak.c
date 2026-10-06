#include "kg.h"

static gboolean
kg_cloak_range(const GumRangeDetails *d, gpointer user_data)
{
    if (d->file != NULL &&
        g_strstr_len(d->file->path, -1, "kedge") != NULL) {
        gum_cloak_add_range(d->range);
        return TRUE;
    }
    if (d->file == NULL && (d->protection & GUM_PAGE_EXECUTE))
        gum_cloak_add_range(d->range);
    return TRUE;
}

int
kg_cmd_cloak(KgContext *ctx)
{
    gum_process_enumerate_ranges((GumPageProtection) 0,
                                 (GumFoundRangeFunc) kg_cloak_range, NULL);
    gum_cloak_add_thread(gum_process_get_current_thread_id());
    gum_cloak_add_file_descriptor(3);
    g_print("[*] cloak engaged\n");
    return 0;
}