#include "kg.h"
#include <dlfcn.h>

int
kg_cmd_load(KgContext *ctx)
{
    if (ctx->argc < 1) {
        g_printerr("usage: kedge load <payload.dylib> [entry]\n");
        return 1;
    }

    const gchar *path = ctx->argv[0];
    const gchar *entry = (ctx->argc >= 2) ? ctx->argv[1] : "init";

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        g_printerr("dlopen failed: %s\n", dlerror());
        return 1;
    }

    g_print("[*] loaded %s (handle=%p)\n", path, handle);

    if (entry != NULL) {
        void (*fn)(void) = (void (*)(void)) dlsym(handle, entry);
        if (fn == NULL) {
            g_printerr("symbol '%s' not found\n", entry);
            dlclose(handle);
            return 1;
        }
        g_print("[*] calling %s()\n", entry);
        fn();
        g_print("[*] %s() returned\n", entry);
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_print("[*] payload active, Ctrl-C to stop\n");
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    dlclose(handle);
    return 0;
}