#ifndef KG_INJECT_H
#define KG_INJECT_H

#include "kg.h"

typedef struct {
    mach_port_t task;
    pid_t pid;
    gchar *payload_path;
} KgInjector;

gboolean kg_injector_attach(KgInjector *inj, pid_t pid);
gboolean kg_injector_inject(KgInjector *inj);
void kg_injector_detach(KgInjector *inj);

gboolean kg_remote_call_x(mach_port_t task, gpointer func,
                          guint n_args, gpointer *args,
                          guint64 *out_x0);

gboolean kg_remote_read(mach_port_t task, mach_vm_address_t addr,
                        void *buf, mach_vm_size_t size,
                        mach_vm_size_t *out_read);

mach_vm_address_t kg_find_symbol_in_task(mach_port_t task,
                                         const gchar *symbol);

#endif