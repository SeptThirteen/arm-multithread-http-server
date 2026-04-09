#ifndef ROUTER_H
#define ROUTER_H

#include "database.h"
#include "http.h"

/* 服务器运行时上下文，路由处理时会用到数据库和静态资源根目录。 */
typedef struct {
    /* 用户数据库句柄。 */
    db_handle_t *db;
    /* 静态文件根目录，例如 www。 */
    const char *static_root;
} server_context_t;

/* 处理单个客户端连接的入口，通常由线程池工作线程调用。 */
void handle_client_connection(int client_fd, void *user_data);
/* 根据请求方法和路径分发具体处理逻辑。 */
int router_handle_request(int client_fd, const http_request_t *req, server_context_t *ctx);

#endif
