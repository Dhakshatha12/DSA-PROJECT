/* server.h - minimal single-threaded HTTP/1.1 server (POSIX sockets / Winsock). */
#ifndef SERVER_H
#define SERVER_H

#include "graph.h"

typedef struct {
    int         port;
    const char *public_dir;     /* static files: index.html, app.js, style.css */
    const char *graph_file;     /* current graph, saved after every edit        */
    const char *default_file;   /* dataset restored by "Reset"                  */
    int         open_browser;   /* 1 = open http://localhost:<port> on start    */
} ServerConfig;

int server_run(const ServerConfig *cfg, Graph *g);   /* blocks until error */

#endif
