#ifndef KG_UTIL_H
#define KG_UTIL_H


gboolean kg_json_extract_string(const gchar *json, const gchar *key, gchar **out);
gboolean kg_json_extract_type(const gchar *json, gchar **out_type);
gchar *kg_json_escape(const gchar *src);

#endif