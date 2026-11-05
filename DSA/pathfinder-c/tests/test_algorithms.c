/* test_algorithms.c - unit tests for the data structures and graph algorithms.
 * Build and run with:  make test
 */
#include "algorithms.h"
#include "ds.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed, failed;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (cond) {                                       \
            passed++;                                     \
        } else {                                          \
            failed++;                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

static AlgoResult R, R2;   /* large: keep off the stack */
static Graph g, g2;

/* ------------------------------------------------------- data structures */

static void test_heap(void)
{
    MinHeap h;
    heap_init(&h, 2);                       /* forces growth */
    long keys[] = {42, 7, 19, 3, 3, 88, 0, 51, 12, 7};
    int n = (int)(sizeof keys / sizeof keys[0]);
    for (int i = 0; i < n; i++)
        heap_push(&h, i, keys[i]);
    long prev = -1;
    int count = 0;
    HeapItem it;
    while (heap_pop(&h, &it)) {
        CHECK(it.key >= prev, "heap popped %ld after %ld", it.key, prev);
        prev = it.key;
        count++;
    }
    CHECK(count == n, "heap popped %d of %d items", count, n);
    heap_free(&h);
}

static void test_queue_stack(void)
{
    Queue q;
    queue_init(&q, 4);
    int out = -1;
    for (int round = 0; round < 3; round++) {      /* wraps around the ring */
        for (int i = 0; i < 4; i++)
            CHECK(queue_push(&q, round * 10 + i) == 0, "enqueue failed");
        CHECK(queue_push(&q, 99) == -1, "enqueue past capacity should fail");
        for (int i = 0; i < 4; i++) {
            queue_pop(&q, &out);
            CHECK(out == round * 10 + i, "queue order: got %d", out);
        }
    }
    CHECK(queue_empty(&q), "queue should be empty");
    queue_free(&q);

    Stack s;
    stack_init(&s, 1);
    for (int i = 0; i < 100; i++)
        stack_push(&s, i);
    for (int i = 99; i >= 0; i--) {
        stack_pop(&s, &out);
        CHECK(out == i, "stack order: got %d expected %d", out, i);
    }
    CHECK(!stack_pop(&s, &out), "pop from empty stack should fail");
    stack_free(&s);
}

static void test_dsu_sort(void)
{
    DSU d;
    dsu_init(&d, 10);
    CHECK(dsu_union(&d, 1, 2) == 1, "first union");
    CHECK(dsu_union(&d, 3, 4) == 1, "second union");
    CHECK(dsu_union(&d, 2, 4) == 1, "merge sets");
    CHECK(dsu_union(&d, 1, 3) == 0, "1 and 3 already connected");
    CHECK(dsu_find(&d, 1) == dsu_find(&d, 4), "same root");
    CHECK(dsu_find(&d, 5) != dsu_find(&d, 1), "separate root");

    WEdge e[] = {{0, 1, 9}, {2, 3, 1}, {1, 2, 5}, {0, 3, 1}, {4, 5, 5}, {1, 4, 2}};
    int n = (int)(sizeof e / sizeof e[0]);
    merge_sort_edges(e, n);
    for (int i = 1; i < n; i++)
        CHECK(e[i - 1].w <= e[i].w, "edges not sorted at %d", i);
    CHECK(e[0].u == 0 && e[1].u == 2, "tie order by u");
}

/* ------------------------------------------------------------- graph ops */

static void test_graph_ops(void)
{
    graph_init(&g2);
    int a = graph_add_node(&g2, "A", 0, 0);
    int b = graph_add_node(&g2, "B", 10, 0);
    int c = graph_add_node(&g2, "C", 20, 0);
    CHECK(graph_add_edge(&g2, a, b, 4) == 0, "add edge");
    CHECK(graph_add_edge(&g2, b, c, 6) == 0, "add edge");
    CHECK(graph_add_edge(&g2, a, a, 1) != 0, "self loop rejected");
    CHECK(graph_add_edge(&g2, a, c, 0) != 0, "zero weight rejected");
    CHECK(graph_edge_weight(&g2, b, a) == 4, "undirected weight");
    graph_add_edge(&g2, a, b, 3);
    CHECK(graph_edge_weight(&g2, a, b) == 3 && graph_edge_count(&g2) == 2, "update keeps one edge");

    StrBuf sb;
    sb_init(&sb);
    graph_to_text(&g2, &sb);
    Graph copy;
    graph_init(&copy);
    CHECK(graph_parse_text(&copy, sb.buf) == 0, "text round-trip parses");
    CHECK(graph_node_count(&copy) == 3 && graph_edge_count(&copy) == 2, "round-trip sizes");
    CHECK(graph_edge_weight(&copy, b, c) == 6, "round-trip weight");
    sb_free(&sb);
    graph_clear(&copy);

    CHECK(graph_remove_node(&g2, b) == 0, "remove node");
    CHECK(graph_edge_count(&g2) == 0, "incident edges removed");
    CHECK(graph_edge_weight(&g2, a, b) == -1, "edge gone");
    graph_clear(&g2);
}

/* ------------------------------------------------------------ algorithms */

static int path_is_valid(const Graph *gr, const AlgoResult *r, int src, int dst)
{
    if (r->path_len < 1 || r->path[0] != src || r->path[r->path_len - 1] != dst)
        return 0;
    for (int i = 0; i + 1 < r->path_len; i++)
        if (graph_edge_weight(gr, r->path[i], r->path[i + 1]) < 0)
            return 0;
    return 1;
}

static void test_shortest_paths(void)
{
    int ids[MAX_NODES], n = 0;
    for (int i = 0; i < g.slots; i++)
        if (g.nodes[i].active)
            ids[n++] = i;

    /* unit-weight copy of the graph: Floyd on it gives true hop counts for BFS */
    graph_init(&g2);
    for (int i = 0; i < g.slots; i++)
        if (g.nodes[i].active)
            graph_add_node(&g2, g.nodes[i].name, 0, 0);
    for (int i = 0; i < g.slots; i++)
        for (const Edge *e = g.nodes[i].head; e; e = e->next)
            if (i < e->to)
                graph_add_edge(&g2, i, e->to, 1);

    int pairs = 0;
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
            int s = ids[a], t = ids[b];
            run_algorithm(&g, ALG_FLOYD, s, t, &R2);
            long fw = R2.distance;
            result_free(&R2);

            run_algorithm(&g, ALG_DIJKSTRA, s, t, &R);
            CHECK(R.reachable && R.distance == fw, "dijkstra %d->%d = %ld, floyd = %ld", s, t, R.distance, fw);
            CHECK(path_is_valid(&g, &R, s, t) && R.path_weight == R.distance, "dijkstra path %d->%d", s, t);
            result_free(&R);

            run_algorithm(&g2, ALG_FLOYD, s, t, &R2);
            long hops = R2.distance;
            result_free(&R2);
            run_algorithm(&g, ALG_BFS, s, t, &R);
            CHECK(R.distance == hops && R.path_len - 1 == hops, "bfs hops %d->%d = %ld, expected %ld",
                  s, t, R.distance, hops);
            CHECK(path_is_valid(&g, &R, s, t), "bfs path %d->%d", s, t);
            result_free(&R);

            run_algorithm(&g, ALG_DFS, s, t, &R);
            CHECK(R.reachable && path_is_valid(&g, &R, s, t), "dfs path %d->%d", s, t);
            result_free(&R);
            pairs++;
        }
    }
    printf("  checked %d source/target pairs\n", pairs);
    graph_clear(&g2);
}

static void test_mst(void)
{
    int n = graph_node_count(&g);
    run_algorithm(&g, ALG_KRUSKAL, -1, -1, &R);
    long kw = R.mst_weight;
    CHECK(R.mst_len == n - 1, "kruskal edges %d", R.mst_len);
    result_free(&R);
    for (int s = 0; s < g.slots; s++) {
        if (!g.nodes[s].active)
            continue;
        run_algorithm(&g, ALG_PRIM, s, -1, &R);
        CHECK(R.mst_len == n - 1 && R.mst_weight == kw, "prim from %d: %ld vs kruskal %ld", s, R.mst_weight, kw);
        result_free(&R);
    }
    printf("  MST weight %ld (Prim == Kruskal from every start)\n", kw);
}

static void test_disconnected(void)
{
    int lone = graph_add_node(&g, "Island", 900, 600);
    run_algorithm(&g, ALG_DIJKSTRA, 0, lone, &R);
    CHECK(!R.reachable && R.path_len == 0, "isolated node must be unreachable");
    result_free(&R);
    run_algorithm(&g, ALG_KRUSKAL, -1, -1, &R);
    CHECK(R.mst_len == graph_node_count(&g) - 2, "spanning forest skips the island");
    result_free(&R);
    CHECK(run_algorithm(&g, ALG_DIJKSTRA, 0, 63, &R) != 0, "invalid target rejected");
    result_free(&R);
    graph_remove_node(&g, lone);
}

int main(void)
{
    if (graph_load(&g, "data/default_graph.txt") != 0) {
        printf("run from the project folder (needs data/default_graph.txt)\n");
        return 1;
    }
    printf("PathFinder-C unit tests\n");
    test_heap();
    test_queue_stack();
    test_dsu_sort();
    test_graph_ops();
    test_shortest_paths();
    test_mst();
    test_disconnected();
    printf("\n%d passed, %d failed\n", passed, failed);
    graph_clear(&g);
    return failed ? 1 : 0;
}
