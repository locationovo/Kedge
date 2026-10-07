#include "kg.h"

typedef struct {
    GumMatchPattern *pattern;
    gint count;
} KgScanCtx;

static gboolean
kg_on_match(GumAddress address, gsize size, gpointer user_data)
{
    KgScanCtx *sc = user_data;
    g_print("[match] %p  (%zu bytes)\n",
            GSIZE_TO_POINTER(address), size);
    sc->count++;
    return TRUE;
}

static gboolean
kg_scan_range(const GumRangeDetails *d, gpointer user_data)
{
    KgScanCtx *sc = user_data;
    if (!(d->protection & GUM_PAGE_READ))
        return TRUE;
    if (d->protection & GUM_PAGE_EXECUTE)
        return TRUE;
    gum_memory_scan(d->range, sc->pattern, kg_on_match, sc);
    return TRUE;
}

int
kg_cmd_scan(KgContext *ctx)
{
    if (ctx->argc < 2) {
        g_printerr("usage: kedge scan <module:NAME|all> <pattern>\n");
        return 1;
    }

    KgScanCtx sc = {0};
    sc.pattern = gum_match_pattern_new_from_string(ctx->argv[1]);
    if (sc.pattern == NULL) {
        g_printerr("invalid pattern\n");
        return 1;
    }

    if (g_str_has_prefix(ctx->argv[0], "module:")) {
        const gchar *modname = ctx->argv[0] + 7;
        GumModule *m = gum_process_find_module_by_name(modname);
        if (m == NULL) {
            g_printerr("module not found\n");
            gum_match_pattern_unref(sc.pattern);
            return 1;
        }
        const GumMemoryRange *range = gum_module_get_range(m);
        if (range != NULL)
            gum_memory_scan(range, sc.pattern, kg_on_match, &sc);
        g_object_unref(m);
    } else if (g_strcmp0(ctx->argv[0], "all") == 0) {
        gum_process_enumerate_ranges(GUM_PAGE_READ,
                                     (GumFoundRangeFunc) kg_scan_range, &sc);
    } else {
        g_printerr("invalid target\n");
        gum_match_pattern_unref(sc.pattern);
        return 1;
    }

    g_print("[*] %d matches\n", sc.count);
    gum_match_pattern_unref(sc.pattern);
    return 0;
}

typedef struct {
    guint8 *data;
    gsize size;
} KgPatchData;

static void
kg_apply_patch(gpointer mem, gpointer user_data)
{
    KgPatchData *pd = user_data;
    memcpy(mem, pd->data, pd->size);
}

int
kg_cmd_patch(KgContext *ctx)
{
    if (ctx->argc < 2) {
        g_printerr("usage: kedge patch <addr-hex> <hex-bytes>\n");
        return 1;
    }

    gpointer target = GSIZE_TO_POINTER(
        g_ascii_strtoull(ctx->argv[0], NULL, 16));

    if (!gum_memory_is_readable(target, 1)) {
        g_printerr("address %p is not readable, refusing to patch\n", target);
        return 1;
    }

    GumPageProtection prot = 0;
    if (!gum_memory_query_protection(target, &prot)) {
        g_printerr("cannot query protection at %p\n", target);
        return 1;
    }
    g_print("[*] target %p protection=0x%x\n", target, prot);

    const gchar *hex = ctx->argv[1];
    gsize len = strlen(hex) / 2;
    if (len == 0) {
        g_printerr("empty patch\n");
        return 1;
    }

    KgPatchData pd = {0};
    pd.data = g_malloc(len);
    pd.size = len;
    for (gsize i = 0; i < len; i++) {
        guint byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) {
            g_printerr("invalid hex\n");
            g_free(pd.data);
            return 1;
        }
        pd.data[i] = (guint8) byte;
    }

    gboolean ok = gum_memory_patch_code(target, len, kg_apply_patch, &pd);
    if (!ok) {
        g_printerr("patch failed\n");
        g_free(pd.data);
        return 1;
    }

    g_print("[*] patched %zu bytes at %p\n", len, target);
    g_free(pd.data);
    return 0;
}