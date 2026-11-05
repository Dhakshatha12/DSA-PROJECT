/* main.c - PathFinder-C entry point.
 *
 *   pathfinder                      start the web app and open http://localhost:8080
 *   pathfinder 9000                 use another port
 *   pathfinder --no-browser         start without opening a browser
 *   pathfinder --run dijkstra 0 5   run one algorithm in the terminal (no browser)
 *   pathfinder --list               list the locations and their ids
 */
#include "algorithms.h"
#include "graph.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#define GRAPH_FILE   "data/graph.txt"
#define DEFAULT_FILE "data/default_graph.txt"
#define PUBLIC_DIR   "public"

static Graph graph;   /* static: the node table is large */

static int has_app_files(void)
{
    FILE *f = fopen(PUBLIC_DIR "/index.html", "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* If started from another folder (e.g. double-clicked), move to the folder
 * that contains the program so public/ and data/ are found. */
static void go_to_program_folder(const char *argv0)
{
    if (has_app_files())
        return;
#ifdef _WIN32
    (void)argv0;
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;
    char *slash = strrchr(path, '\\');
    if (slash) {
        *slash = '\0';
        if (_chdir(path) != 0)
            return;
    }
#else
    const char *slash = strrchr(argv0, '/');
    if (!slash)
        return;
    char dir[1024];
    size_t n = (size_t)(slash - argv0);
    if (n == 0 || n >= sizeof dir)
        return;
    memcpy(dir, argv0, n);
    dir[n] = '\0';
    if (chdir(dir) != 0)
        return;
#endif
}

static void list_nodes(const Graph *g)
{
    printf("%-4s %-22s %s\n", "id", "location", "roads (neighbour:weight)");
    for (int i = 0; i < g->slots; i++) {
        if (!g->nodes[i].active)
            continue;
        printf("%-4d %-22s", i, g->nodes[i].name);
        for (const Edge *e = g->nodes[i].head; e; e = e->next)
            printf(" %s:%d", g->nodes[e->to].name, e->weight);
        printf("\n");
    }
}

static int run_cli(const Graph *g, const char *key, int src, int dst)
{
    int algo = algo_from_key(key);
    if (algo < 0) {
        fprintf(stderr, "unknown algorithm '%s' (dijkstra bfs dfs prim kruskal floyd)\n", key);
        return 1;
    }
    AlgoResult *r = malloc(sizeof *r);
    if (!r)
        return 1;
    if (run_algorithm(g, (AlgoId)algo, src, dst, r) != 0) {
        fprintf(stderr, "error: %s\n", r->error);
        free(r);
        return 1;
    }
    printf("== %s ==\n", algo_title((AlgoId)algo));
    for (int i = 0; i < r->trace.count; i++)
        printf("%3d. %s\n", i + 1, r->trace.steps[i].msg);
    if (r->path_len > 0) {
        printf("\nPath: ");
        for (int i = 0; i < r->path_len; i++)
            printf("%s%s", i ? " -> " : "", g->nodes[r->path[i]].name);
        printf("\nTotal weight along the path: %ld\n", r->path_weight);
    }
    if (r->mst_len > 0)
        printf("\nMST: %d edges, total weight %ld\n", r->mst_len, r->mst_weight);
    printf("Nodes visited: %d, edges examined: %ld, data-structure ops: %ld, time: %.1f us\n",
           r->order_len, r->edges_examined, r->ds_ops, r->micros);
    result_free(r);
    free(r);
    return 0;
}

int main(int argc, char **argv)
{
    go_to_program_folder(argv[0]);
    graph_init(&graph);
    if (graph_load(&graph, GRAPH_FILE) != 0 && graph_load(&graph, DEFAULT_FILE) != 0) {
        fprintf(stderr, "Could not load %s or %s.\nKeep pathfinder next to the public and data folders.\n",
                GRAPH_FILE, DEFAULT_FILE);
        return 1;
    }

    if (argc >= 2 && !strcmp(argv[1], "--list")) {
        list_nodes(&graph);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--run")) {
        if (argc < 4) {
            fprintf(stderr, "usage: pathfinder --run <algo> <src-id> [dst-id]\n");
            return 1;
        }
        return run_cli(&graph, argv[2], atoi(argv[3]), argc > 4 ? atoi(argv[4]) : -1);
    }

    ServerConfig cfg = {8080, PUBLIC_DIR, GRAPH_FILE, DEFAULT_FILE, 1};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-browser")) {
            cfg.open_browser = 0;
            continue;
        }
        int p = atoi(argv[i]);
        if (p < 1 || p > 65535) {
            fprintf(stderr, "usage: pathfinder [port] [--no-browser] | --run <algo> <src> [dst] | --list\n");
            return 1;
        }
        cfg.port = p;
    }
    if (!has_app_files()) {
        fprintf(stderr, "Cannot find %s/index.html. Keep pathfinder next to the public and data folders.\n",
                PUBLIC_DIR);
        return 1;
    }
    return server_run(&cfg, &graph);
}
