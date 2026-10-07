#include "kg.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>

static gchar *
kg_resolve_jbroot_path(const gchar *path)
{
    if (!g_str_has_prefix(path, "/var/jb/"))
        return g_strdup(path);

    char exe[4096];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0)
        return g_strdup(path);

    const gchar *marker = strstr(exe, "/.jbroot-");
    if (marker == NULL)
        return g_strdup(path);

    const gchar *after = strchr(marker + 1, '/');
    if (after == NULL)
        return g_strdup(path);

    gsize prefix_len = (gsize)(after - exe);
    gchar *resolved = g_strdup_printf("%.*s%s", (int) prefix_len, exe, path + 8);
    return resolved;
}

int
kg_cmd_load(KgContext *ctx)
{
    if (ctx->argc < 1) {
        g_printerr("usage: kedge load <payload.dylib> [entry]\n");
        return 1;
    }

    const gchar *raw_path = ctx->argv[0];
    const gchar *entry = (ctx->argc >= 2) ? ctx->argv[1] : "init";

    gchar *path = kg_resolve_jbroot_path(raw_path);
    g_print("[*] resolved path: %s\n", path);

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        g_printerr("dlopen failed: %s\n", dlerror());
        g_free(path);
        return 1;
    }

    g_print("[*] loaded %s (handle=%p)\n", path, handle);
    g_free(path);

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