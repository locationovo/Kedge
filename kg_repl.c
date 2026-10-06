#include "kg.h"
#include "kg_util.h"

typedef char *(*readline_fn)(const char *);
typedef void (*add_history_fn)(const char *);
typedef void (*using_history_fn)(void);

static readline_fn p_readline = NULL;
static add_history_fn p_add_history = NULL;
static using_history_fn p_using_history = NULL;

static gboolean
kg_load_readline(void)
{
    void *h = dlopen("/var/jb/usr/lib/libreadline.dylib", RTLD_LAZY);
    if (h == NULL)
        h = dlopen("/usr/lib/libreadline.dylib", RTLD_LAZY);
    if (h == NULL)
        h = dlopen("libreadline.dylib", RTLD_LAZY);
    if (h == NULL)
        return FALSE;
    p_readline = (readline_fn) dlsym(h, "readline");
    p_add_history = (add_history_fn) dlsym(h, "add_history");
    p_using_history = (using_history_fn) dlsym(h, "using_history");
    return p_readline != NULL;
}

typedef struct {
    GumScriptBackend *backend;
    GumScript *script;
} KgRepl;

static void
kg_on_repl_message(GumScript *script, const gchar *message,
                   GBytes *data, gpointer user_data)
{
    gchar *type = NULL;
    if (!kg_json_extract_type(message, &type)) {
        g_print("[raw] %s\n", message);
        return;
    }

    if (g_strcmp0(type, "log") == 0) {
        gchar *payload = NULL;
        if (kg_json_extract_string(message, "payload", &payload)) {
            g_print("%s\n", payload);
            g_free(payload);
        }
    } else if (g_strcmp0(type, "error") == 0) {
        gchar *desc = NULL;
        if (kg_json_extract_string(message, "description", &desc)) {
            g_printerr("[error] %s\n", desc);
            g_free(desc);
        }
    } else if (g_strcmp0(type, "send") == 0) {
        gchar *payload = NULL;
        if (kg_json_extract_string(message, "payload", &payload)) {
            g_print("[send] %s\n", payload);
            g_free(payload);
        }
    } else {
        g_print("[%s] %s\n", type, message);
    }
    g_free(type);
}

static void
kg_on_repl_error(GumScript *script, GError *error, gpointer user_data)
{
    g_printerr("[script error] %s\n", error->message);
}

static gchar *
kg_wrap_input(const gchar *line)
{
    gchar *flat = g_strdup(line);
    for (gchar *p = flat; *p; p++)
        if (*p == '\n' || *p == '\r')
            *p = ' ';

    gboolean is_statement = FALSE;
    const gchar *t = flat;
    while (*t == ' ' || *t == '\t')
        t++;
    static const gchar *kw[] = {
        "var ", "let ", "const ", "function ", "if ", "for ", "while ",
        "return ", "import ", "export ", "class ", "try ", "throw ",
        "switch ", "do ", "delete ", "console.", "Interceptor.",
        "Memory.", "Module.", "Process.", "Thread.", "Stalker.",
        "globalThis.", "setTimeout", "setInterval", "clearTimeout",
        "clearInterval", "DebugSymbol.", "NativeFunction", "hexdump",
        NULL
    };
    for (int i = 0; kw[i]; i++) {
        if (g_str_has_prefix(t, kw[i])) {
            is_statement = TRUE;
            break;
        }
    }

    gchar *wrapped;
    if (is_statement) {
        wrapped = g_strdup_printf(
            "(function(){try{%s}catch(e){"
            "console.log(JSON.stringify({type:'error',"
            "description:''+e}));}})();",
            flat);
    } else {
        wrapped = g_strdup_printf(
            "(function(){try{var __r=eval(%s);"
            "if(__r!==undefined)console.log(JSON.stringify(__r));"
            "}catch(e){console.log(JSON.stringify({type:'error',"
            "description:''+e}));}})();",
            flat);
    }
    g_free(flat);
    return wrapped;
}

static gchar *
kg_read_line(const gchar *prompt)
{
    if (p_readline != NULL)
        return p_readline(prompt);
    g_print("%s", prompt);
    fflush(stdout);
    gchar buf[4096];
    if (fgets(buf, sizeof(buf), stdin) == NULL)
        return NULL;
    gsize len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return g_strdup(buf);
}

int
kg_cmd_repl(KgContext *ctx)
{
    KgRepl *repl = g_new0(KgRepl, 1);

    repl->backend = gum_script_backend_obtain_qjs();
    if (repl->backend == NULL) {
        g_printerr("QuickJS backend not available\n");
        g_free(repl);
        return 1;
    }

    if (kg_load_readline() && p_using_history != NULL)
        p_using_history();

    static const gchar *bootstrap =
        "globalThis.kg={"
        "p:function(a){return ptr(a);},"
        "r:function(a,n){return Memory.readByteArray(ptr(a),n);},"
        "w:function(a,b){Memory.writeByteArray(ptr(a),b);},"
        "m:function(n){return Module.findBaseAddress(n);},"
        "e:function(m,s){return Module.getExportByName(m,s);},"
        "h:function(a,cb){return Interceptor.attach(ptr(a),cb);},"
        "s:function(a){return Stalker.follow(a);},"
        "d:function(a){return DebugSymbol.fromAddress(ptr(a));}"
        "};";

    GError *error = NULL;
    repl->script = gum_script_backend_create_sync(
        repl->backend, "kedge-repl", bootstrap, NULL, NULL, &error);
    if (repl->script == NULL) {
        g_printerr("bootstrap failed: %s\n",
                   error ? error->message : "unknown");
        if (error)
            g_error_free(error);
        g_free(repl);
        return 1;
    }

    g_signal_connect(repl->script, "message",
                     G_CALLBACK(kg_on_repl_message), repl);
    g_signal_connect(repl->script, "error",
                     G_CALLBACK(kg_on_repl_error), repl);

    error = NULL;
    gum_script_load_sync(repl->script, &error);
    if (error != NULL) {
        g_printerr("load failed: %s\n", error->message);
        g_error_free(error);
        g_object_unref(repl->script);
        g_free(repl);
        return 1;
    }

    g_print("[*] REPL ready (QuickJS). Type 'exit' to quit.\n");
    g_print("[*] Helpers: kg.p kg.r kg.w kg.m kg.e kg.h kg.d\n");

    while (TRUE) {
        gchar *line = kg_read_line("kg> ");
        if (line == NULL)
            break;
        if (strlen(line) > 0 && p_add_history != NULL)
            p_add_history(line);
        if (g_strcmp0(line, "exit") == 0 ||
            g_strcmp0(line, "quit") == 0) {
            g_free(line);
            break;
        }
        if (strlen(line) == 0) {
            g_free(line);
            continue;
        }

        gchar *wrapped = kg_wrap_input(line);
        g_free(line);

        error = NULL;
        GumScript *one = gum_script_backend_create_sync(
            repl->backend, "repl-once", wrapped, NULL, NULL, &error);
        g_free(wrapped);

        if (one != NULL) {
            g_signal_connect(one, "message",
                             G_CALLBACK(kg_on_repl_message), repl);
            g_signal_connect(one, "error",
                             G_CALLBACK(kg_on_repl_error), repl);
            error = NULL;
            gum_script_load_sync(one, &error);
            if (error != NULL) {
                g_printerr("[eval error] %s\n", error->message);
                g_error_free(error);
                error = NULL;
            }
            gum_script_unload_sync(one, NULL);
            g_object_unref(one);
        } else {
            g_printerr("[create error] %s\n",
                       error ? error->message : "unknown");
            if (error) {
                g_error_free(error);
                error = NULL;
            }
        }
    }

    g_print("[*] REPL exiting\n");
    gum_script_unload_sync(repl->script, NULL);
    g_object_unref(repl->script);
    g_free(repl);
    return 0;
}