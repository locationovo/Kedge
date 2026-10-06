#include "kg_util.h"
#include <string.h>

static const gchar *
kg_skip_ws(const gchar *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static const gchar *
kg_find_key(const gchar *json, const gchar *key)
{
    gchar *needle = g_strdup_printf("\"%s\"", key);
    const gchar *p = strstr(json, needle);
    g_free(needle);
    if (p == NULL)
        return NULL;
    p = kg_skip_ws(p + strlen(key) + 2);
    if (*p != ':')
        return NULL;
    return kg_skip_ws(p + 1);
}

static gchar *
kg_parse_json_string(const gchar **pp)
{
    const gchar *p = *pp;
    if (*p != '"')
        return NULL;
    p++;

    GString *out = g_string_new(NULL);
    while (*p != '\0' && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  g_string_append_c(out, '"');  break;
                case '\\': g_string_append_c(out, '\\'); break;
                case '/':  g_string_append_c(out, '/');  break;
                case 'n':  g_string_append_c(out, '\n'); break;
                case 't':  g_string_append_c(out, '\t'); break;
                case 'r':  g_string_append_c(out, '\r'); break;
                case 'b':  g_string_append_c(out, '\b'); break;
                case 'f':  g_string_append_c(out, '\f'); break;
                case 'u':
                    if (p[1] && p[2] && p[3] && p[4]) {
                        gchar hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        gunichar uc = (gunichar) g_ascii_strtoull(hex, NULL, 16);
                        gchar utf8[8] = {0};
                        gint n = g_unichar_to_utf8(uc, utf8);
                        g_string_append_len(out, utf8, n);
                        p += 4;
                    }
                    break;
                default:
                    g_string_append_c(out, *p);
                    break;
            }
            p++;
        } else {
            g_string_append_c(out, *p++);
        }
    }

    if (*p != '"') {
        g_string_free(out, TRUE);
        return NULL;
    }
    *pp = p + 1;

    gchar *result = g_strdup(out->str);
    g_string_free(out, TRUE);
    return result;
}

gboolean
kg_json_extract_string(const gchar *json, const gchar *key, gchar **out)
{
    const gchar *p = kg_find_key(json, key);
    if (p == NULL || *p != '"')
        return FALSE;
    gchar *v = kg_parse_json_string(&p);
    if (v == NULL)
        return FALSE;
    *out = v;
    return TRUE;
}

gboolean
kg_json_extract_type(const gchar *json, gchar **out_type)
{
    return kg_json_extract_string(json, "type", out_type);
}

gchar *
kg_json_escape(const gchar *src)
{
    GString *out = g_string_new(NULL);
    for (const gchar *p = src; *p; p++) {
        switch (*p) {
            case '"':  g_string_append(out, "\\\""); break;
            case '\\': g_string_append(out, "\\\\"); break;
            case '\n': g_string_append(out, "\\n");  break;
            case '\r': g_string_append(out, "\\r");  break;
            case '\t': g_string_append(out, "\\t");  break;
            default:
                if ((guchar) *p < 0x20)
                    g_string_append_printf(out, "\\u%04x", (guchar) *p);
                else
                    g_string_append_c(out, *p);
                break;
        }
    }
    gchar *result = g_strdup(out->str);
    g_string_free(out, TRUE);
    return result;
}