#include "kg.h"

static GumExceptor *g_exceptor = NULL;

static gboolean
kg_on_exception(GumExceptionDetails *d, gpointer user_data)
{
    gchar *s = gum_exception_details_to_string(d);
    g_print("[exception] type=%d addr=%p tid=%lu\n%s\n",
            d->type,
            GSIZE_TO_POINTER(d->address),
            (unsigned long) d->thread_id,
            s);
    g_free(s);
    return TRUE;
}

int
kg_cmd_except(KgContext *ctx)
{
    if (g_exceptor == NULL) {
        g_exceptor = gum_exceptor_obtain();
        gum_exceptor_add(g_exceptor,
                         (GumExceptionHandler) kg_on_exception, NULL);
    }
    g_print("[*] exception handler installed\n");
    return 0;
}