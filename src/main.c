#define _POSIX_C_SOURCE 200809L

#include "database.h"
#include "router.h"
#include "thread_pool.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static int g_listen_fd = -1;

static void on_sigint(int signo)
{
    (void)signo;
    g_stop = 1;

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

static int create_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 128) != 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char **argv)
{
    int port = 8080;
    size_t workers = 8;

    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (argc > 2) {
        workers = (size_t)atoi(argv[2]);
    }

    const char *static_root = "www";
    const char *db_path = "users.db";

    signal(SIGINT, on_sigint);

    db_handle_t *db = db_open(db_path);
    if (db == NULL) {
        fprintf(stderr, "failed to open database: %s\n", db_path);
        return 1;
    }

    if (db_init_schema(db) != 0) {
        fprintf(stderr, "failed to init database schema\n");
        db_close(db);
        return 1;
    }

    server_context_t ctx;
    ctx.db = db;
    ctx.static_root = static_root;

    thread_pool_t *pool = thread_pool_create(workers, handle_client_connection, &ctx);
    if (pool == NULL) {
        fprintf(stderr, "failed to create thread pool\n");
        db_close(db);
        return 1;
    }

    g_listen_fd = create_listen_socket(port);
    if (g_listen_fd < 0) {
        thread_pool_destroy(pool);
        db_close(db);
        return 1;
    }

    printf("server started on 0.0.0.0:%d with %zu workers\n", port, workers);

    while (!g_stop) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (g_stop || errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        if (thread_pool_submit(pool, client_fd) != 0) {
            close(client_fd);
        }
    }

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    thread_pool_destroy(pool);
    db_close(db);

    puts("server stopped");
    return 0;
}
