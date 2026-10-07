#include <frida-gum.h>
#include <JavaScriptCore/JavaScriptCore.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>


static JSClassRef g_ptrClass = NULL;

static JSValueRef
kg_ptr_make(JSContextRef ctx, uint64_t addr)
{
    return JSObjectMake(ctx, g_ptrClass, (void *)(uintptr_t) addr);
}

static uint64_t
kg_ptr_addr(JSContextRef ctx, JSValueRef val)
{
    if (val == NULL) return 0;
    if (JSValueIsNumber(ctx, val))
        return (uint64_t) JSValueToNumber(ctx, val, NULL);
    if (!JSValueIsObject(ctx, val)) return 0;
    JSObjectRef obj = JSValueToObject(ctx, val, NULL);
    if (obj == NULL) return 0;
    return (uint64_t)(uintptr_t) JSObjectGetPrivate(obj);
}

static JSValueRef
kg_ptr_op(JSContextRef ctx, JSObjectRef self, size_t argc,
          const JSValueRef argv[], int op)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    if (argc < 1) return kg_ptr_make(ctx, a);
    uint64_t b = kg_ptr_addr(ctx, argv[0]);
    uint64_t r = 0;
    switch (op) {
        case 0: r = a + b; break;
        case 1: r = a - b; break;
        case 2: r = a & b; break;
        case 3: r = a | b; break;
        case 4: r = a ^ b; break;
    }
    return kg_ptr_make(ctx, r);
}

static JSValueRef p_add(JSContextRef c, JSObjectRef f, JSObjectRef s,
    size_t n, const JSValueRef a[], JSValueRef *e) { (void)f; return kg_ptr_op(c,s,n,a,0); }
static JSValueRef p_sub(JSContextRef c, JSObjectRef f, JSObjectRef s,
    size_t n, const JSValueRef a[], JSValueRef *e) { (void)f; return kg_ptr_op(c,s,n,a,1); }
static JSValueRef p_and(JSContextRef c, JSObjectRef f, JSObjectRef s,
    size_t n, const JSValueRef a[], JSValueRef *e) { (void)f; return kg_ptr_op(c,s,n,a,2); }
static JSValueRef p_or (JSContextRef c, JSObjectRef f, JSObjectRef s,
    size_t n, const JSValueRef a[], JSValueRef *e) { (void)f; return kg_ptr_op(c,s,n,a,3); }
static JSValueRef p_xor(JSContextRef c, JSObjectRef f, JSObjectRef s,
    size_t n, const JSValueRef a[], JSValueRef *e) { (void)f; return kg_ptr_op(c,s,n,a,4); }

static JSValueRef
p_isNull(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
         size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    return JSValueMakeBoolean(ctx, a == 0);
}

static JSValueRef
p_equals(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
         size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    uint64_t b = (argc > 0) ? kg_ptr_addr(ctx, argv[0]) : 0;
    return JSValueMakeBoolean(ctx, a == b);
}

static JSValueRef
p_toInt32(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
          size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    return JSValueMakeNumber(ctx, (double)(int32_t) a);
}

static JSValueRef
p_toString(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
           size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long) a);
    JSStringRef s = JSStringCreateWithUTF8CString(buf);
    JSValueRef v = JSValueMakeString(ctx, s);
    JSStringRelease(s);
    return v;
}

static JSValueRef
p_readU8(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
         size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    return JSValueMakeNumber(ctx, *(uint8_t *)(uintptr_t) a);
}

static JSValueRef
p_readU32(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
          size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    return JSValueMakeNumber(ctx, *(uint32_t *)(uintptr_t) a);
}

static JSValueRef
p_readU64(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
          size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    return JSValueMakeNumber(ctx, (double) *(uint64_t *)(uintptr_t) a);
}

static JSValueRef
p_readPointer(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
              size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    return kg_ptr_make(ctx, *(uint64_t *)(uintptr_t) a);
}

static JSValueRef
p_readUtf8String(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                 size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    const char *s = (const char *)(uintptr_t) a;
    JSStringRef js = JSStringCreateWithUTF8CString(s);
    JSValueRef v = JSValueMakeString(ctx, js);
    JSStringRelease(js);
    return v;
}

static JSValueRef
p_writeU8(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
          size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    if (argc > 0) *(uint8_t *)(uintptr_t) a = (uint8_t) JSValueToNumber(ctx, argv[0], NULL);
    return JSValueMakeUndefined(ctx);
}

static JSValueRef
p_writeU32(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
           size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    if (argc > 0) *(uint32_t *)(uintptr_t) a = (uint32_t) JSValueToNumber(ctx, argv[0], NULL);
    return JSValueMakeUndefined(ctx);
}

static JSValueRef
p_writeU64(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
           size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    uint64_t a = (uint64_t)(uintptr_t) JSObjectGetPrivate(self);
    if (argc > 0) *(uint64_t *)(uintptr_t) a = kg_ptr_addr(ctx, argv[0]);
    return JSValueMakeUndefined(ctx);
}

static const JSStaticFunction g_ptrMethods[] = {
    { "add",             p_add,             kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "sub",             p_sub,             kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "and",             p_and,             kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "or",              p_or,              kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "xor",             p_xor,             kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "isNull",          p_isNull,          kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "equals",          p_equals,          kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "toInt32",         p_toInt32,         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "toString",        p_toString,        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "readU8",          p_readU8,          kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "readU32",         p_readU32,         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "readU64",         p_readU64,         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "readPointer",     p_readPointer,     kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "readUtf8String",  p_readUtf8String,  kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "writeU8",         p_writeU8,         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "writeU32",        p_writeU32,        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { "writeU64",        p_writeU64,        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete },
    { NULL, NULL, 0 }
};

static const JSClassDefinition g_ptrDef = {
    0, kJSClassAttributeNone, "NativePointer", NULL,
    NULL, g_ptrMethods,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};


static void
set_str(JSContextRef ctx, JSObjectRef obj, const char *key, const char *val)
{
    JSStringRef k = JSStringCreateWithUTF8CString(key);
    JSStringRef v = JSStringCreateWithUTF8CString(val ? val : "");
    JSObjectSetProperty(ctx, obj, k, JSValueMakeString(ctx, v),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(k);
    JSStringRelease(v);
}

static void
set_num(JSContextRef ctx, JSObjectRef obj, const char *key, double val)
{
    JSStringRef k = JSStringCreateWithUTF8CString(key);
    JSObjectSetProperty(ctx, obj, k, JSValueMakeNumber(ctx, val),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(k);
}

static void
set_ptr(JSContextRef ctx, JSObjectRef obj, const char *key, uint64_t val)
{
    JSStringRef k = JSStringCreateWithUTF8CString(key);
    JSObjectSetProperty(ctx, obj, k, kg_ptr_make(ctx, val),
                        kJSPropertyAttributeNone, NULL);
    JSStringRelease(k);
}


static JSValueRef
js_process_get(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
               size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    JSObjectRef proc = JSObjectMake(ctx, NULL, NULL);
    set_str(ctx, proc, "arch", "arm64");
    set_str(ctx, proc, "platform", "darwin");
    set_num(ctx, proc, "pid", (double) gum_process_get_id());
    set_num(ctx, proc, "pageSize", (double) gum_query_page_size());
    return proc;
}

typedef struct {
    JSContextRef ctx;
    JSObjectRef  arr;
    guint        idx;
} ModEnum;

static gboolean
mod_enum_cb(GumModule *m, gpointer user_data)
{
    ModEnum *e = user_data;
    const GumMemoryRange *r = gum_module_get_range(m);
    const gchar *path = gum_module_get_path(m);
    const gchar *name = gum_module_get_name(m);

    JSObjectRef obj = JSObjectMake(e->ctx, NULL, NULL);
    set_str(e->ctx, obj, "name", name);
    set_str(e->ctx, obj, "path", path);
    if (r != NULL) {
        set_ptr(e->ctx, obj, "base", r->base_address);
        set_num(e->ctx, obj, "size", (double) r->size);
    }
    JSObjectSetPropertyAtIndex(e->ctx, e->arr, e->idx++, obj, NULL);
    return TRUE;
}

static JSValueRef
js_process_enumerateModules(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                            size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    JSObjectRef arr = JSObjectMakeArray(ctx, 0, NULL, NULL);
    ModEnum e = { ctx, arr, 0 };
    gum_process_enumerate_modules(mod_enum_cb, &e);
    return arr;
}


static JSValueRef
js_module_getExportByName(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                          size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    if (argc < 2) return JSValueMakeNull(ctx);

    char mod[256] = {0};
    char sym[256] = {0};

    if (JSValueIsString(ctx, argv[0])) {
        JSStringRef js = JSValueToStringCopy(ctx, argv[0], NULL);
        JSStringGetUTF8CString(js, mod, sizeof(mod));
        JSStringRelease(js);
    }
    {
        JSStringRef js = JSValueToStringCopy(ctx, argv[1], NULL);
        JSStringGetUTF8CString(js, sym, sizeof(sym));
        JSStringRelease(js);
    }

    GumAddress a = 0;
    if (JSValueIsNull(ctx, argv[0]) || mod[0] == '\0') {
        a = gum_module_find_global_export_by_name(sym);
    } else {
        GumModule *m = gum_process_find_module_by_name(mod);
        if (m != NULL) {
            a = gum_module_find_export_by_name(m, sym);
            g_object_unref(m);
        }
    }
    if (a == 0) return JSValueMakeNull(ctx);
    return kg_ptr_make(ctx, (uint64_t) a);
}

static JSValueRef
js_module_getBaseAddress(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                         size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    if (argc < 1) return JSValueMakeNull(ctx);
    JSStringRef js = JSValueToStringCopy(ctx, argv[0], NULL);
    char name[256] = {0};
    JSStringGetUTF8CString(js, name, sizeof(name));
    JSStringRelease(js);

    GumModule *m = gum_process_find_module_by_name(name);
    if (m == NULL) return JSValueMakeNull(ctx);
    const GumMemoryRange *r = gum_module_get_range(m);
    g_object_unref(m);
    if (r == NULL) return JSValueMakeNull(ctx);
    return kg_ptr_make(ctx, r->base_address);
}

/* ---------- Memory ---------- */

static JSValueRef
js_memory_readByteArray(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                        size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    if (argc < 2) return JSValueMakeNull(ctx);
    uint64_t addr = kg_ptr_addr(ctx, argv[0]);
    size_t size = (size_t) JSValueToNumber(ctx, argv[1], NULL);
    if (size == 0 || size > 16 * 1024 * 1024) return JSValueMakeNull(ctx);

    gsize nread = 0;
    guint8 *buf = gum_memory_read(GSIZE_TO_POINTER((gsize) addr), size, &nread);
    if (buf == NULL || nread == 0) {
        if (buf) g_free(buf);
        return JSValueMakeNull(ctx);
    }

    GString *s = g_string_new(NULL);
    for (gsize i = 0; i < nread; i++)
        g_string_append_printf(s, "%02x", buf[i]);
    g_free(buf);

    JSStringRef js = JSStringCreateWithUTF8CString(s->str);
    JSValueRef v = JSValueMakeString(ctx, js);
    JSStringRelease(js);
    g_string_free(s, TRUE);
    return v;
}

static JSValueRef
js_memory_writeByteArray(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                         size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    if (argc < 2) return JSValueMakeBoolean(ctx, FALSE);
    uint64_t addr = kg_ptr_addr(ctx, argv[0]);

    JSStringRef js = JSValueToStringCopy(ctx, argv[1], NULL);
    if (js == NULL) return JSValueMakeBoolean(ctx, FALSE);
    size_t len = JSStringGetMaximumUTF8CStringSize(js);
    char *hex = g_malloc(len);
    JSStringGetUTF8CString(js, hex, len);
    JSStringRelease(js);

    gsize nbytes = strlen(hex) / 2;
    guint8 *buf = g_malloc(nbytes > 0 ? nbytes : 1);
    for (gsize i = 0; i < nbytes; i++) {
        guint b = 0;
        sscanf(hex + i * 2, "%2x", &b);
        buf[i] = (guint8) b;
    }
    g_free(hex);

    gboolean ok = gum_memory_write(GSIZE_TO_POINTER((gsize) addr), buf, nbytes);
    g_free(buf);
    return JSValueMakeBoolean(ctx, ok);
}

/* ---------- 注入全局 ---------- */

static void
kg_install(JSGlobalContextRef ctx, const char *name,
           JSObjectCallAsFunctionCallback fn)
{
    JSStringRef n = JSStringCreateWithUTF8CString(name);
    JSObjectRef f = JSObjectMakeFunctionWithCallback(ctx, n, fn);
    JSObjectSetProperty(ctx, JSContextGetGlobalObject(ctx),
                        n, f, kJSPropertyAttributeNone, NULL);
    JSStringRelease(n);
}

static void
kg_eval(JSGlobalContextRef ctx, const char *src)
{
    JSStringRef s = JSStringCreateWithUTF8CString(src);
    JSValueRef ex = NULL;
    JSEvaluateScript(ctx, s, NULL, NULL, 0, &ex);
    JSStringRelease(s);
    if (ex != NULL) {
        JSStringRef str = JSValueToStringCopy(ctx, ex, NULL);
        size_t n = JSStringGetMaximumUTF8CStringSize(str);
        char *buf = g_malloc(n);
        JSStringGetUTF8CString(str, buf, n);
        g_printerr("[exception] %s\n", buf);
        g_free(buf);
        JSStringRelease(str);
    }
}

static const char *g_bootstrap =
    "function __kgwrap(o) { return o; }"
    "globalThis.hexdump = function(addr, len) {"
    "  return Memory_readByteArray(addr, len);"
    "};"
    "globalThis.Module = {"
    "  getExportByName: Module_getExportByName,"
    "  getBaseAddress: Module_getBaseAddress"
    "};"
    "globalThis.Memory = {"
    "  readByteArray: Memory_readByteArray,"
    "  writeByteArray: Memory_writeByteArray"
    "};"
    "globalThis.Process = Process_get();"
    "Process.enumerateModules = Process_enumerateModules;";

int
main(int argc, char **argv)
{
    gum_init_embedded();

    g_ptrClass = JSClassCreate(&g_ptrDef);

    JSGlobalContextRef ctx = JSGlobalContextCreate(NULL);

    kg_install(ctx, "Process_get", js_process_get);
    kg_install(ctx, "Process_enumerateModules", js_process_enumerateModules);
    kg_install(ctx, "Module_getExportByName", js_module_getExportByName);
    kg_install(ctx, "Module_getBaseAddress", js_module_getBaseAddress);
    kg_install(ctx, "Memory_readByteArray", js_memory_readByteArray);
    kg_install(ctx, "Memory_writeByteArray", js_memory_writeByteArray);

    kg_eval(ctx, g_bootstrap);

    g_print("[*] kedge-repl (JSC) ready. Type 'exit' to quit.\n");

    char line[8192];
    while (1) {
        g_print("kg> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
        if (strlen(line) == 0) continue;

        char expr[16384];
        snprintf(expr, sizeof(expr),
                 "(function(){try{var __r=eval(%s);"
                 "if(__r!==undefined)console.log(JSON.stringify(__r));"
                 "}catch(e){console.log('Error: '+e);}})();",
                 line);
        kg_eval(ctx, expr);
    }

    JSGlobalContextRelease(ctx);
    JSClassRelease(g_ptrClass);
    g_print("[*] byebye\n");
    return 0;
}