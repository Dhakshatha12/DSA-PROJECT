/* graph.h - undirected weighted graph stored as an adjacency list.
 *
 * Every node keeps a singly linked list of the edges that leave it. An
 * undirected edge u-v is stored twice (u->v and v->u), so iterating the
 * neighbours of a node is O(degree).
 */
#ifndef GRAPH_H
#define GRAPH_H

#include "strbuf.h"

#define MAX_NODES 64
#define MAX_NAME  32
#define MAX_WEIGHT 9999

typedef struct Edge {
    int          to;      /* index of the neighbouring node */
    int          weight;  /* positive edge weight (e.g. km) */
    struct Edge *next;    /* next edge in this node's list  */
} Edge;

typedef struct {
    char  name[MAX_NAME];
    int   x, y;           /* position on the map canvas (0..1000 x 0..620) */
    int   active;         /* 0 = empty slot                                 */
    Edge *head;           /* adjacency list                                 */
} Node;

typedef struct {
    Node nodes[MAX_NODES];
    int  slots;           /* highest used slot + 1 */
} Graph;

void graph_init(Graph *g);
void graph_clear(Graph *g);                 /* frees every edge, removes every node */

int  graph_valid(const Graph *g, int id);
int  graph_node_count(const Graph *g);
int  graph_edge_count(const Graph *g);      /* undirected edges */

int  graph_add_node(Graph *g, const char *name, int x, int y);   /* returns id or -1 */
int  graph_remove_node(Graph *g, int id);
int  graph_move_node(Graph *g, int id, int x, int y);
int  graph_rename_node(Graph *g, int id, const char *name);

int  graph_add_edge(Graph *g, int u, int v, int weight);         /* adds or updates */
int  graph_remove_edge(Graph *g, int u, int v);
int  graph_edge_weight(const Graph *g, int u, int v);            /* -1 if absent */

int  graph_load(Graph *g, const char *path);   /* 0 on success */
int  graph_save(const Graph *g, const char *path);
int  graph_parse_text(Graph *g, const char *text);
void graph_to_text(const Graph *g, StrBuf *sb);
void graph_to_json(const Graph *g, StrBuf *sb);

#endif
