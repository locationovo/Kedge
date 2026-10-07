#include "kg.h"
#include <dlfcn.h>

static gchar *
kg_find_jbroot(void)
{
    const gchar *bases[] = {
        "/private/var/containers/Bundle/Application",
        "/var/containers/Bundle/Application",
        NULL
    };

    for (int i = 0; bases[i]; i++) {
        GDir *dir = g_dir_open(bases[i], 0, NULL);
        if (dir == NULL)
            continue;
        const gchar *name;
        gchar *found = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_prefix(name, ".jbroot-")) {
                found = g_strdup_printf("%s/%s", bases[i], name);
                break;
            }
        }
        g_dir_close(dir);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static gchar *
kg_resolve_jbroot_path(const gchar *path)
{
    if (!g_str_has_prefix(path, "/var/jb/"))
        return g_strdup(path);

    gchar *jbroot = kg_find_jbroot();
    if (jbroot == NULL)
        return g_strdup(path);

    gchar *resolved = g_strdup_printf("%s%s", jbroot, path + 7);
    g_free(jbroot);
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
    fflush(stdout);

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
        fflush(stdout);
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