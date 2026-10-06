#include "kg.h"

typedef struct {
    GumStalker *stalker;
    GumStalkerTransformer *transformer;
    gint insn_count;
    GMainLoop *loop;
    int pipe_fds[2];
} KgTrace;

static KgTrace *g_current_trace = NULL;

static void
kg_transform(GumStalkerIterator *it, GumStalkerOutput *out, gpointer user_data)
{
    KgTrace *t = user_data;
    const cs_insn *insn;
    while (gum_stalker_iterator_next(it, &insn)) {
        g_atomic_int_inc(&t->insn_count);
        gchar mnemonic[32] = {0};
        gchar op_str[160] = {0};
        g_strlcpy(mnemonic, insn->mnemonic, sizeof(mnemonic));
        g_strlcpy(op_str, insn->op_str, sizeof(op_str));
        g_print("  0x%llx  %s %s\n",
                (unsigned long long) insn->address, mnemonic, op_str);
        gum_stalker_iterator_keep(it);
    }
}

static gboolean
kg_on_pipe_readable(GIOChannel *src, GIOCondition cond, gpointer user_data)
{
    KgTrace *t = user_data;
    gchar buf[64];
    read(t->pipe_fds[0], buf, sizeof(buf));
    g_main_loop_quit(t->loop);
    return FALSE;
}

static void
kg_on_signal(int signo)
{
    if (g_current_trace != NULL) {
        gchar c = (gchar) signo;
        ssize_t r = write(g_current_trace->pipe_fds[1], &c, 1);
        (void) r;
    }
}

static void
kg_exclude_module(GumStalker *stalker, GumModule *m)
{
    if (m == NULL)
        return;
    const GumMemoryRange *r = gum_module_get_range(m);
    if (r != NULL)
        gum_stalker_exclude(stalker, r);
}

int
kg_cmd_trace(KgContext *ctx)
{
    KgTrace *t = g_new0(KgTrace, 1);
    t->stalker = gum_stalker_new();
    t->loop = g_main_loop_new(NULL, FALSE);
    pipe(t->pipe_fds);

    t->transformer = gum_stalker_transformer_make_from_callback(
        (GumStalkerTransformerCallback) kg_transform, t, NULL);

    GumModule *self_mod = gum_process_find_module_by_name("kedge");
    kg_exclude_module(t->stalker, self_mod);
    if (self_mod != NULL)
        g_object_unref(self_mod);

    g_current_trace = t;
    signal(SIGINT, kg_on_signal);
    signal(SIGTERM, kg_on_signal);

    GIOChannel *ch = g_io_channel_unix_new(t->pipe_fds[0]);
    g_io_add_watch(ch, G_IO_IN, kg_on_pipe_readable, t);

    g_print("[*] Stalker following current thread, Ctrl-C to stop\n");
    gum_stalker_follow_me(t->stalker, t->transformer, NULL);

    g_main_loop_run(t->loop);

    gum_stalker_unfollow_me(t->stalker);
    gum_stalker_flush(t->stalker);
    g_current_trace = NULL;

    g_print("[*] stopped, total insns: %d\n",
            g_atomic_int_get(&t->insn_count));

    g_io_channel_unref(ch);
    close(t->pipe_fds[0]);
    close(t->pipe_fds[1]);
    g_object_unref(t->transformer);
    g_object_unref(t->stalker);
    g_main_loop_unref(t->loop);
    g_free(t);
    return 0;
}