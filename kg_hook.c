#include "kg.h"

typedef struct {
    gchar *name;
    guint hits;
} KgHookCtx;

typedef struct {
    GumInvocationListener *listener;
} KgHookEntry;

static GPtrArray *g_hook_entries = NULL;
static GMainLoop *g_hook_loop = NULL;
static int g_hook_pipe[2];

static void
kg_hook_ctx_free(gpointer data)
{
    KgHookCtx *h = data;
    g_free(h->name);
    g_free(h);
}

static void
kg_hook_entry_free(gpointer data)
{
    KgHookEntry *e = data;
    g_object_unref(e->listener);
    g_free(e);
}

static void
kg_on_enter(GumInvocationContext *ic, gpointer user_data)
{
    KgHookCtx *h = user_data;
    g_atomic_int_inc(&h->hits);
    gpointer a0 = gum_invocation_context_get_nth_argument(ic, 0);
    gpointer a1 = gum_invocation_context_get_nth_argument(ic, 1);
    g_print("[enter] %s(arg0=%p, arg1=%p)\n", h->name, a0, a1);
}

static void
kg_on_leave(GumInvocationContext *ic, gpointer user_data)
{
    KgHookCtx *h = user_data;
    gpointer ret = gum_invocation_context_get_return_value(ic);
    g_print("[leave] %s -> %p\n", h->name, ret);
}

static gboolean
kg_on_api_match(const GumApiDetails *d, gpointer user_data)
{
    KgHookCtx *h = g_new0(KgHookCtx, 1);
    h->name = g_strdup(d->name);

    GumInvocationListener *listener = gum_make_call_listener(
        kg_on_enter, kg_on_leave, h, kg_hook_ctx_free);

    GumInterceptor *ic = gum_interceptor_obtain();
    gum_interceptor_attach(ic, GSIZE_TO_POINTER(d->address), listener, NULL);

    KgHookEntry *e = g_new0(KgHookEntry, 1);
    e->listener = listener;
    g_ptr_array_add(g_hook_entries, e);
    return TRUE;
}

static gboolean
kg_hook_pipe_readable(GIOChannel *src, GIOCondition cond, gpointer user_data)
{
    gchar buf[64];
    read(g_hook_pipe[0], buf, sizeof(buf));
    g_main_loop_quit(g_hook_loop);
    return FALSE;
}

static void
kg_hook_signal(int signo)
{
    gchar c = (gchar) signo;
    ssize_t r = write(g_hook_pipe[1], &c, 1);
    (void) r;
}

static gchar *
kg_normalize_query(const gchar *raw)
{
    if (g_str_has_prefix(raw, "exports:") ||
        g_str_has_prefix(raw, "imports:") ||
        g_str_has_prefix(raw, "sections:"))
        return g_strdup(raw);

    /* 默认按导出符号匹配：exports:*!<pattern>* */
    return g_strdup_printf("exports:*!%s", raw);
}

int
kg_cmd_trace(KgContext *ctx)
{
    if (ctx->argc < 1) {
        g_printerr("usage: kedge trace <symbol|glob|exports:...>\n");
        return 1;
    }

    g_hook_entries = g_ptr_array_new_with_free_func(kg_hook_entry_free);

    GumInterceptor *ic = gum_interceptor_obtain();
    gum_interceptor_begin_transaction(ic);

    GumApiResolver *resolver = gum_api_resolver_make("module");

    gchar *query = kg_normalize_query(ctx->argv[0]);
    g_print("[*] query: %s\n", query);

    GError *error = NULL;
    gum_api_resolver_enumerate_matches(resolver, query,
                                       (GumFoundApiFunc) kg_on_api_match,
                                       NULL, &error);
    g_free(query);

    if (error != NULL) {
        g_printerr("enumerate_matches failed: %s\n", error->message);
        g_error_free(error);
    }

    gum_interceptor_end_transaction(ic);
    g_object_unref(resolver);

    g_print("[*] %u hooks installed\n", g_hook_entries->len);

    if (g_hook_entries->len == 0) {
        g_ptr_array_free(g_hook_entries, TRUE);
        g_hook_entries = NULL;
        return 1;
    }

    g_hook_loop = g_main_loop_new(NULL, FALSE);
    pipe(g_hook_pipe);

    signal(SIGINT, kg_hook_signal);
    signal(SIGTERM, kg_hook_signal);

    GIOChannel *ch = g_io_channel_unix_new(g_hook_pipe[0]);
    g_io_add_watch(ch, G_IO_IN, kg_hook_pipe_readable, NULL);

    g_print("[*] Ctrl-C to stop\n");
    g_main_loop_run(g_hook_loop);

    g_io_channel_unref(ch);
    close(g_hook_pipe[0]);
    close(g_hook_pipe[1]);

    for (guint i = 0; i < g_hook_entries->len; i++) {
        KgHookEntry *e = g_ptr_array_index(g_hook_entries, i);
        gum_interceptor_detach(ic, e->listener);
    }
    g_ptr_array_free(g_hook_entries, TRUE);
    g_hook_entries = NULL;
    g_main_loop_unref(g_hook_loop);
    g_hook_loop = NULL;
    return 0;
}