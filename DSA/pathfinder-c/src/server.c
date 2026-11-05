/* server.c - HTTP server, request parsing, routing and the JSON API.
 *
 * Only binds to 127.0.0.1, handles one request per connection
 * (Connection: close) and needs no third-party libraries.
 */
#include "server.h"
#include "algorithms.h"
#include "strbuf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#define INVALID_SOCKET (-1)
#endif

#define MAX_REQUEST (1 << 20)

typedef struct {
    char  method[8];
    char  path[256];
    char  query[2048];
    char *body;
    int   body_len;
} Request;

static const ServerConfig *CFG;
static Graph *GRAPH;

/* ------------------------------------------------------------- utilities */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *in, size_t n, char *out, size_t outsz)
{
    size_t j = 0;
    for (size_t i = 0; i < n && j + 1 < outsz; i++) {
        if (in[i] == '+') {
            out[j++] = ' ';
        } else if (in[i] == '%' && i + 2 < n && hexval(in[i + 1]) >= 0 && hexval(in[i + 2]) >= 0) {
            out[j++] = (char)(hexval(in[i + 1]) * 16 + hexval(in[i + 2]));
            i += 2;
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

/* Finds ?key=value in the query string. Returns 1 if present. */
static int get_param(const Request *rq, const char *key, char *out, size_t outsz)
{
    size_t klen = strlen(key);
    const char *p = rq->query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            url_decode(p + klen + 1, len - klen - 1, out, outsz);
            return 1;
        }
        if (!amp)
            break;
        p = amp + 1;
    }
    out[0] = '\0';
    return 0;
}

static int get_int(const Request *rq, const char *key, int *out)
{
    char buf[32];
    if (!get_param(rq, key, buf, sizeof buf) || !buf[0])
        return 0;
    char *end;
    long v = strtol(buf, &end, 10);
    if (*end != '\0')
        return 0;
    *out = (int)v;
    return 1;
}

static void send_all(sock_t fd, const char *data, size_t len)
{
    while (len > 0) {
        int n = send(fd, data, (int)len, 0);
        if (n <= 0)
            return;
        data += n;
        len -= (size_t)n;
    }
}

static void respond(sock_t fd, int status, const char *ctype, const char *body, size_t len,
                    const char *extra_headers)
{
    const char *reason = status == 200 ? "OK" : status == 400 ? "Bad Request" :
                         status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" :
                         status == 413 ? "Payload Too Large" : "Internal Server Error";
    char head[512];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                     "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n%s"
                     "Connection: close\r\n\r\n",
                     status, reason, ctype, (unsigned long)len, extra_headers ? extra_headers : "");
    send_all(fd, head, (size_t)n);
    send_all(fd, body, len);
}

static void respond_json(sock_t fd, int status, const StrBuf *sb)
{
    respond(fd, status, "application/json; charset=utf-8", sb->buf, sb->len, NULL);
}

static void respond_error(sock_t fd, int status, const char *msg)
{
    StrBuf sb;
    sb_init(&sb);
    sb_puts(&sb, "{\"ok\":false,\"error\":");
    sb_json_str(&sb, msg);
    sb_putc(&sb, '}');
    respond_json(fd, status, &sb);
    sb_free(&sb);
}

static void respond_graph(sock_t fd)
{
    StrBuf sb;
    sb_init(&sb);
    sb_puts(&sb, "{\"ok\":true,\"graph\":");
    graph_to_json(GRAPH, &sb);
    sb_putc(&sb, '}');
    respond_json(fd, 200, &sb);
    sb_free(&sb);
}

static void persist(void)
{
    if (graph_save(GRAPH, CFG->graph_file) != 0)
        fprintf(stderr, "warning: could not save %s\n", CFG->graph_file);
}

/* ---------------------------------------------------------- static files */

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

static int safe_path(const char *p)
{
    if (strstr(p, ".."))
        return 0;
    for (; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '/' || *p == '.' || *p == '-' || *p == '_'))
            return 0;
    return 1;
}

static void serve_static(sock_t fd, const char *path)
{
    if (!strcmp(path, "/"))
        path = "/index.html";
    if (!safe_path(path)) {
        respond_error(fd, 404, "not found");
        return;
    }
    char full[512];
    snprintf(full, sizeof full, "%s%s", CFG->public_dir, path);
    FILE *f = fopen(full, "rb");
    if (!f) {
        respond_error(fd, 404, "not found");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(size > 0 ? (size_t)size : 1);
    size_t got = data ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!data) {
        respond_error(fd, 500, "out of memory");
        return;
    }
    respond(fd, 200, mime_for(full), data, got, NULL);
    free(data);
}

/* ------------------------------------------------------------- API routes */

static void api_run(sock_t fd, const Request *rq)
{
    char key[32];
    int src = -1, dst = -1;
    get_param(rq, "algo", key, sizeof key);
    get_int(rq, "src", &src);
    get_int(rq, "dst", &dst);
    int algo = algo_from_key(key);
    if (algo < 0) {
        respond_error(fd, 400, "unknown algorithm");
        return;
    }
    AlgoResult *r = malloc(sizeof *r);
    if (!r) {
        respond_error(fd, 500, "out of memory");
        return;
    }
    if (run_algorithm(GRAPH, (AlgoId)algo, src, dst, r) != 0) {
        respond_error(fd, 400, r->error);
    } else {
        StrBuf sb;
        sb_init(&sb);
        result_to_json(GRAPH, r, &sb);
        respond_json(fd, 200, &sb);
        sb_free(&sb);
    }
    result_free(r);
    free(r);
}

static void route(sock_t fd, Request *rq)
{
    const char *p = rq->path;
    int is_get = !strcmp(rq->method, "GET");
    int is_post = !strcmp(rq->method, "POST");

    if (strncmp(p, "/api/", 5) != 0) {
        if (is_get)
            serve_static(fd, p);
        else
            respond_error(fd, 405, "method not allowed");
        return;
    }

    if (is_get && !strcmp(p, "/api/graph")) {
        respond_graph(fd);
    } else if (is_get && !strcmp(p, "/api/algorithms")) {
        StrBuf sb;
        sb_init(&sb);
        sb_puts(&sb, "{\"ok\":true,\"algorithms\":");
        algorithms_catalog_json(&sb);
        sb_putc(&sb, '}');
        respond_json(fd, 200, &sb);
        sb_free(&sb);
    } else if (is_get && !strcmp(p, "/api/run")) {
        api_run(fd, rq);
    } else if (is_get && !strcmp(p, "/api/export")) {
        StrBuf sb;
        sb_init(&sb);
        graph_to_text(GRAPH, &sb);
        respond(fd, 200, "text/plain; charset=utf-8", sb.buf, sb.len,
                "Content-Disposition: attachment; filename=\"graph.txt\"\r\n");
        sb_free(&sb);
    } else if (!is_post) {
        respond_error(fd, strncmp(p, "/api/", 5) == 0 ? 404 : 405, "unknown endpoint");
    } else if (!strcmp(p, "/api/node/add")) {
        char name[MAX_NAME * 3];
        int x = 500, y = 300;
        get_param(rq, "name", name, sizeof name);
        get_int(rq, "x", &x);
        get_int(rq, "y", &y);
        if (graph_add_node(GRAPH, name, x, y) < 0)
            respond_error(fd, 400, "graph is full (64 locations max)");
        else {
            persist();
            respond_graph(fd);
        }
    } else if (!strcmp(p, "/api/node/remove") || !strcmp(p, "/api/node/move") ||
               !strcmp(p, "/api/node/rename")) {
        int id = -1, x = 0, y = 0, rc;
        char name[MAX_NAME * 3];
        get_int(rq, "id", &id);
        if (!strcmp(p, "/api/node/remove"))
            rc = graph_remove_node(GRAPH, id);
        else if (!strcmp(p, "/api/node/move"))
            rc = (get_int(rq, "x", &x) && get_int(rq, "y", &y)) ? graph_move_node(GRAPH, id, x, y) : -1;
        else
            rc = get_param(rq, "name", name, sizeof name) ? graph_rename_node(GRAPH, id, name) : -1;
        if (rc != 0)
            respond_error(fd, 400, "invalid location");
        else {
            persist();
            respond_graph(fd);
        }
    } else if (!strcmp(p, "/api/edge/add")) {
        int u = -1, v = -1, w = 0;
        get_int(rq, "u", &u);
        get_int(rq, "v", &v);
        get_int(rq, "w", &w);
        if (graph_add_edge(GRAPH, u, v, w) != 0)
            respond_error(fd, 400, "invalid road: pick two different locations and a weight from 1 to 9999");
        else {
            persist();
            respond_graph(fd);
        }
    } else if (!strcmp(p, "/api/edge/remove")) {
        int u = -1, v = -1;
        get_int(rq, "u", &u);
        get_int(rq, "v", &v);
        if (graph_remove_edge(GRAPH, u, v) != 0)
            respond_error(fd, 400, "no such road");
        else {
            persist();
            respond_graph(fd);
        }
    } else if (!strcmp(p, "/api/reset")) {
        if (graph_load(GRAPH, CFG->default_file) != 0)
            respond_error(fd, 500, "could not load the default dataset");
        else {
            persist();
            respond_graph(fd);
        }
    } else if (!strcmp(p, "/api/clear")) {
        graph_clear(GRAPH);
        persist();
        respond_graph(fd);
    } else if (!strcmp(p, "/api/import")) {
        if (!rq->body || graph_parse_text(GRAPH, rq->body) != 0)
            respond_error(fd, 400, "could not parse the graph file");
        else {
            persist();
            respond_graph(fd);
        }
    } else {
        respond_error(fd, 404, "unknown endpoint");
    }
}

/* ------------------------------------------------------- request reading */

static int strncasecmp_compat(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[i]);
        if (ca != cb || ca == 0)
            return ca - cb;
    }
    return 0;
}

static int read_request(sock_t fd, Request *rq, char **raw_out)
{
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf)
        return -1;
    char *hdr_end = NULL;
    long content_len = 0;
    for (;;) {
        if (len == cap) {
            if (cap >= MAX_REQUEST) {
                free(buf);
                return -2;
            }
            cap *= 2;
            char *nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
            if (hdr_end)
                hdr_end = NULL;   /* re-find after realloc */
        }
        int n = recv(fd, buf + len, (int)(cap - len), 0);
        if (n <= 0)
            break;
        len += (size_t)n;
        buf[len] = '\0';
        hdr_end = strstr(buf, "\r\n\r\n");
        if (hdr_end) {
            const char *cl = NULL;
            for (char *q = buf; q < hdr_end; q++)
                if ((q == buf || q[-1] == '\n') && strncasecmp_compat(q, "Content-Length:", 15) == 0) {
                    cl = q + 15;
                    break;
                }
            content_len = cl ? strtol(cl, NULL, 10) : 0;
            if (content_len < 0 || content_len > MAX_REQUEST) {
                free(buf);
                return -2;
            }
            size_t have = len - (size_t)(hdr_end + 4 - buf);
            if (have >= (size_t)content_len)
                break;
        }
    }
    hdr_end = len ? strstr(buf, "\r\n\r\n") : NULL;
    if (!hdr_end) {
        free(buf);
        return -1;
    }
    size_t avail = len - (size_t)(hdr_end + 4 - buf);
    if ((size_t)content_len > avail)
        content_len = (long)avail;      /* client closed early: keep what arrived */
    *hdr_end = '\0';
    char target[2304];
    if (sscanf(buf, "%7s %2303s", rq->method, target) != 2) {
        free(buf);
        return -1;
    }
    char *qm = strchr(target, '?');
    if (qm) {
        *qm = '\0';
        snprintf(rq->query, sizeof rq->query, "%.2047s", qm + 1);
    } else {
        rq->query[0] = '\0';
    }
    snprintf(rq->path, sizeof rq->path, "%.255s", target);
    rq->body = hdr_end + 4;
    rq->body_len = (int)content_len;
    rq->body[content_len] = '\0';
    *raw_out = buf;
    return 0;
}

/* ------------------------------------------------------------- main loop */

/* Opens the app in the default browser (Chrome, Edge, Firefox, ...). */
static void open_in_browser(int port)
{
    char url[64];
    snprintf(url, sizeof url, "http://localhost:%d", port);
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[160];
#ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "open '%s' >/dev/null 2>&1 &", url);
#else
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", url);
#endif
    if (system(cmd) != 0)
        printf("  (could not open a browser automatically - open the address above)\n");
#endif
}

int server_run(const ServerConfig *cfg, Graph *g)
{
    CFG = cfg;
    GRAPH = g;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) {
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)cfg->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* localhost only */
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "Could not use port %d - PathFinder-C may already be running.\n"
                        "Close the other window, or start on another port: pathfinder %d\n",
                cfg->port, cfg->port + 1);
        CLOSESOCK(srv);
        if (cfg->open_browser)
            open_in_browser(cfg->port);   /* probably our own earlier instance */
        return 1;
    }
    if (listen(srv, 16) != 0) {
        perror("listen");
        CLOSESOCK(srv);
        return 1;
    }
    printf("\n  PathFinder-C is running\n  Open  http://localhost:%d  in your browser\n"
           "  Keep this window open while you use the app (Ctrl+C or close it to stop)\n\n", cfg->port);
    fflush(stdout);
    if (cfg->open_browser)
        open_in_browser(cfg->port);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        sock_t fd = accept(srv, (struct sockaddr *)&cli, &cl);
        if (fd == INVALID_SOCKET)
            continue;
        Request rq;
        char *raw = NULL;
        clock_t t0 = clock();
        int rc = read_request(fd, &rq, &raw);
        if (rc == 0) {
            route(fd, &rq);
            printf("  %-4s %s%s%s  (%.2f ms)\n", rq.method, rq.path, rq.query[0] ? "?" : "", rq.query,
                   (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
            fflush(stdout);
        } else if (rc == -2) {
            respond_error(fd, 413, "request too large");
        }
        free(raw);
        CLOSESOCK(fd);
    }
}
