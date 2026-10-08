#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <CommonCrypto/CommonHMAC.h>
#include <glib.h>

#define KG_WS_CONT   0x0
#define KG_WS_TEXT   0x1
#define KG_WS_BIN    0x2
#define KG_WS_CLOSE  0x8
#define KG_WS_PING   0x9
#define KG_WS_PONG   0xA

typedef struct {
    int fd;
    int id_counter;
    int connected;
} KgRpc;

static int
kg_tcp_connect(const char *host, int port)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static const char *
kg_b64(const uint8_t *data, size_t len)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static char out[512];
    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t a = i < len ? data[i++] : 0;
        uint32_t b = i < len ? data[i++] : 0;
        uint32_t c = i < len ? data[i++] : 0;
        out[j++] = tbl[(a >> 2) & 0x3F];
        out[j++] = tbl[((a << 4) | (b >> 4)) & 0x3F];
        out[j++] = tbl[((b << 2) | (c >> 6)) & 0x3F];
        out[j++] = tbl[c & 0x3F];
    }
    size_t pad = (3 - (len % 3)) % 3;
    while (pad--)
        out[j - 1 - pad] = '=';
    out[j] = 0;
    return out;
}

static ssize_t
kg_recv_exact(int fd, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t r = recv(fd, (char *) buf + total, len - total, 0);
        if (r <= 0)
            return r;
        total += r;
    }
    return (ssize_t) total;
}

static int
kg_ws_handshake(int fd, const char *host, int port)
{
    uint8_t key[16];
    arc4random_buf(key, sizeof(key));

    char req[1024];
    int n = snprintf(req, sizeof(req),
        "GET /ws HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        host, port, kg_b64(key, sizeof(key)));

    if (send(fd, req, n, 0) != n)
        return -1;

    char resp[4096];
    size_t total = 0;
    while (total < sizeof(resp) - 1) {
        ssize_t r = recv(fd, resp + total, sizeof(resp) - 1 - total, 0);
        if (r <= 0)
            return -1;
        total += r;
        resp[total] = 0;
        if (strstr(resp, "\r\n\r\n") != NULL)
            break;
    }

    if (strncmp(resp, "HTTP/1.1 101", 12) != 0 &&
        strncmp(resp, "HTTP/1.0 101", 12) != 0)
        return -1;

    return 0;
}

static int
kg_ws_send_frame(int fd, const uint8_t *data, size_t len, int opcode)
{
    uint8_t header[14];
    size_t hlen = 2;
    header[0] = 0x80 | (opcode & 0x0F);

    if (len < 126) {
        header[1] = 0x80 | (uint8_t) len;
    } else if (len < 65536) {
        header[1] = 0x80 | 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        hlen = 4;
    } else {
        header[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++)
            header[2 + i] = (len >> (56 - i * 8)) & 0xFF;
        hlen = 10;
    }

    uint8_t mask[4];
    arc4random_buf(mask, sizeof(mask));
    memcpy(header + hlen, mask, 4);
    hlen += 4;

    uint8_t *buf = malloc(hlen + len);
    memcpy(buf, header, hlen);
    for (size_t i = 0; i < len; i++)
        buf[hlen + i] = data[i] ^ mask[i % 4];

    ssize_t sent = send(fd, buf, hlen + len, 0);
    free(buf);
    return sent == (ssize_t)(hlen + len) ? 0 : -1;
}

static uint8_t *
kg_ws_recv_frame(int fd, size_t *out_len, int *out_opcode)
{
    uint8_t h[2];
    if (kg_recv_exact(fd, h, 2) != 2)
        return NULL;

    *out_opcode = h[0] & 0x0F;
    uint64_t len = h[1] & 0x7F;
    if (len == 126) {
        uint8_t b[2];
        if (kg_recv_exact(fd, b, 2) != 2)
            return NULL;
        len = ((uint64_t) b[0] << 8) | b[1];
    } else if (len == 127) {
        uint8_t b[8];
        if (kg_recv_exact(fd, b, 8) != 8)
            return NULL;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | b[i];
    }

    uint8_t mask[4] = {0};
    int masked = h[1] & 0x80;
    if (masked && kg_recv_exact(fd, mask, 4) != 4)
        return NULL;

    uint8_t *data = malloc(len + 1);
    if (kg_recv_exact(fd, data, len) != (ssize_t) len) {
        free(data);
        return NULL;
    }
    if (masked)
        for (uint64_t i = 0; i < len; i++)
            data[i] ^= mask[i % 4];
    data[len] = 0;
    *out_len = len;
    return data;
}

static uint8_t *
kg_ws_recv_data(int fd, size_t *out_len)
{
    size_t cap = 4096;
    size_t used = 0;
    uint8_t *acc = malloc(cap);

    while (1) {
        size_t flen = 0;
        int op = 0;
        uint8_t *frame = kg_ws_recv_frame(fd, &flen, &op);
        if (frame == NULL) {
            free(acc);
            return NULL;
        }
        if (op == KG_WS_CLOSE) {
            free(frame);
            free(acc);
            return NULL;
        }
        if (op == KG_WS_PING) {
            kg_ws_send_frame(fd, frame, flen, KG_WS_PONG);
            free(frame);
            continue;
        }
        if (op == KG_WS_PONG) {
            free(frame);
            continue;
        }

        if (used + flen > cap) {
            while (used + flen > cap)
                cap *= 2;
            acc = realloc(acc, cap);
        }
        memcpy(acc + used, frame, flen);
        used += flen;

        int fin = frame[0] & 0x80;
        free(frame);
        if (fin)
            break;
    }

    *out_len = used;
    uint8_t *out = malloc(used + 1);
    memcpy(out, acc, used);
    out[used] = 0;
    free(acc);
    return out;
}

static int
kg_ws_send_raw(int fd, const uint8_t *data, size_t len)
{
    return kg_ws_send_frame(fd, data, len, KG_WS_BIN);
}

static void
kg_conn_close(KgRpc *rpc)
{
    if (rpc->connected) {
        uint8_t zero[2] = {0x03, 0xE8};
        kg_ws_send_frame(rpc->fd, zero, sizeof(zero), KG_WS_CLOSE);
        close(rpc->fd);
        rpc->connected = 0;
    }
}

static int
kg_rpc_connect(KgRpc *rpc, const char *host, int port)
{
    memset(rpc, 0, sizeof(*rpc));

    fprintf(stderr, "  [c1] tcp connect\n");
    rpc->fd = kg_tcp_connect(host, port);
    if (rpc->fd < 0) {
        fprintf(stderr, "  [c1] failed\n");
        return -1;
    }
    fprintf(stderr, "  [c1] ok, fd=%d\n", rpc->fd);

    fprintf(stderr, "  [c2] websocket handshake\n");
    if (kg_ws_handshake(rpc->fd, host, port) != 0) {
        fprintf(stderr, "  [c2] failed\n");
        close(rpc->fd);
        return -1;
    }
    fprintf(stderr, "  [c2] ok\n");

    fprintf(stderr, "  [c3] recv challenge\n");
    size_t clen = 0;
    uint8_t *challenge = kg_ws_recv_data(rpc->fd, &clen);
    if (challenge == NULL || clen != 16) {
        fprintf(stderr, "  [c3] failed, clen=%zu\n", clen);
        if (challenge) free(challenge);
        close(rpc->fd);
        return -1;
    }
    fprintf(stderr, "  [c3] ok, 16 bytes\n");

    fprintf(stderr, "  [c4] hmac\n");
    uint8_t response[32];
    CCHmac(kCCHmacAlgSHA256, "", 0, challenge, 16, response);
    free(challenge);
    fprintf(stderr, "  [c4] ok\n");

    fprintf(stderr, "  [c5] send response\n");
    if (kg_ws_send_raw(rpc->fd, response, 32) != 0) {
        fprintf(stderr, "  [c5] failed\n");
        close(rpc->fd);
        return -1;
    }
    fprintf(stderr, "  [c5] ok\n");

    rpc->connected = 1;
    return 0;
}

static char *
kg_rpc_parse(const uint8_t *msg, size_t len, size_t *out_consumed)
{
    if (len < 4) {
        *out_consumed = len;
        return NULL;
    }
    uint32_t jlen = msg[0] | (msg[1] << 8) | (msg[2] << 16) | (msg[3] << 24);
    if (jlen + 4 > len) {
        *out_consumed = len;
        return NULL;
    }
    char *json = malloc(jlen + 1);
    memcpy(json, msg + 4, jlen);
    json[jlen] = 0;
    *out_consumed = 4 + jlen;
    return json;
}

static int
kg_rpc_send_json(int fd, const char *json)
{
    size_t jlen = strlen(json);
    size_t total = 4 + jlen;
    uint8_t *msg = malloc(total);
    msg[0] = jlen & 0xFF;
    msg[1] = (jlen >> 8) & 0xFF;
    msg[2] = (jlen >> 16) & 0xFF;
    msg[3] = (jlen >> 24) & 0xFF;
    memcpy(msg + 4, json, jlen);

    int r = kg_ws_send_raw(fd, msg, total);
    free(msg);
    return r;
}

static char *
kg_rpc_recv_json(int fd)
{
    size_t len = 0;
    uint8_t *msg = kg_ws_recv_data(fd, &len);
    if (msg == NULL)
        return NULL;

    size_t consumed = 0;
    char *json = kg_rpc_parse(msg, len, &consumed);
    free(msg);
    return json;
}

static const char *
kg_json_find_value(const char *json, const char *key)
{
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (p == NULL)
        return NULL;
    p += strlen(needle);
    while (*p == ' ')
        p++;
    return p;
}

static gchar *
kg_json_get_string(const char *json, const char *key)
{
    const char *p = kg_json_find_value(json, key);
    if (p == NULL || *p != '"')
        return NULL;
    p++;
    GString *s = g_string_new(NULL);
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': g_string_append_c(s, '\n'); break;
                case 't': g_string_append_c(s, '\t'); break;
                case 'r': g_string_append_c(s, '\r'); break;
                case '\\': g_string_append_c(s, '\\'); break;
                case '"': g_string_append_c(s, '"'); break;
                default: g_string_append_c(s, *p);
            }
            p++;
        } else {
            g_string_append_c(s, *p++);
        }
    }
    return g_string_free(s, FALSE);
}

static char *
kg_json_escape(const char *src)
{
    GString *s = g_string_new(NULL);
    for (const char *p = src; *p; p++) {
        switch (*p) {
            case '\\': g_string_append(s, "\\\\"); break;
            case '"':  g_string_append(s, "\\\""); break;
            case '\n': g_string_append(s, "\\n"); break;
            case '\r': g_string_append(s, "\\r"); break;
            case '\t': g_string_append(s, "\\t"); break;
            default:   g_string_append_c(s, *p);
        }
    }
    return g_string_free(s, FALSE);
}

static int
kg_cmd_ps(KgRpc *rpc)
{
    char *req = g_strdup_printf("{\"type\":\"enumerate-processes\",\"id\":%d}",
                                rpc->id_counter++);
    if (kg_rpc_send_json(rpc->fd, req) != 0) {
        g_free(req);
        return -1;
    }
    g_free(req);

    char *resp = kg_rpc_recv_json(rpc->fd);
    if (resp == NULL)
        return -1;

    gchar *type = kg_json_get_string(resp, "type");
    if (type == NULL || g_strcmp0(type, "processes") != 0) {
        g_printerr("unexpected response: %s\n", resp);
        g_free(type);
        free(resp);
        return -1;
    }
    g_free(type);

    g_print("PID\tNAME\n");
    const char *p = resp;
    while ((p = strstr(p, "\"pid\":")) != NULL) {
        p += 6;
        int pid = atoi(p);
        const char *np = strstr(p, "\"name\":\"");
        if (np == NULL)
            break;
        np += 8;
        const char *end = strchr(np, '"');
        if (end == NULL)
            break;
        gchar *name = g_strndup(np, end - np);
        g_print("%d\t%s\n", pid, name);
        g_free(name);
        p = end + 1;
    }

    free(resp);
    return 0;
}

static gchar *
kg_rpc_attach(KgRpc *rpc, int pid)
{
    char *req = g_strdup_printf(
        "{\"type\":\"attach\",\"id\":%d,\"pid\":%d}",
        rpc->id_counter++, pid);
    if (kg_rpc_send_json(rpc->fd, req) != 0) {
        g_free(req);
        return NULL;
    }
    g_free(req);

    char *resp = kg_rpc_recv_json(rpc->fd);
    if (resp == NULL)
        return NULL;

    gchar *type = kg_json_get_string(resp, "type");
    if (type == NULL || g_strcmp0(type, "attached") != 0) {
        g_printerr("attach failed: %s\n", resp);
        g_free(type);
        free(resp);
        return NULL;
    }
    g_free(type);

    gchar *sid = kg_json_get_string(resp, "session_id");
    free(resp);
    return sid;
}

static gchar *
kg_rpc_create_script(KgRpc *rpc, const char *session_id, const char *source)
{
    char *esc = kg_json_escape(source);
    char *req = g_strdup_printf(
        "{\"type\":\"create-script\",\"id\":%d,\"session_id\":\"%s\","
        "\"source\":\"%s\",\"options\":{\"name\":\"kedge\",\"runtime\":\"qjs\"}}",
        rpc->id_counter++, session_id, esc);
    g_free(esc);

    if (kg_rpc_send_json(rpc->fd, req) != 0) {
        g_free(req);
        return NULL;
    }
    g_free(req);

    char *resp = kg_rpc_recv_json(rpc->fd);
    if (resp == NULL)
        return NULL;

    gchar *type = kg_json_get_string(resp, "type");
    if (type == NULL || g_strcmp0(type, "script-created") != 0) {
        g_printerr("create-script failed: %s\n", resp);
        g_free(type);
        free(resp);
        return NULL;
    }
    g_free(type);

    gchar *sid = kg_json_get_string(resp, "script_id");
    free(resp);
    return sid;
}

static int
kg_rpc_load_script(KgRpc *rpc, const char *script_id)
{
    char *req = g_strdup_printf(
        "{\"type\":\"load\",\"id\":%d,\"script_id\":\"%s\"}",
        rpc->id_counter++, script_id);
    if (kg_rpc_send_json(rpc->fd, req) != 0) {
        g_free(req);
        return -1;
    }
    g_free(req);

    char *resp = kg_rpc_recv_json(rpc->fd);
    if (resp == NULL)
        return -1;

    gchar *type = kg_json_get_string(resp, "type");
    int ok = (type != NULL && g_strcmp0(type, "loaded") == 0);
    if (!ok)
        g_printerr("load failed: %s\n", resp);
    g_free(type);
    free(resp);
    return ok ? 0 : -1;
}

static int
kg_cmd_exec(KgRpc *rpc, int pid, const char *source)
{
    gchar *session_id = kg_rpc_attach(rpc, pid);
    if (session_id == NULL)
        return -1;
    g_print("[*] attached, session=%s\n", session_id);

    gchar *script_id = kg_rpc_create_script(rpc, session_id, source);
    if (script_id == NULL) {
        g_free(session_id);
        return -1;
    }
    g_print("[*] script=%s\n", script_id);

    if (kg_rpc_load_script(rpc, script_id) != 0) {
        g_free(script_id);
        g_free(session_id);
        return -1;
    }
    g_print("[*] loaded, listening (Ctrl-C to stop)\n");

    while (1) {
        size_t len = 0;
        uint8_t *msg = kg_ws_recv_data(rpc->fd, &len);
        if (msg == NULL)
            break;
        size_t consumed = 0;
        char *json = kg_rpc_parse(msg, len, &consumed);
        free(msg);
        if (json == NULL)
            continue;

        gchar *type = kg_json_get_string(json, "type");
        if (type != NULL && g_strcmp0(type, "message") == 0) {
            const char *m = kg_json_find_value(json, "message");
            if (m) g_print("%.*s\n", (int) strlen(m), m);
        } else if (type != NULL && g_strcmp0(type, "log") == 0) {
            gchar *payload = kg_json_get_string(json, "payload");
            if (payload) {
                g_print("%s\n", payload);
                g_free(payload);
            }
        }
        g_free(type);
        free(json);
    }

    g_free(script_id);
    g_free(session_id);
    return 0;
}

static void
kg_usage(const char *prog)
{
    fprintf(stderr, "usage: %s <host> <port> <command> [args]\n", prog);
    fprintf(stderr, "commands:\n");
    fprintf(stderr, "  ps                   enumerate remote processes\n");
    fprintf(stderr, "  attach <pid>         attach to remote process\n");
    fprintf(stderr, "  exec <pid> <js>      attach + create script + load + print\n");
}

int
main(int argc, char **argv)
{
    if (argc < 4) {
        kg_usage(argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *cmd = argv[3];

    fprintf(stderr, "[1] before connect\n");

    KgRpc rpc;
    if (kg_rpc_connect(&rpc, host, port) != 0) {
        fprintf(stderr, "connect %s:%d failed\n", host, port);
        return 1;
    }
    fprintf(stderr, "[2] connected, fd=%d\n", rpc.fd);

    int ret = 0;
    if (strcmp(cmd, "ps") == 0) {
        fprintf(stderr, "[3] running ps\n");
        ret = kg_cmd_ps(&rpc);
        fprintf(stderr, "[4] ps done, ret=%d\n", ret);
    } else if (strcmp(cmd, "attach") == 0) {
        if (argc < 5) {
            fprintf(stderr, "attach requires <pid>\n");
            ret = 1;
        } else {
            gchar *sid = kg_rpc_attach(&rpc, atoi(argv[4]));
            if (sid) {
                g_print("session_id=%s\n", sid);
                g_free(sid);
            } else {
                ret = 1;
            }
        }
    } else if (strcmp(cmd, "exec") == 0) {
        if (argc < 6) {
            fprintf(stderr, "exec requires <pid> <js-source>\n");
            ret = 1;
        } else {
            ret = kg_cmd_exec(&rpc, atoi(argv[4]), argv[5]);
        }
    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        ret = 1;
    }

    kg_conn_close(&rpc);
    return ret;
}