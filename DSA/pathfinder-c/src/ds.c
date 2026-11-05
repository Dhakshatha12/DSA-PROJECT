/* ds.c - implementations of the core data structures. */
#include "ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return q;
}

/* ================================================================ min-heap */

void heap_init(MinHeap *h, int cap)
{
    h->cap = cap > 4 ? cap : 4;
    h->size = 0;
    h->a = xrealloc(NULL, sizeof(HeapItem) * (size_t)h->cap);
}

void heap_free(MinHeap *h)
{
    free(h->a);
    h->a = NULL;
    h->size = h->cap = 0;
}

int heap_empty(const MinHeap *h)
{
    return h->size == 0;
}

static int heap_less(const HeapItem *x, const HeapItem *y)
{
    /* tie-break on node id so results are deterministic */
    return x->key < y->key || (x->key == y->key && x->node < y->node);
}

void heap_push(MinHeap *h, int node, long key)
{
    if (h->size == h->cap) {
        h->cap *= 2;
        h->a = xrealloc(h->a, sizeof(HeapItem) * (size_t)h->cap);
    }
    int i = h->size++;
    h->a[i].node = node;
    h->a[i].key = key;
    while (i > 0) {                          /* sift up */
        int parent = (i - 1) / 2;
        if (!heap_less(&h->a[i], &h->a[parent]))
            break;
        HeapItem t = h->a[i];
        h->a[i] = h->a[parent];
        h->a[parent] = t;
        i = parent;
    }
}

int heap_pop(MinHeap *h, HeapItem *out)
{
    if (h->size == 0)
        return 0;
    *out = h->a[0];
    h->a[0] = h->a[--h->size];
    int i = 0;
    for (;;) {                               /* sift down */
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < h->size && heap_less(&h->a[l], &h->a[m]))
            m = l;
        if (r < h->size && heap_less(&h->a[r], &h->a[m]))
            m = r;
        if (m == i)
            break;
        HeapItem t = h->a[i];
        h->a[i] = h->a[m];
        h->a[m] = t;
        i = m;
    }
    return 1;
}

/* ================================================================== queue */

void queue_init(Queue *q, int cap)
{
    q->cap = cap > 4 ? cap : 4;
    q->head = q->size = 0;
    q->a = xrealloc(NULL, sizeof(int) * (size_t)q->cap);
}

void queue_free(Queue *q)
{
    free(q->a);
    q->a = NULL;
    q->cap = q->size = q->head = 0;
}

int queue_empty(const Queue *q)
{
    return q->size == 0;
}

int queue_push(Queue *q, int v)
{
    if (q->size == q->cap)
        return -1;                           /* full: caller sized it to V */
    q->a[(q->head + q->size) % q->cap] = v;
    q->size++;
    return 0;
}

int queue_pop(Queue *q, int *out)
{
    if (q->size == 0)
        return 0;
    *out = q->a[q->head];
    q->head = (q->head + 1) % q->cap;
    q->size--;
    return 1;
}

int queue_at(const Queue *q, int i)
{
    return q->a[(q->head + i) % q->cap];
}

/* ================================================================== stack */

void stack_init(Stack *s, int cap)
{
    s->cap = cap > 4 ? cap : 4;
    s->top = 0;
    s->a = xrealloc(NULL, sizeof(int) * (size_t)s->cap);
}

void stack_free(Stack *s)
{
    free(s->a);
    s->a = NULL;
    s->cap = s->top = 0;
}

int stack_empty(const Stack *s)
{
    return s->top == 0;
}

void stack_push(Stack *s, int v)
{
    if (s->top == s->cap) {
        s->cap *= 2;
        s->a = xrealloc(s->a, sizeof(int) * (size_t)s->cap);
    }
    s->a[s->top++] = v;
}

int stack_pop(Stack *s, int *out)
{
    if (s->top == 0)
        return 0;
    *out = s->a[--s->top];
    return 1;
}

/* ============================================================= union-find */

void dsu_init(DSU *d, int n)
{
    for (int i = 0; i < n && i < 64; i++) {
        d->parent[i] = i;
        d->rank[i] = 0;
    }
}

int dsu_find(DSU *d, int x)
{
    int root = x;
    while (d->parent[root] != root)
        root = d->parent[root];
    while (d->parent[x] != root) {           /* path compression */
        int next = d->parent[x];
        d->parent[x] = root;
        x = next;
    }
    return root;
}

int dsu_union(DSU *d, int a, int b)
{
    a = dsu_find(d, a);
    b = dsu_find(d, b);
    if (a == b)
        return 0;                            /* would create a cycle */
    if (d->rank[a] < d->rank[b]) {
        int t = a;
        a = b;
        b = t;
    }
    d->parent[b] = a;
    if (d->rank[a] == d->rank[b])
        d->rank[a]++;
    return 1;
}

/* ================================================================ sorting */

static int edge_le(const WEdge *x, const WEdge *y)
{
    if (x->w != y->w) return x->w < y->w;
    if (x->u != y->u) return x->u < y->u;
    return x->v <= y->v;
}

static void merge_rec(WEdge *a, WEdge *tmp, int lo, int hi)
{
    if (hi - lo < 2)
        return;
    int mid = (lo + hi) / 2;
    merge_rec(a, tmp, lo, mid);
    merge_rec(a, tmp, mid, hi);
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        tmp[k++] = edge_le(&a[i], &a[j]) ? a[i++] : a[j++];
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi)  tmp[k++] = a[j++];
    memcpy(a + lo, tmp + lo, sizeof(WEdge) * (size_t)(hi - lo));
}

void merge_sort_edges(WEdge *a, int n)
{
    if (n < 2)
        return;
    WEdge *tmp = xrealloc(NULL, sizeof(WEdge) * (size_t)n);
    merge_rec(a, tmp, 0, n);
    free(tmp);
}
