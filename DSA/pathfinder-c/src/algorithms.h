/* algorithms.h - graph algorithms that record a step-by-step trace.
 *
 * Every algorithm fills an AlgoResult: the answer (distances, path, MST, ...)
 * plus a Trace of Steps. Each Step carries a human-readable message and a
 * snapshot of the data structure the algorithm is driving (heap / queue /
 * stack), which the web front end replays as an animation.
 */
#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include "graph.h"
#include "strbuf.h"

#define INF_DIST   1000000000L   /* fits a 32-bit long (Windows); max path << 1e9 */
#define DS_SNAP    12        /* max data-structure entries kept per step */
#define MAX_STEPS  20000

typedef enum {
    ALG_DIJKSTRA = 0,
    ALG_BFS,
    ALG_DFS,
    ALG_PRIM,
    ALG_KRUSKAL,
    ALG_FLOYD,
    ALG_COUNT
} AlgoId;

typedef struct {
    char type[10];     /* push pop visit relax skip stale tree reject consider phase update done */
    int  u, v, k;
    long val;
    int  ds_count;     /* entries stored in ds_node/ds_key, -1 = no snapshot */
    int  ds_total;     /* real size of the structure at this step            */
    int  ds_node[DS_SNAP];
    long ds_key[DS_SNAP];
    char msg[176];
} Step;

typedef struct {
    Step *steps;
    int   count;
    int   cap;
    int   truncated;
} Trace;

typedef struct {
    AlgoId algo;
    int    src, dst;
    int    ok;                 /* 0 = invalid request (see error) */
    char   error[96];

    int    reachable;
    long   distance;           /* weighted cost src->dst (or hops for BFS) */
    long   path_weight;        /* sum of weights along the returned path   */
    int    path[MAX_NODES];
    int    path_len;
    int    stopped_early;

    long   dist[MAX_NODES];    /* dist / key / level per node, INF if unknown */
    int    parent[MAX_NODES];
    int    order[MAX_NODES];   /* order in which nodes were finalised/visited */
    int    order_len;

    int    mst_u[MAX_NODES], mst_v[MAX_NODES], mst_w[MAX_NODES];
    int    mst_len;
    long   mst_weight;

    long   fw[MAX_NODES][MAX_NODES];   /* Floyd-Warshall distance matrix */

    long   edges_examined;
    long   ds_ops;             /* pushes + pops / unions + finds */
    double micros;             /* C execution time, tracing included */

    Trace  trace;
} AlgoResult;

const char *algo_key(AlgoId id);          /* "dijkstra", ... */
const char *algo_title(AlgoId id);
int         algo_from_key(const char *key);  /* -1 if unknown */

/* Runs the algorithm; returns 0 on success. Call result_free afterwards. */
int  run_algorithm(const Graph *g, AlgoId algo, int src, int dst, AlgoResult *r);
void result_free(AlgoResult *r);
void result_to_json(const Graph *g, const AlgoResult *r, StrBuf *sb);
void algorithms_catalog_json(StrBuf *sb);

#endif
