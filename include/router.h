#ifndef ROUTER_H
#define ROUTER_H

#include "database.h"
#include "http.h"

typedef struct {
    db_handle_t *db;
    const char *static_root;
} server_context_t;

void handle_client_connection(int client_fd, void *user_data);
int router_handle_request(int client_fd, const http_request_t *req, server_context_t *ctx);

#endif
