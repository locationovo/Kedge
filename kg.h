#ifndef KG_H
#define KG_H

#include <glib.h>
#include <frida-gum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/thread_info.h>
#include <mach/task_info.h>
#include <mach/vm_types.h>
#include <mach/arm/thread_status.h>

typedef struct {
    int argc;
    char **argv;
} KgContext;

int kg_cmd_ps(KgContext *ctx);
int kg_cmd_trace(KgContext *ctx);
int kg_cmd_stalker(KgContext *ctx);
int kg_cmd_discover(KgContext *ctx);
int kg_cmd_scan(KgContext *ctx);
int kg_cmd_patch(KgContext *ctx);
int kg_cmd_kill(KgContext *ctx);
int kg_cmd_modules(KgContext *ctx);
int kg_cmd_threads(KgContext *ctx);
int kg_cmd_except(KgContext *ctx);
int kg_cmd_cloak(KgContext *ctx);
int kg_cmd_watch(KgContext *ctx);
int kg_cmd_bt(KgContext *ctx);
int kg_cmd_load(KgContext *ctx);
int kg_cmd_inject(KgContext *ctx);

#endif