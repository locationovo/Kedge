#include "kg.h"

int
kg_cmd_bt(KgContext *ctx)
{
    if (ctx->argc < 1) {
        g_printerr("usage: kedge bt <addr-hex>\n");
        return 1;
    }

    gpointer addr = GSIZE_TO_POINTER(
        g_ascii_strtoull(ctx->argv[0], NULL, 16));

    gchar *name = gum_symbol_name_from_address(addr);
    if (name != NULL) {
        g_print("symbol: %s\n", name);
        g_free(name);
    }

    GumReturnAddressDetails rad;
    if (gum_return_address_details_from_address(addr, &rad))
        g_print("%s  %s:%u\n", rad.function_name,
                rad.file_name, rad.line_number);

    return 0;
}