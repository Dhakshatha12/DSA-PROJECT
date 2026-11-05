/* graph.c - adjacency-list graph operations, persistence and JSON output. */
#include "graph.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers */

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    /* skip leading spaces, keep printable characters only */
    while (*src == ' ' || *src == '\t')
        src++;
    for (; *src && i < MAX_NAME - 1; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '\r' || c == '\n')
            break;
        if (c >= 0x20)
            dst[i++] = (char)c;
    }
    while (i > 0 && dst[i - 1] == ' ')
        i--;
    dst[i] = '\0';
    if (i == 0)
        strcpy(dst, "Node");
}

static void free_list(Edge *e)
{
    while (e) {
        Edge *next = e->next;
        free(e);
        e = next;
    }
}

/* Inserts or updates the directed half-edge u->v. */
static int set_half_edge(Graph *g, int u, int v, int w)
{
    for (Edge *e = g->nodes[u].head; e; e = e->next) {
        if (e->to == v) {
            e->weight = w;
            return 0;
        }
    }
    Edge *e = malloc(sizeof *e);
    if (!e)
        return -1;
    e->to = v;
    e->weight = w;
    e->next = g->nodes[u].head;      /* O(1) insertion at the head */
    g->nodes[u].head = e;
    return 0;
}

static int remove_half_edge(Graph *g, int u, int v)
{
    Edge **pp = &g->nodes[u].head;
    while (*pp) {
        if ((*pp)->to == v) {
            Edge *dead = *pp;
            *pp = dead->next;
            free(dead);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/* --------------------------------------------------------------- basic ops */

void graph_init(Graph *g)
{
    memset(g, 0, sizeof *g);
}

void graph_clear(Graph *g)
{
    for (int i = 0; i < MAX_NODES; i++)
        free_list(g->nodes[i].head);
    graph_init(g);
}

int graph_valid(const Graph *g, int id)
{
    return id >= 0 && id < MAX_NODES && g->nodes[id].active;
}

int graph_node_count(const Graph *g)
{
    int n = 0;
    for (int i = 0; i < g->slots; i++)
        n += g->nodes[i].active;
    return n;
}

int graph_edge_count(const Graph *g)
{
    int n = 0;
    for (int i = 0; i < g->slots; i++)
        for (Edge *e = g->nodes[i].head; e; e = e->next)
            n++;
    return n / 2;
}

int graph_add_node(Graph *g, const char *name, int x, int y)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (!g->nodes[i].active) {
            Node *n = &g->nodes[i];
            copy_name(n->name, name ? name : "Node");
            n->x = x;
            n->y = y;
            n->active = 1;
            n->head = NULL;
            if (i + 1 > g->slots)
                g->slots = i + 1;
            return i;
        }
    }
    return -1;   /* graph full */
}

int graph_remove_node(Graph *g, int id)
{
    if (!graph_valid(g, id))
        return -1;
    for (Edge *e = g->nodes[id].head; e; e = e->next)
        remove_half_edge(g, e->to, id);
    free_list(g->nodes[id].head);
    memset(&g->nodes[id], 0, sizeof g->nodes[id]);
    while (g->slots > 0 && !g->nodes[g->slots - 1].active)
        g->slots--;
    return 0;
}

int graph_move_node(Graph *g, int id, int x, int y)
{
    if (!graph_valid(g, id))
        return -1;
    g->nodes[id].x = x;
    g->nodes[id].y = y;
    return 0;
}

int graph_rename_node(Graph *g, int id, const char *name)
{
    if (!graph_valid(g, id))
        return -1;
    copy_name(g->nodes[id].name, name);
    return 0;
}

int graph_add_edge(Graph *g, int u, int v, int weight)
{
    if (!graph_valid(g, u) || !graph_valid(g, v) || u == v)
        return -1;
    if (weight < 1 || weight > MAX_WEIGHT)
        return -1;
    if (set_half_edge(g, u, v, weight) || set_half_edge(g, v, u, weight))
        return -1;
    return 0;
}

int graph_remove_edge(Graph *g, int u, int v)
{
    if (!graph_valid(g, u) || !graph_valid(g, v))
        return -1;
    int a = remove_half_edge(g, u, v);
    int b = remove_half_edge(g, v, u);
    return (a && b) ? 0 : -1;
}

int graph_edge_weight(const Graph *g, int u, int v)
{
    if (!graph_valid(g, u) || !graph_valid(g, v))
        return -1;
    for (const Edge *e = g->nodes[u].head; e; e = e->next)
        if (e->to == v)
            return e->weight;
    return -1;
}

/* ------------------------------------------------------------- persistence */

/* Text format (one record per line):
 *   N <id> <x> <y> <name>
 *   E <u> <v> <weight>
 * Lines starting with '#' are comments.
 */
int graph_parse_text(Graph *g, const char *text)
{
    Graph tmp;
    graph_init(&tmp);
    const char *p = text;
    int line_no = 0;
    while (*p) {
        char line[256];
        size_t n = 0;
        while (*p && *p != '\n' && n < sizeof line - 1)
            line[n++] = *p++;
        while (*p && *p != '\n')
            p++;                 /* drop overlong tail */
        if (*p == '\n')
            p++;
        line[n] = '\0';
        line_no++;

        char *s = line;
        while (isspace((unsigned char)*s))
            s++;
        if (*s == '\0' || *s == '#')
            continue;

        if (*s == 'N') {
            int id, x, y, used = 0;
            if (sscanf(s + 1, "%d %d %d %n", &id, &x, &y, &used) < 3 || id < 0 || id >= MAX_NODES) {
                fprintf(stderr, "graph: bad node on line %d\n", line_no);
                graph_clear(&tmp);
                return -1;
            }
            Node *nd = &tmp.nodes[id];
            if (nd->active) {
                fprintf(stderr, "graph: duplicate node id %d\n", id);
                graph_clear(&tmp);
                return -1;
            }
            copy_name(nd->name, s + 1 + used);
            nd->x = x;
            nd->y = y;
            nd->active = 1;
            if (id + 1 > tmp.slots)
                tmp.slots = id + 1;
        } else if (*s == 'E') {
            int u, v, w;
            if (sscanf(s + 1, "%d %d %d", &u, &v, &w) != 3 || graph_add_edge(&tmp, u, v, w) != 0) {
                fprintf(stderr, "graph: bad edge on line %d\n", line_no);
                graph_clear(&tmp);
                return -1;
            }
        }
    }
    graph_clear(g);
    *g = tmp;        /* adjacency lists now owned by g */
    return 0;
}

int graph_load(Graph *g, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 1 << 20) {
        fclose(f);
        return -1;
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        return -1;
    }
    size_t got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = '\0';
    int rc = graph_parse_text(g, text);
    free(text);
    return rc;
}

void graph_to_text(const Graph *g, StrBuf *sb)
{
    sb_puts(sb, "# PathFinder-C graph file\n# N <id> <x> <y> <name>\n# E <u> <v> <weight>\n");
    for (int i = 0; i < g->slots; i++)
        if (g->nodes[i].active)
            sb_printf(sb, "N %d %d %d %s\n", i, g->nodes[i].x, g->nodes[i].y, g->nodes[i].name);
    for (int i = 0; i < g->slots; i++)
        for (const Edge *e = g->nodes[i].head; e; e = e->next)
            if (i < e->to)
                sb_printf(sb, "E %d %d %d\n", i, e->to, e->weight);
}

int graph_save(const Graph *g, const char *path)
{
    StrBuf sb;
    sb_init(&sb);
    graph_to_text(g, &sb);
    FILE *f = fopen(path, "wb");
    if (!f) {
        sb_free(&sb);
        return -1;
    }
    fwrite(sb.buf, 1, sb.len, f);
    fclose(f);
    sb_free(&sb);
    return 0;
}

void graph_to_json(const Graph *g, StrBuf *sb)
{
    sb_puts(sb, "{\"nodes\":[");
    int first = 1;
    for (int i = 0; i < g->slots; i++) {
        const Node *n = &g->nodes[i];
        if (!n->active)
            continue;
        if (!first)
            sb_putc(sb, ',');
        first = 0;
        int degree = 0;
        for (const Edge *e = n->head; e; e = e->next)
            degree++;
        sb_printf(sb, "{\"id\":%d,\"x\":%d,\"y\":%d,\"degree\":%d,\"name\":", i, n->x, n->y, degree);
        sb_json_str(sb, n->name);
        sb_putc(sb, '}');
    }
    sb_puts(sb, "],\"edges\":[");
    first = 1;
    for (int i = 0; i < g->slots; i++) {
        for (const Edge *e = g->nodes[i].head; e; e = e->next) {
            if (i >= e->to)
                continue;    /* emit each undirected edge once */
            if (!first)
                sb_putc(sb, ',');
            first = 0;
            sb_printf(sb, "{\"u\":%d,\"v\":%d,\"w\":%d}", i, e->to, e->weight);
        }
    }
    sb_printf(sb, "],\"nodeCount\":%d,\"edgeCount\":%d,\"maxNodes\":%d}",
              graph_node_count(g), graph_edge_count(g), MAX_NODES);
}
