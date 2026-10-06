#include "kg_inject.h"

static mach_port_t
kg_get_task_port(pid_t pid)
{
    mach_port_t task = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS) {
        g_printerr("task_for_pid(%d) failed: 0x%x\n", pid, kr);
        return MACH_PORT_NULL;
    }
    return task;
}

static mach_vm_address_t
kg_remote_alloc(mach_port_t task, mach_vm_size_t size)
{
    mach_vm_address_t addr = 0;
    kern_return_t kr = mach_vm_allocate(task, &addr, size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
        return 0;
    return addr;
}

static gboolean
kg_remote_write_data(mach_port_t task, mach_vm_address_t addr,
                     const void *data, mach_vm_size_t size)
{
    kern_return_t kr = mach_vm_protect(task, addr, size, FALSE,
                                       VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
        return FALSE;
    kr = mach_vm_write(task, addr, (vm_offset_t) data,
                       (mach_msg_type_number_t) size);
    return kr == KERN_SUCCESS;
}

static gboolean
kg_remote_write_code(mach_port_t task, mach_vm_address_t addr,
                     const void *data, mach_vm_size_t size)
{
    kern_return_t kr = mach_vm_protect(task, addr, size, FALSE,
                                       VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
        return FALSE;
    kr = mach_vm_write(task, addr, (vm_offset_t) data,
                       (mach_msg_type_number_t) size);
    if (kr != KERN_SUCCESS)
        return FALSE;
    kr = mach_vm_protect(task, addr, size, FALSE,
                         VM_PROT_READ | VM_PROT_EXECUTE);
    return kr == KERN_SUCCESS;
}

gboolean
kg_remote_read(mach_port_t task, mach_vm_address_t addr,
               void *buf, mach_vm_size_t size, mach_vm_size_t *out_read)
{
    mach_vm_size_t n = 0;
    kern_return_t kr = mach_vm_read_overwrite(task, addr, size,
                                              (mach_vm_address_t) buf, &n);
    if (kr != KERN_SUCCESS) {
        if (out_read)
            *out_read = 0;
        return FALSE;
    }
    if (out_read)
        *out_read = n;
    return TRUE;
}

static gboolean
kg_remote_call_impl(mach_port_t task, gpointer func,
                    guint n_args, gpointer *args, guint64 *out_x0)
{
    mach_vm_address_t result_slot = 0;
    if (out_x0 != NULL) {
        result_slot = kg_remote_alloc(task, 8);
        if (result_slot == 0)
            return FALSE;
        guint64 zero = 0;
        kg_remote_write_data(task, result_slot, &zero, 8);
    }

    const mach_vm_size_t tp_size = 48;
    mach_vm_address_t tp = kg_remote_alloc(task, tp_size);
    if (tp == 0)
        return FALSE;

    guint8 blob[48];
    memset(blob, 0, sizeof(blob));

    if (out_x0 != NULL) {
        guint32 code[8] = {
            0x58000110,
            0x58000131,
            0xD63F0220,
            0xF9000200,
            0xD2800000,
            0xD2800030,
            0xD4001001,
            0xD503201F,
        };
        memcpy(blob, code, sizeof(code));
        guint64 slot64 = (guint64) result_slot;
        memcpy(blob + 32, &slot64, 8);
        guint64 fn64 = (guint64) (uintptr_t) func;
        memcpy(blob + 40, &fn64, 8);
    } else {
        guint32 code[3] = {
            0x58000050,
            0xD61F0200,
            0xD503201F,
        };
        memcpy(blob, code, sizeof(code));
        guint64 fn64 = (guint64) (uintptr_t) func;
        memcpy(blob + 8, &fn64, 8);
    }

    if (!kg_remote_write_code(task, tp, blob, sizeof(blob)))
        return FALSE;

    const mach_vm_size_t stack_size = 0x20000;
    mach_vm_address_t stack = kg_remote_alloc(task, stack_size);
    if (stack == 0)
        return FALSE;

    arm_thread_state64_t state;
    memset(&state, 0, sizeof(state));
    state.__pc = tp;
    state.__sp = stack + stack_size - 16;
    state.__lr = 0;
    for (guint i = 0; i < n_args && i < 8; i++)
        state.__x[i] = (guint64) (uintptr_t) args[i];

    thread_act_t thread = MACH_PORT_NULL;
    kern_return_t kr = thread_create_running(
        task, ARM_THREAD_STATE64,
        (thread_state_t) &state, ARM_THREAD_STATE64_COUNT,
        &thread);

    if (kr != KERN_SUCCESS) {
        g_printerr("thread_create_running failed: 0x%x\n", kr);
        mach_vm_deallocate(task, tp, tp_size);
        mach_vm_deallocate(task, stack, stack_size);
        if (result_slot)
            mach_vm_deallocate(task, result_slot, 8);
        return FALSE;
    }

    thread_basic_info_data_t info;
    mach_msg_type_number_t count;
    for (int i = 0; i < 1000; i++) {
        count = THREAD_BASIC_INFO_COUNT;
        kr = thread_info(thread, THREAD_BASIC_INFO,
                         (thread_info_t) &info, &count);
        if (kr != KERN_SUCCESS)
            break;
        if (info.run_state == TH_STATE_HALTED ||
            info.run_state == TH_STATE_STOPPED)
            break;
        usleep(10000);
    }

    if (out_x0 != NULL && result_slot != 0) {
        guint64 val = 0;
        mach_vm_size_t nread = 0;
        if (kg_remote_read(task, result_slot, &val, 8, &nread) && nread == 8)
            *out_x0 = val;
    }

    thread_terminate(thread);
    mach_port_deallocate(mach_task_self(), thread);
    mach_vm_deallocate(task, tp, tp_size);
    mach_vm_deallocate(task, stack, stack_size);
    if (result_slot)
        mach_vm_deallocate(task, result_slot, 8);
    return TRUE;
}

gboolean
kg_remote_call_x(mach_port_t task, gpointer func,
                 guint n_args, gpointer *args, guint64 *out_x0)
{
    return kg_remote_call_impl(task, func, n_args, args, out_x0);
}

mach_vm_address_t
kg_find_symbol_in_task(mach_port_t task, const gchar *symbol)
{
    struct task_dyld_info dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t kr = task_info(task, TASK_DYLD_INFO,
                                 (task_info_t) &dyld_info, &count);
    if (kr != KERN_SUCCESS)
        return 0;

    mach_vm_address_t info_addr = dyld_info.all_image_info_addr;
    if (info_addr == 0)
        return 0;

    guint8 header[128] = {0};
    mach_vm_size_t nread = 0;
    if (!kg_remote_read(task, info_addr, header, sizeof(header), &nread) ||
        nread < 64)
        return 0;

    guint32 version = *(guint32 *) (header + 0);
    guint32 info_count = *(guint32 *) (header + 4);
    mach_vm_address_t info_array = *(mach_vm_address_t *) (header + 8);

    if (version < 1 || info_count == 0 || info_array == 0)
        return 0;

    for (guint32 i = 0; i < info_count && i < 1024; i++) {
        guint8 entry[24];
        mach_vm_address_t entry_addr = info_array + (mach_vm_address_t) i * 24;
        if (!kg_remote_read(task, entry_addr, entry, 24, &nread) || nread < 24)
            continue;

        mach_vm_address_t image_base = *(mach_vm_address_t *) (entry + 0);
        mach_vm_address_t path_ptr = *(mach_vm_address_t *) (entry + 8);

        gchar path[257] = {0};
        if (!kg_remote_read(task, path_ptr, path, 256, &nread) || nread < 2)
            continue;
        path[256] = '\0';

        if (strstr(path, "libdyld.dylib") == NULL)
            continue;

        struct mach_header_64 mh;
        if (!kg_remote_read(task, image_base, &mh, sizeof(mh), &nread) ||
            nread < sizeof(mh))
            return 0;
        if (mh.magic != MH_MAGIC_64)
            return 0;

        mach_vm_address_t cmd_addr = image_base + sizeof(mh);
        for (guint32 j = 0; j < mh.ncmds; j++) {
            struct load_command lc;
            if (!kg_remote_read(task, cmd_addr, &lc, sizeof(lc), &nread))
                break;
            if (lc.cmd == LC_SYMTAB) {
                struct symtab_command sc;
                if (!kg_remote_read(task, cmd_addr, &sc, sizeof(sc), &nread))
                    return 0;
                for (guint32 k = 0; k < sc.nsyms; k++) {
                    struct nlist_64 nl;
                    mach_vm_address_t nl_addr = image_base + sc.symoff +
                        (mach_vm_address_t) k * sizeof(nl);
                    if (!kg_remote_read(task, nl_addr, &nl, sizeof(nl), &nread))
                        break;
                    if (nl.n_un.n_strx == 0)
                        continue;
                    gchar symname[257] = {0};
                    if (!kg_remote_read(task,
                                        image_base + sc.stroff + nl.n_un.n_strx,
                                        symname, 256, &nread))
                        continue;
                    symname[256] = '\0';
                    if (g_strcmp0(symname + 1, symbol) == 0)
                        return image_base + nl.n_value;
                }
                break;
            }
            cmd_addr += lc.cmdsize;
        }
        break;
    }
    return 0;
}

gboolean
kg_injector_attach(KgInjector *inj, pid_t pid)
{
    inj->pid = pid;
    inj->task = kg_get_task_port(pid);
    return inj->task != MACH_PORT_NULL;
}

gboolean
kg_injector_inject(KgInjector *inj)
{
    if (inj->task == MACH_PORT_NULL || inj->payload_path == NULL)
        return FALSE;

    gsize path_len = strlen(inj->payload_path) + 1;
    mach_vm_address_t remote_path = kg_remote_alloc(inj->task, path_len);
    if (remote_path == 0)
        return FALSE;

    gboolean ok = FALSE;

    if (!kg_remote_write_data(inj->task, remote_path,
                              inj->payload_path, path_len))
        goto out;

    mach_vm_address_t dlopen_addr = kg_find_symbol_in_task(inj->task, "dlopen");
    if (dlopen_addr == 0) {
        g_printerr("dlopen not found\n");
        goto out;
    }
    g_print("[*] target dlopen = 0x%llx\n", (unsigned long long) dlopen_addr);

    gpointer args[2] = {
        GSIZE_TO_POINTER(remote_path),
        GSIZE_TO_POINTER(RTLD_NOW)
    };
    guint64 handle = 0;
    if (!kg_remote_call_x(inj->task, GSIZE_TO_POINTER(dlopen_addr),
                          2, args, &handle)) {
        g_printerr("remote dlopen failed\n");
        goto out;
    }
    g_print("[*] dlopen handle = 0x%llx\n", (unsigned long long) handle);
    ok = TRUE;

out:
    mach_vm_deallocate(inj->task, remote_path, path_len);
    return ok;
}

void
kg_injector_detach(KgInjector *inj)
{
    if (inj->task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), inj->task);
        inj->task = MACH_PORT_NULL;
    }
    g_free(inj->payload_path);
    inj->payload_path = NULL;
}

int
kg_cmd_inject(KgContext *ctx)
{
    if (ctx->argc < 2) {
        g_printerr("usage: kedge inject <pid> <payload.dylib>\n");
        return 1;
    }

    pid_t pid = (pid_t) atoi(ctx->argv[0]);
    if (pid <= 0) {
        g_printerr("invalid pid\n");
        return 1;
    }

    KgInjector inj = {0};
    inj.payload_path = g_strdup(ctx->argv[1]);

    if (!kg_injector_attach(&inj, pid)) {
        g_printerr("attach failed\n");
        g_free(inj.payload_path);
        return 1;
    }

    g_print("[*] attached to pid %d (task=0x%x)\n", pid, inj.task);

    gboolean ok = kg_injector_inject(&inj);
    kg_injector_detach(&inj);
    return ok ? 0 : 1;
}