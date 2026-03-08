/*
 * tsort — topological sort
 *
 * Usage: tsort [FILE]
 *   Reads pairs of items from FILE (or stdin) and writes them in
 *   topologically sorted order, one item per line.
 *   Items on the same line: "A B" means A must come before B.
 *   --version / --help
 *
 * Exit: 0 = success, 1 = cycle detected or error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "1.0"
#define INIT_CAP 64

/* ── Node table ──────────────────────────────────────────────── */

typedef struct Edge {
    int to;
    struct Edge *next;
} Edge;

typedef struct {
    char *name;
    Edge *out;    /* edges to successors */
    int   indeg;  /* in-degree */
    int   visited;
} Node;

static Node  *g_nodes = NULL;
static int    g_nnodes = 0, g_cap = 0;

static int find_or_add(const char *name) {
    for (int i = 0; i < g_nnodes; i++)
        if (!strcmp(g_nodes[i].name, name)) return i;
    if (g_nnodes == g_cap) {
        g_cap = g_cap ? g_cap * 2 : INIT_CAP;
        g_nodes = realloc(g_nodes, (size_t)g_cap * sizeof(Node));
        if (!g_nodes) { perror("tsort"); exit(2); }
    }
    int idx = g_nnodes++;
    g_nodes[idx].name = strdup(name);
    g_nodes[idx].out = NULL;
    g_nodes[idx].indeg = 0;
    g_nodes[idx].visited = 0;
    return idx;
}

static void add_edge(int from, int to) {
    /* avoid duplicate edges */
    for (Edge *e = g_nodes[from].out; e; e = e->next)
        if (e->to == to) return;
    Edge *e = malloc(sizeof(Edge));
    if (!e) { fprintf(stderr, "tsort: out of memory\n"); exit(1); }
    e->to = to; e->next = g_nodes[from].out;
    g_nodes[from].out = e;
    g_nodes[to].indeg++;
}

/* ── Kahn's algorithm ────────────────────────────────────────── */

static int toposort(void) {
    /* queue of nodes with indeg 0 */
    int *queue = malloc((size_t)g_nnodes * sizeof(int));
    if (!queue) { fprintf(stderr, "tsort: out of memory\n"); exit(1); }
    int head = 0, tail = 0;

    for (int i = 0; i < g_nnodes; i++)
        if (g_nodes[i].indeg == 0) queue[tail++] = i;

    int count = 0;
    while (head < tail) {
        int u = queue[head++];
        printf("%s\n", g_nodes[u].name);
        count++;
        for (Edge *e = g_nodes[u].out; e; e = e->next) {
            if (--g_nodes[e->to].indeg == 0)
                queue[tail++] = e->to;
        }
    }
    free(queue);

    if (count != g_nnodes) {
        fprintf(stderr, "tsort: cycle detected\n");
        /* print the cycling nodes anyway */
        for (int i = 0; i < g_nnodes; i++)
            if (g_nodes[i].indeg > 0)
                fprintf(stderr, "tsort: %s\n", g_nodes[i].name);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("tsort %s (Winix)\n", VERSION); return 0; }
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr,
                "usage: tsort [FILE]\n\n"
                "Topologically sort pairs of items read from FILE (or stdin).\n"
                "Input format: pairs 'A B' meaning A must precede B.\n\n"
                "      --version\n"
                "      --help\n");
            return 0;
        }
    }

    FILE *fp = stdin;
    if (argc > 1 && strcmp(argv[1], "-")) {
        fp = fopen(argv[1], "r");
        if (!fp) { perror(argv[1]); return 1; }
    }

    char tok[4096];
    char pending[4096]; int have_pending = 0;

    while (fscanf(fp, "%4095s", tok) == 1) {
        if (have_pending) {
            int a = find_or_add(pending);
            int b = find_or_add(tok);
            if (a != b) add_edge(a, b);
            have_pending = 0;
        } else {
            strcpy(pending, tok);
            have_pending = 1;
        }
    }
    if (have_pending) {
        /* odd item — treat as self-loop (node with no edges) */
        find_or_add(pending);
    }
    if (fp != stdin) fclose(fp);

    int ret = toposort();

    /* cleanup */
    for (int i = 0; i < g_nnodes; i++) {
        free(g_nodes[i].name);
        Edge *e = g_nodes[i].out;
        while (e) { Edge *nx = e->next; free(e); e = nx; }
    }
    free(g_nodes);
    return ret;
}
