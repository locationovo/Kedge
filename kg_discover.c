#include "kg.h"

typedef gboolean (* KgFnMatchFunc) (const GumFunctionDetails * details,
                                     gpointer user_data);

typedef void (* KgFindFunctionsMatchingFn) (const gchar * str,
                                            KgFnMatchFunc func,
                                            gpointer user_data);

static gboolean
kg_on_func(const GumFunctionDetails *d, gpointer user_data)
{
    g_print("%p  %s  (%d args)\n",
            GSIZE_TO_POINTER(d->address), d->name, d->num_arguments);
    return TRUE;
}

int
kg_cmd_discover(KgContext *ctx)
{
    if (ctx->argc < 1) {
        g_printerr("usage: kedge discover <pattern> [addr]\n");
        return 1;
    }

    KgFindFunctionsMatchingFn fn =
        (KgFindFunctionsMatchingFn) gum_find_functions_matching;
    fn(ctx->argv[0], kg_on_func, NULL);

    if (ctx->argc >= 2) {
        gpointer addr = GSIZE_TO_POINTER(
            g_ascii_strtoull(ctx->argv[1], NULL, 16));
        GumMemoryRange r;
        if (gum_process_find_function_range(addr, &r))
            g_print("function range: %p-%p\n",
                    GSIZE_TO_POINTER(r.base_address),
                    GSIZE_TO_POINTER(r.base_address + r.size));
        GumDebugSymbolDetails ds;
        if (gum_symbol_details_from_address(addr, &ds))
            g_print("symbol: %s  %s:%u\n",
                    ds.symbol_name, ds.file_name, ds.line_number);
    }

    return 0;
}