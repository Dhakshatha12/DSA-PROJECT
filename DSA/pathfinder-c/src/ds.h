/* ds.h - the classic data structures the graph algorithms are built on.
 *
 *   MinHeap  binary min-heap priority queue      (Dijkstra, Prim)
 *   Queue    circular array FIFO queue            (BFS)
 *   Stack    dynamic array LIFO stack             (DFS)
 *   DSU      disjoint-set union / union-find      (Kruskal)
 *   merge_sort_edges  stable O(E log E) sort      (Kruskal)
 */
#ifndef DS_H
#define DS_H

/* ---------------------------------------------------------------- min-heap */
typedef struct {
    int  node;
    long key;
} HeapItem;

typedef struct {
    HeapItem *a;
    int       size;
    int       cap;
} MinHeap;

void heap_init(MinHeap *h, int cap);
void heap_free(MinHeap *h);
int  heap_empty(const MinHeap *h);
void heap_push(MinHeap *h, int node, long key);    /* O(log n) sift-up   */
int  heap_pop(MinHeap *h, HeapItem *out);          /* O(log n) sift-down */

/* ---------------------------------------------------------- circular queue */
typedef struct {
    int *a;
    int  head;   /* index of the front element */
    int  size;
    int  cap;
} Queue;

void queue_init(Queue *q, int cap);
void queue_free(Queue *q);
int  queue_empty(const Queue *q);
int  queue_push(Queue *q, int v);                  /* enqueue at the back */
int  queue_pop(Queue *q, int *out);                /* dequeue the front   */
int  queue_at(const Queue *q, int i);              /* i-th from the front */

/* ------------------------------------------------------------------- stack */
typedef struct {
    int *a;
    int  top;    /* number of elements */
    int  cap;
} Stack;

void stack_init(Stack *s, int cap);
void stack_free(Stack *s);
int  stack_empty(const Stack *s);
void stack_push(Stack *s, int v);
int  stack_pop(Stack *s, int *out);

/* -------------------------------------------------------------- union-find */
typedef struct {
    int parent[64];
    int rank[64];
} DSU;

void dsu_init(DSU *d, int n);
int  dsu_find(DSU *d, int x);                      /* with path compression */
int  dsu_union(DSU *d, int a, int b);              /* union by rank; 0 if already joined */

/* ------------------------------------------------------------------ sorting */
typedef struct {
    int u, v, w;
} WEdge;

void merge_sort_edges(WEdge *a, int n);            /* by weight, then u, then v */

#endif
