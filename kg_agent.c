#include <frida-gum.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>

static int (*orig_open)(const char *, int, ...) = NULL;
static int (*orig_close)(int) = NULL;

static int
kg_open_replacement(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    g_print("[kg_agent] open(\"%s\", %d)\n", path, flags);
    return orig_open(path, flags, mode);
}

static int
kg_close_replacement(int fd)
{
    g_print("[kg_agent] close(%d)\n", fd);
    return orig_close(fd);
}

__attribute__((constructor))
static void
kg_agent_init(void)
{
    gum_init_embedded();

    GumInterceptor *ic = gum_interceptor_obtain();

    gpointer open_addr = dlsym(RTLD_DEFAULT, "open");
    if (open_addr != NULL)
        gum_interceptor_replace(ic, open_addr,
                                (gpointer) kg_open_replacement,
                                NULL, (gpointer *) &orig_open);

    gpointer close_addr = dlsym(RTLD_DEFAULT, "close");
    if (close_addr != NULL)
        gum_interceptor_replace(ic, close_addr,
                                (gpointer) kg_close_replacement,
                                NULL, (gpointer *) &orig_close);

    g_print("[kg_agent] hooks installed\n");
}