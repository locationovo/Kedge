#include <frida-gum.h>
#include <frida-gumjs.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

typedef char *(*readline_fn)(const char *);
typedef void (*add_history_fn)(const char *);
typedef void (*using_history_fn)(void);

static readline_fn p_readline = NULL;
static add_history_fn p_add_history = NULL;
static using_history_fn p_using_history = NULL;

static void
kg_load_readline(void)
{
    void *h = dlopen("/var/jb/usr/lib/libreadline.dylib", RTLD_LAZY);
    if (h == NULL)
        h = dlopen("/usr/lib/libreadline.dylib", RTLD_LAZY);
    if (h == NULL)
        h = dlopen("libreadline.dylib", RTLD_LAZY);
    if (h == NULL)
        return;
    p_readline = (readline_fn) dlsym(h, "readline");
    p_add_history = (add_history_fn) dlsym(h, "add_history");
    p_using_history = (using_history_fn) dlsym(h, "using_history");
}

static void
on_message(GumScript *script, const gchar *message, GBytes *data,
           gpointer user_data)
{
    gchar *p = strstr(message, "\"payload\":\"");
    if (p != NULL) {
        p += 11;
        gchar *end = strchr(p, '"');
        if (end != NULL) {
            gchar *payload = g_strndup(p, end - p);
            g_print("%s\n", payload);
            g_free(payload);
            return;
        }
    }
    g_print("%s\n", message);
}

static void
on_error(GumScript *script, GError *error, gpointer user_data)
{
    g_printerr("[error] %s\n", error->message);
}

int
main(int argc, char **argv)
{
    gum_init_embedded();
    kg_load_readline();

    GumScriptBackend *backend = gum_script_backend_obtain_qjs();
    if (backend == NULL) {
        g_printerr("QuickJS backend not available\n");
        return 1;
    }

    if (p_using_history != NULL)
        p_using_history();

    g_print("[*] kedge-repl ready. Type 'exit' to quit.\n");

    while (1) {
        char *line = NULL;
        if (p_readline != NULL) {
            line = p_readline("kg> ");
        } else {
            g_print("kg> ");
            fflush(stdout);
            char buf[4096];
            if (fgets(buf, sizeof(buf), stdin) == NULL)
                break;
            size_t n = strlen(buf);
            if (n > 0 && buf[n - 1] == '\n')
                buf[n - 1] = '\0';
            line = g_strdup(buf);
        }
        if (line == NULL)
            break;
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            g_free(line);
            break;
        }
        if (strlen(line) == 0) {
            g_free(line);
            continue;
        }
        if (p_add_history != NULL)
            p_add_history(line);

        gchar *wrapped = g_strdup_printf(
            "(function(){try{var __r=eval(%s);"
            "if(__r!==undefined)console.log(JSON.stringify(__r));"
            "}catch(e){console.log('Error: '+e);}})();",
            line);
        g_free(line);

        GError *error = NULL;
        GumScript *script = gum_script_backend_create_sync(
            backend, "repl", wrapped, NULL, NULL, &error);
        g_free(wrapped);

        if (script == NULL) {
            g_printerr("create failed: %s\n",
                       error ? error->message : "unknown");
            if (error) {
                g_error_free(error);
                error = NULL;
            }
            continue;
        }

        g_signal_connect(script, "message", G_CALLBACK(on_message), NULL);
        g_signal_connect(script, "error", G_CALLBACK(on_error), NULL);
        gum_script_load_sync(script, NULL);
        gum_script_unload_sync(script, NULL);
        g_object_unref(script);
    }

    g_print("[*] byebye\n");
    return 0;
}