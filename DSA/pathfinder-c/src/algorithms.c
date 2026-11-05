/* algorithms.c - Dijkstra, BFS, DFS, Prim, Kruskal and Floyd-Warshall,
 * each instrumented to record an animation trace.
 */
#include "algorithms.h"
#include "ds.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static double now_micros(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_micros(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}
#endif

/* ----------------------------------------------------------- algo catalog */

typedef struct {
    const char *key, *title, *goal, *time, *space, *structures;
    int needs_target;
} AlgoInfo;

static const AlgoInfo INFO[ALG_COUNT] = {
    {"dijkstra", "Dijkstra's Algorithm", "Shortest weighted path from source to target",
     "O((V + E) log V)", "O(V + E)", "Binary min-heap priority queue, adjacency list", 1},
    {"bfs", "Breadth-First Search", "Path with the fewest hops (ignores weights)",
     "O(V + E)", "O(V)", "Circular FIFO queue, visited array", 1},
    {"dfs", "Depth-First Search", "Explores as deep as possible; finds a path, not the shortest",
     "O(V + E)", "O(V + E)", "LIFO stack (dynamic array), visited array", 1},
    {"prim", "Prim's MST", "Minimum spanning tree grown outward from the source",
     "O(E log V)", "O(V + E)", "Binary min-heap priority queue", 0},
    {"kruskal", "Kruskal's MST", "Minimum spanning tree built from the cheapest edges",
     "O(E log E)", "O(V + E)", "Merge sort + union-find (path compression, union by rank)", 0},
    {"floyd", "Floyd-Warshall", "Shortest distances between every pair of locations",
     "O(V^3)", "O(V^2)", "2-D distance matrix + next-hop matrix", 1},
};

const char *algo_key(AlgoId id)   { return (id >= 0 && id < ALG_COUNT) ? INFO[id].key : "?"; }
const char *algo_title(AlgoId id) { return (id >= 0 && id < ALG_COUNT) ? INFO[id].title : "?"; }

int algo_from_key(const char *key)
{
    for (int i = 0; i < ALG_COUNT; i++)
        if (strcmp(key, INFO[i].key) == 0)
            return i;
    return -1;
}

void algorithms_catalog_json(StrBuf *sb)
{
    sb_puts(sb, "[");
    for (int i = 0; i < ALG_COUNT; i++) {
        if (i)
            sb_putc(sb, ',');
        sb_puts(sb, "{\"key\":");        sb_json_str(sb, INFO[i].key);
        sb_puts(sb, ",\"title\":");      sb_json_str(sb, INFO[i].title);
        sb_puts(sb, ",\"goal\":");       sb_json_str(sb, INFO[i].goal);
        sb_puts(sb, ",\"time\":");       sb_json_str(sb, INFO[i].time);
        sb_puts(sb, ",\"space\":");      sb_json_str(sb, INFO[i].space);
        sb_puts(sb, ",\"structures\":"); sb_json_str(sb, INFO[i].structures);
        sb_printf(sb, ",\"needsTarget\":%s}", INFO[i].needs_target ? "true" : "false");
    }
    sb_puts(sb, "]");
}

/* ---------------------------------------------------------------- tracing */

static const Graph *G;   /* graph being traced (for node names in messages) */

static const char *nm(int id)
{
    return (id >= 0 && id < MAX_NODES && G->nodes[id].active) ? G->nodes[id].name : "-";
}

static void fmt_dist(char *out, size_t n, long d)
{
    if (d >= INF_DIST)
        snprintf(out, n, "inf");
    else
        snprintf(out, n, "%ld", d);
}

static Step *add_step(AlgoResult *r, const char *type, int u, int v, long val, const char *fmt, ...)
{
    Trace *t = &r->trace;
    if (t->count >= MAX_STEPS) {
        t->truncated = 1;
        return NULL;
    }
    if (t->count == t->cap) {
        int cap = t->cap ? t->cap * 2 : 64;
        Step *ns = realloc(t->steps, sizeof(Step) * (size_t)cap);
        if (!ns) {
            t->truncated = 1;
            return NULL;
        }
        t->steps = ns;
        t->cap = cap;
    }
    Step *s = &t->steps[t->count++];
    memset(s, 0, sizeof *s);
    snprintf(s->type, sizeof s->type, "%s", type);
    s->u = u;
    s->v = v;
    s->k = -1;
    s->val = val;
    s->ds_count = -1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s->msg, sizeof s->msg, fmt, ap);
    va_end(ap);
    return s;
}

/* Snapshot of the heap, sorted by priority so the UI shows what pops next. */
static void snap_heap(Step *s, const MinHeap *h)
{
    if (!s)
        return;
    HeapItem tmp[256];
    int n = h->size < 256 ? h->size : 256;
    memcpy(tmp, h->a, sizeof(HeapItem) * (size_t)n);
    for (int i = 1; i < n; i++) {          /* insertion sort: n is tiny */
        HeapItem x = tmp[i];
        int j = i - 1;
        while (j >= 0 && (tmp[j].key > x.key || (tmp[j].key == x.key && tmp[j].node > x.node))) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = x;
    }
    s->ds_total = h->size;
    s->ds_count = n < DS_SNAP ? n : DS_SNAP;
    for (int i = 0; i < s->ds_count; i++) {
        s->ds_node[i] = tmp[i].node;
        s->ds_key[i] = tmp[i].key;
    }
}

static void snap_queue(Step *s, const Queue *q, const long *level)
{
    if (!s)
        return;
    s->ds_total = q->size;
    s->ds_count = q->size < DS_SNAP ? q->size : DS_SNAP;
    for (int i = 0; i < s->ds_count; i++) {
        int node = queue_at(q, i);
        s->ds_node[i] = node;
        s->ds_key[i] = level[node];
    }
}

/* DFS stack entries encode (from, node) as node + MAX_NODES * (from + 1). */
static int enc(int from, int node) { return node + MAX_NODES * (from + 1); }
static int dec_node(int x)         { return x % MAX_NODES; }
static int dec_from(int x)         { return x / MAX_NODES - 1; }

static void snap_stack(Step *s, const Stack *st)
{
    if (!s)
        return;
    s->ds_total = st->top;
    s->ds_count = st->top < DS_SNAP ? st->top : DS_SNAP;
    for (int i = 0; i < s->ds_count; i++) {    /* top of stack first */
        int x = st->a[st->top - 1 - i];
        s->ds_node[i] = dec_node(x);
        s->ds_key[i] = dec_from(x);
    }
}

/* Neighbours of u sorted by node id (deterministic traversal order). */
static int sorted_neighbours(const Graph *g, int u, int *nb, int *w)
{
    int n = 0;
    for (const Edge *e = g->nodes[u].head; e && n < MAX_NODES; e = e->next) {
        int j = n++;
        while (j > 0 && nb[j - 1] > e->to) {
            nb[j] = nb[j - 1];
            w[j] = w[j - 1];
            j--;
        }
        nb[j] = e->to;
        w[j] = e->weight;
    }
    return n;
}

static void build_path(AlgoResult *r, int dst)
{
    int rev[MAX_NODES], n = 0;
    for (int v = dst; v != -1 && n < MAX_NODES; v = r->parent[v])
        rev[n++] = v;
    r->path_len = n;
    for (int i = 0; i < n; i++)
        r->path[i] = rev[n - 1 - i];
}

static long path_weight(const Graph *g, const int *path, int n)
{
    long total = 0;
    for (int i = 0; i + 1 < n; i++)
        total += graph_edge_weight(g, path[i], path[i + 1]);
    return total;
}

/* ================================================================ Dijkstra */

static void dijkstra(const Graph *g, AlgoResult *r)
{
    int done[MAX_NODES] = {0};
    MinHeap h;
    heap_init(&h, 32);
    r->dist[r->src] = 0;
    heap_push(&h, r->src, 0);
    r->ds_ops++;
    snap_heap(add_step(r, "push", r->src, -1, 0, "Start at %s: distance 0, push it into the min-heap",
                       nm(r->src)), &h);

    HeapItem it;
    while (heap_pop(&h, &it)) {
        r->ds_ops++;
        int u = it.node;
        if (done[u] || it.key > r->dist[u]) {
            snap_heap(add_step(r, "stale", u, -1, it.key,
                               "Pop %s (key %ld) - stale entry, a shorter distance was already found",
                               nm(u), it.key), &h);
            continue;
        }
        done[u] = 1;
        r->order[r->order_len++] = u;
        snap_heap(add_step(r, "visit", u, -1, r->dist[u],
                           "Pop %s with the smallest distance %ld - it is now final", nm(u), r->dist[u]), &h);
        if (u == r->dst) {
            r->stopped_early = 1;
            add_step(r, "done", u, -1, r->dist[u], "Target %s reached: shortest distance = %ld",
                     nm(u), r->dist[u]);
            break;
        }
        for (const Edge *e = g->nodes[u].head; e; e = e->next) {
            int v = e->to;
            r->edges_examined++;
            if (done[v])
                continue;
            long nd = r->dist[u] + e->weight;
            char old[24];
            fmt_dist(old, sizeof old, r->dist[v]);
            if (nd < r->dist[v]) {
                r->dist[v] = nd;
                r->parent[v] = u;
                heap_push(&h, v, nd);
                r->ds_ops++;
                snap_heap(add_step(r, "relax", u, v, nd,
                                   "Relax %s -> %s: %ld + %d = %ld < %s, update and push",
                                   nm(u), nm(v), r->dist[u], e->weight, nd, old), &h);
            } else {
                snap_heap(add_step(r, "skip", u, v, nd,
                                   "Check %s -> %s: %ld + %d = %ld is not better than %s",
                                   nm(u), nm(v), r->dist[u], e->weight, nd, old), &h);
            }
        }
    }
    heap_free(&h);
    if (r->dst >= 0 && r->dist[r->dst] < INF_DIST) {
        r->reachable = 1;
        r->distance = r->dist[r->dst];
        build_path(r, r->dst);
    }
}

/* ===================================================================== BFS */

static void bfs(const Graph *g, AlgoResult *r)
{
    int seen[MAX_NODES] = {0};
    Queue q;
    queue_init(&q, MAX_NODES);
    seen[r->src] = 1;
    r->dist[r->src] = 0;
    queue_push(&q, r->src);
    r->ds_ops++;
    snap_queue(add_step(r, "push", r->src, -1, 0, "Start at %s (level 0): enqueue it", nm(r->src)), &q, r->dist);

    int u, nb[MAX_NODES], w[MAX_NODES];
    while (queue_pop(&q, &u)) {
        r->ds_ops++;
        r->order[r->order_len++] = u;
        snap_queue(add_step(r, "visit", u, -1, r->dist[u], "Dequeue %s (level %ld)", nm(u), r->dist[u]),
                   &q, r->dist);
        if (u == r->dst) {
            r->stopped_early = 1;
            add_step(r, "done", u, -1, r->dist[u], "Target %s reached in %ld hop(s)", nm(u), r->dist[u]);
            break;
        }
        int n = sorted_neighbours(g, u, nb, w);
        for (int i = 0; i < n; i++) {
            int v = nb[i];
            r->edges_examined++;
            if (seen[v]) {
                snap_queue(add_step(r, "skip", u, v, r->dist[v], "%s -> %s: already discovered", nm(u), nm(v)),
                           &q, r->dist);
                continue;
            }
            seen[v] = 1;
            r->dist[v] = r->dist[u] + 1;
            r->parent[v] = u;
            queue_push(&q, v);
            r->ds_ops++;
            snap_queue(add_step(r, "tree", u, v, r->dist[v], "Discover %s from %s (level %ld), enqueue it",
                                nm(v), nm(u), r->dist[v]), &q, r->dist);
        }
    }
    queue_free(&q);
    if (r->dst >= 0 && seen[r->dst]) {
        r->reachable = 1;
        r->distance = r->dist[r->dst];
        build_path(r, r->dst);
    }
}

/* ===================================================================== DFS */

static void dfs(const Graph *g, AlgoResult *r)
{
    int seen[MAX_NODES] = {0};
    Stack st;
    stack_init(&st, 32);
    stack_push(&st, enc(-1, r->src));
    r->ds_ops++;
    snap_stack(add_step(r, "push", r->src, -1, 0, "Start at %s: push it onto the stack", nm(r->src)), &st);

    int x, nb[MAX_NODES], w[MAX_NODES];
    while (stack_pop(&st, &x)) {
        r->ds_ops++;
        int u = dec_node(x), from = dec_from(x);
        if (seen[u]) {
            snap_stack(add_step(r, "stale", u, from, 0, "Pop %s - already visited, discard", nm(u)), &st);
            continue;
        }
        seen[u] = 1;
        r->parent[u] = from;
        r->dist[u] = from < 0 ? 0 : r->dist[from] + 1;   /* depth */
        r->order[r->order_len++] = u;
        if (from >= 0)
            snap_stack(add_step(r, "tree", from, u, r->dist[u], "Pop %s: visit it via %s (depth %ld)",
                                nm(u), nm(from), r->dist[u]), &st);
        else
            snap_stack(add_step(r, "visit", u, -1, 0, "Pop %s: visit it (depth 0)", nm(u)), &st);
        if (u == r->dst) {
            r->stopped_early = 1;
            add_step(r, "done", u, -1, r->dist[u], "Target %s found at depth %ld", nm(u), r->dist[u]);
            break;
        }
        int n = sorted_neighbours(g, u, nb, w);
        for (int i = n - 1; i >= 0; i--) {        /* reverse so the smallest id is explored first */
            int v = nb[i];
            r->edges_examined++;
            if (seen[v])
                continue;
            stack_push(&st, enc(u, v));
            r->ds_ops++;
            snap_stack(add_step(r, "push", v, u, 0, "Push %s (neighbour of %s)", nm(v), nm(u)), &st);
        }
    }
    stack_free(&st);
    if (r->dst >= 0 && seen[r->dst]) {
        r->reachable = 1;
        build_path(r, r->dst);
        r->distance = r->path_len - 1;
    }
}

/* ==================================================================== Prim */

static void prim(const Graph *g, AlgoResult *r)
{
    int in_tree[MAX_NODES] = {0};
    MinHeap h;
    heap_init(&h, 32);
    r->dist[r->src] = 0;
    heap_push(&h, r->src, 0);
    r->ds_ops++;
    snap_heap(add_step(r, "push", r->src, -1, 0, "Start the tree at %s (key 0)", nm(r->src)), &h);

    HeapItem it;
    while (heap_pop(&h, &it)) {
        r->ds_ops++;
        int u = it.node;
        if (in_tree[u] || it.key > r->dist[u]) {
            snap_heap(add_step(r, "stale", u, -1, it.key, "Pop %s (key %ld) - stale, skip", nm(u), it.key), &h);
            continue;
        }
        in_tree[u] = 1;
        r->order[r->order_len++] = u;
        if (r->parent[u] >= 0) {
            int p = r->parent[u], wt = (int)r->dist[u];
            r->mst_u[r->mst_len] = p;
            r->mst_v[r->mst_len] = u;
            r->mst_w[r->mst_len] = wt;
            r->mst_len++;
            r->mst_weight += wt;
            snap_heap(add_step(r, "tree", p, u, wt, "Add edge %s - %s (%d) to the tree, total = %ld",
                               nm(p), nm(u), wt, r->mst_weight), &h);
        } else {
            snap_heap(add_step(r, "visit", u, -1, 0, "%s joins the tree as the root", nm(u)), &h);
        }
        for (const Edge *e = g->nodes[u].head; e; e = e->next) {
            int v = e->to;
            r->edges_examined++;
            if (in_tree[v])
                continue;
            if (e->weight < r->dist[v]) {
                char old[24];
                fmt_dist(old, sizeof old, r->dist[v]);
                r->dist[v] = e->weight;
                r->parent[v] = u;
                heap_push(&h, v, e->weight);
                r->ds_ops++;
                snap_heap(add_step(r, "relax", u, v, e->weight, "Cheaper link to %s via %s: %d < %s, push",
                                   nm(v), nm(u), e->weight, old), &h);
            } else {
                snap_heap(add_step(r, "skip", u, v, e->weight, "Link %s - %s (%d) is not cheaper than %ld",
                                   nm(u), nm(v), e->weight, r->dist[v]), &h);
            }
        }
    }
    heap_free(&h);
    int n = graph_node_count(g);
    add_step(r, "done", -1, -1, r->mst_weight,
             r->order_len == n ? "Spanning tree complete: %d edges, total weight %ld"
                               : "Tree covers only the reachable part: %d edges, total weight %ld",
             r->mst_len, r->mst_weight);
}

/* ================================================================= Kruskal */

static void kruskal(const Graph *g, AlgoResult *r)
{
    WEdge edges[MAX_NODES * MAX_NODES / 2];
    int m = 0;
    for (int u = 0; u < g->slots; u++)
        for (const Edge *e = g->nodes[u].head; e; e = e->next)
            if (u < e->to) {
                edges[m].u = u;
                edges[m].v = e->to;
                edges[m].w = e->weight;
                m++;
            }
    merge_sort_edges(edges, m);
    add_step(r, "phase", -1, -1, m, "Merge-sort all %d edges by weight (cheapest first)", m);

    DSU d;
    dsu_init(&d, MAX_NODES);
    int need = graph_node_count(g) - 1;
    for (int i = 0; i < m && r->mst_len < need; i++) {
        WEdge *e = &edges[i];
        r->edges_examined++;
        add_step(r, "consider", e->u, e->v, e->w, "Consider %s - %s (%d)", nm(e->u), nm(e->v), e->w);
        r->ds_ops += 2;
        if (dsu_union(&d, e->u, e->v)) {
            r->mst_u[r->mst_len] = e->u;
            r->mst_v[r->mst_len] = e->v;
            r->mst_w[r->mst_len] = e->w;
            r->mst_len++;
            r->mst_weight += e->w;
            add_step(r, "tree", e->u, e->v, e->w, "Different sets: union them and keep the edge, total = %ld",
                     r->mst_weight);
        } else {
            add_step(r, "reject", e->u, e->v, e->w, "Same set: %s and %s are already connected, reject (cycle)",
                     nm(e->u), nm(e->v));
        }
    }
    for (int u = 0; u < g->slots; u++)
        if (g->nodes[u].active)
            r->order[r->order_len++] = u;
    add_step(r, "done", -1, -1, r->mst_weight,
             r->mst_len == need ? "Spanning tree complete: %d edges, total weight %ld"
                                : "Graph is disconnected: spanning forest with %d edges, total weight %ld",
             r->mst_len, r->mst_weight);
}

/* ========================================================== Floyd-Warshall */

static void floyd(const Graph *g, AlgoResult *r)
{
    static int next[MAX_NODES][MAX_NODES];
    int ids[MAX_NODES], n = 0;
    for (int i = 0; i < g->slots; i++)
        if (g->nodes[i].active)
            ids[n++] = i;

    for (int a = 0; a < MAX_NODES; a++)
        for (int b = 0; b < MAX_NODES; b++) {
            r->fw[a][b] = (a == b) ? 0 : INF_DIST;
            next[a][b] = -1;
        }
    for (int a = 0; a < n; a++) {
        int i = ids[a];
        next[i][i] = i;
        for (const Edge *e = g->nodes[i].head; e; e = e->next) {
            r->fw[i][e->to] = e->weight;
            next[i][e->to] = e->to;
        }
    }
    add_step(r, "phase", -1, -1, n, "Initialise the %d x %d matrix with direct edge weights", n, n);

    for (int c = 0; c < n; c++) {
        int k = ids[c];
        Step *s = add_step(r, "phase", k, -1, c, "Round %d: allow paths that pass through %s", c + 1, nm(k));
        if (s)
            s->k = k;
        for (int a = 0; a < n; a++) {
            int i = ids[a];
            if (r->fw[i][k] >= INF_DIST)
                continue;
            for (int b = 0; b < n; b++) {
                int j = ids[b];
                r->edges_examined++;
                long via = r->fw[i][k] + r->fw[k][j];
                if (via < r->fw[i][j]) {
                    char old[24];
                    fmt_dist(old, sizeof old, r->fw[i][j]);
                    r->fw[i][j] = via;
                    next[i][j] = next[i][k];
                    r->ds_ops++;
                    if (i < j) {   /* the matrix is symmetric: log one half */
                        s = add_step(r, "update", i, j, via, "d(%s, %s): %s -> %ld via %s",
                                     nm(i), nm(j), old, via, nm(k));
                        if (s)
                            s->k = k;
                    }
                }
            }
        }
    }
    for (int a = 0; a < n; a++)
        r->order[r->order_len++] = ids[a];

    if (r->dst >= 0 && r->fw[r->src][r->dst] < INF_DIST) {
        r->reachable = 1;
        r->distance = r->fw[r->src][r->dst];
        int u = r->src;
        r->path[r->path_len++] = u;
        while (u != r->dst && r->path_len < MAX_NODES) {
            u = next[u][r->dst];
            r->path[r->path_len++] = u;
        }
        for (int i = 0; i < MAX_NODES; i++)
            r->dist[i] = r->fw[r->src][i];
        add_step(r, "done", r->src, r->dst, r->distance, "Shortest %s -> %s = %ld (read from the matrix)",
                 nm(r->src), nm(r->dst), r->distance);
    }
}

/* =============================================================== dispatch */

int run_algorithm(const Graph *g, AlgoId algo, int src, int dst, AlgoResult *r)
{
    memset(r, 0, sizeof *r);
    G = g;
    r->algo = algo;
    r->src = src;
    r->dst = dst;
    for (int i = 0; i < MAX_NODES; i++) {
        r->dist[i] = INF_DIST;
        r->parent[i] = -1;
    }
    if (algo < 0 || algo >= ALG_COUNT) {
        snprintf(r->error, sizeof r->error, "unknown algorithm");
        return -1;
    }
    if (graph_node_count(g) == 0) {
        snprintf(r->error, sizeof r->error, "the graph is empty - add some locations first");
        return -1;
    }
    if (algo != ALG_KRUSKAL && !graph_valid(g, src)) {
        snprintf(r->error, sizeof r->error, "choose a valid source location");
        return -1;
    }
    if (INFO[algo].needs_target && !graph_valid(g, dst)) {
        snprintf(r->error, sizeof r->error, "choose a valid target location");
        return -1;
    }
    if (!INFO[algo].needs_target)
        r->dst = -1;

    double t0 = now_micros();
    switch (algo) {
    case ALG_DIJKSTRA: dijkstra(g, r); break;
    case ALG_BFS:      bfs(g, r);      break;
    case ALG_DFS:      dfs(g, r);      break;
    case ALG_PRIM:     prim(g, r);     break;
    case ALG_KRUSKAL:  kruskal(g, r);  break;
    case ALG_FLOYD:    floyd(g, r);    break;
    default: break;
    }
    r->micros = now_micros() - t0;
    if (r->reachable)
        r->path_weight = path_weight(g, r->path, r->path_len);
    else if (r->dst >= 0)
        add_step(r, "done", r->src, r->dst, -1, "%s cannot be reached from %s", nm(r->dst), nm(r->src));
    r->ok = 1;
    return 0;
}

void result_free(AlgoResult *r)
{
    free(r->trace.steps);
    r->trace.steps = NULL;
    r->trace.count = r->trace.cap = 0;
}

/* ================================================================== JSON */

static void json_long(StrBuf *sb, long v)
{
    if (v >= INF_DIST)
        sb_puts(sb, "null");
    else
        sb_printf(sb, "%ld", v);
}

void result_to_json(const Graph *g, const AlgoResult *r, StrBuf *sb)
{
    const AlgoInfo *in = &INFO[r->algo];
    sb_puts(sb, "{\"ok\":true,\"algo\":");
    sb_json_str(sb, in->key);
    sb_puts(sb, ",\"title\":");
    sb_json_str(sb, in->title);
    sb_puts(sb, ",\"complexity\":{\"time\":");
    sb_json_str(sb, in->time);
    sb_puts(sb, ",\"space\":");
    sb_json_str(sb, in->space);
    sb_puts(sb, ",\"structures\":");
    sb_json_str(sb, in->structures);
    sb_printf(sb, "},\"source\":%d,\"target\":%d,\"reachable\":%s,\"stoppedEarly\":%s",
              r->src, r->dst, r->reachable ? "true" : "false", r->stopped_early ? "true" : "false");
    sb_puts(sb, ",\"distance\":");
    if (r->reachable) json_long(sb, r->distance); else sb_puts(sb, "null");
    sb_printf(sb, ",\"pathWeight\":%ld,\"path\":[", r->path_weight);
    for (int i = 0; i < r->path_len; i++)
        sb_printf(sb, "%s%d", i ? "," : "", r->path[i]);
    sb_puts(sb, "],\"order\":[");
    for (int i = 0; i < r->order_len; i++)
        sb_printf(sb, "%s%d", i ? "," : "", r->order[i]);

    sb_puts(sb, "],\"dist\":{");
    int first = 1;
    for (int i = 0; i < g->slots; i++) {
        if (!g->nodes[i].active)
            continue;
        sb_printf(sb, "%s\"%d\":", first ? "" : ",", i);
        json_long(sb, r->dist[i]);
        first = 0;
    }
    sb_puts(sb, "},\"parent\":{");
    first = 1;
    for (int i = 0; i < g->slots; i++) {
        if (!g->nodes[i].active)
            continue;
        sb_printf(sb, "%s\"%d\":%d", first ? "" : ",", i, r->parent[i]);
        first = 0;
    }
    sb_puts(sb, "},\"mst\":[");
    for (int i = 0; i < r->mst_len; i++)
        sb_printf(sb, "%s[%d,%d,%d]", i ? "," : "", r->mst_u[i], r->mst_v[i], r->mst_w[i]);
    sb_printf(sb, "],\"mstWeight\":%ld", r->mst_weight);

    if (r->algo == ALG_FLOYD) {
        sb_puts(sb, ",\"matrix\":{\"ids\":[");
        first = 1;
        for (int i = 0; i < g->slots; i++)
            if (g->nodes[i].active) {
                sb_printf(sb, "%s%d", first ? "" : ",", i);
                first = 0;
            }
        sb_puts(sb, "],\"d\":[");
        int fr = 1;
        for (int i = 0; i < g->slots; i++) {
            if (!g->nodes[i].active)
                continue;
            sb_puts(sb, fr ? "[" : ",[");
            fr = 0;
            int fc = 1;
            for (int j = 0; j < g->slots; j++) {
                if (!g->nodes[j].active)
                    continue;
                if (!fc)
                    sb_putc(sb, ',');
                fc = 0;
                json_long(sb, r->fw[i][j]);
            }
            sb_putc(sb, ']');
        }
        sb_puts(sb, "]}");
    }

    sb_printf(sb, ",\"stats\":{\"steps\":%d,\"visited\":%d,\"edgesExamined\":%ld,\"dsOps\":%ld,"
                  "\"micros\":%.1f,\"truncated\":%s}",
              r->trace.count, r->order_len, r->edges_examined, r->ds_ops, r->micros,
              r->trace.truncated ? "true" : "false");

    sb_puts(sb, ",\"steps\":[");
    for (int i = 0; i < r->trace.count; i++) {
        const Step *s = &r->trace.steps[i];
        sb_printf(sb, "%s{\"t\":\"%s\",\"u\":%d,\"v\":%d,\"k\":%d,\"val\":%ld,\"msg\":",
                  i ? "," : "", s->type, s->u, s->v, s->k, s->val);
        sb_json_str(sb, s->msg);
        if (s->ds_count >= 0) {
            sb_printf(sb, ",\"dsTotal\":%d,\"ds\":[", s->ds_total);
            for (int j = 0; j < s->ds_count; j++)
                sb_printf(sb, "%s[%d,%ld]", j ? "," : "", s->ds_node[j], s->ds_key[j]);
            sb_putc(sb, ']');
        }
        sb_putc(sb, '}');
    }
    sb_puts(sb, "]}");
}
