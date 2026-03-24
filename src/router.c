#define _POSIX_C_SOURCE 200809L

#include "router.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int should_close_connection(const http_request_t *req)
{
    if (req->connection[0] == '\0') {
        return 1;
    }
    return strcasecmp(req->connection, "keep-alive") != 0;
}

static int is_path_safe(const char *path)
{
    return strstr(path, "..") == NULL;
}

static void simple_hash(const char *input, char out_hex[17])
{
    unsigned long long hash = 1469598103934665603ULL;
    while (*input != '\0') {
        hash ^= (unsigned char)*input;
        hash *= 1099511628211ULL;
        input++;
    }
    snprintf(out_hex, 17, "%016llx", hash);
}

static int form_get_value(const char *body, const char *key, char *out, size_t out_len)
{
    if (body == NULL || key == NULL || out == NULL || out_len == 0) {
        return -1;
    }

    size_t key_len = strlen(key);
    const char *p = body;

    while (*p != '\0') {
        const char *amp = strchr(p, '&');
        size_t segment_len = amp != NULL ? (size_t)(amp - p) : strlen(p);

        if (segment_len > key_len + 1 && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t value_len = segment_len - key_len - 1;
            if (value_len >= out_len) {
                value_len = out_len - 1;
            }
            memcpy(out, p + key_len + 1, value_len);
            out[value_len] = '\0';

            for (size_t i = 0; out[i] != '\0'; i++) {
                if (out[i] == '+') {
                    out[i] = ' ';
                }
            }
            return 0;
        }

        if (amp == NULL) {
            break;
        }
        p = amp + 1;
    }

    return -1;
}

static int send_json(int fd, int status, const char *json, int close_conn)
{
    return http_send_response(fd, status, "application/json; charset=utf-8", json, strlen(json), close_conn);
}

static int handle_register(int fd, const http_request_t *req, server_context_t *ctx)
{
    char username[128] = {0};
    char password[128] = {0};

    if (form_get_value(req->body, "username", username, sizeof(username)) != 0 ||
        form_get_value(req->body, "password", password, sizeof(password)) != 0) {
        return send_json(fd, 400, "{\"ok\":false,\"message\":\"invalid form\"}", 1);
    }

    char pwd_hash[17] = {0};
    simple_hash(password, pwd_hash);

    int rc = db_create_user(ctx->db, username, pwd_hash);
    if (rc == 1) {
        return send_json(fd, 409, "{\"ok\":false,\"message\":\"username exists\"}", 1);
    }
    if (rc != 0) {
        return send_json(fd, 500, "{\"ok\":false,\"message\":\"db error\"}", 1);
    }

    return send_json(fd, 201, "{\"ok\":true,\"message\":\"registered\"}", 1);
}

static int handle_login(int fd, const http_request_t *req, server_context_t *ctx)
{
    char username[128] = {0};
    char password[128] = {0};

    if (form_get_value(req->body, "username", username, sizeof(username)) != 0 ||
        form_get_value(req->body, "password", password, sizeof(password)) != 0) {
        return send_json(fd, 400, "{\"ok\":false,\"message\":\"invalid form\"}", 1);
    }

    char pwd_hash[17] = {0};
    simple_hash(password, pwd_hash);

    int matched = 0;
    if (db_check_user(ctx->db, username, pwd_hash, &matched) != 0) {
        return send_json(fd, 500, "{\"ok\":false,\"message\":\"db error\"}", 1);
    }

    if (!matched) {
        return send_json(fd, 403, "{\"ok\":false,\"message\":\"invalid credentials\"}", 1);
    }

    return send_json(fd, 200, "{\"ok\":true,\"message\":\"login success\"}", 1);
}

static int serve_static_file(int fd, const char *url_path, server_context_t *ctx, int close_conn)
{
    if (!is_path_safe(url_path)) {
        return http_send_response(fd, 403, "text/plain; charset=utf-8", "Forbidden", 9, close_conn);
    }

    const char *path = url_path;
    if (strcmp(path, "/") == 0) {
        path = "/index.html";
    }

    char filepath[2048];
    snprintf(filepath, sizeof(filepath), "%s%s", ctx->static_root, path);

    return http_send_file_response(fd, filepath, close_conn);
}

int router_handle_request(int client_fd, const http_request_t *req, server_context_t *ctx)
{
    int close_conn = should_close_connection(req);

    if (strcmp(req->method, "GET") == 0) {
        return serve_static_file(client_fd, req->path, ctx, close_conn);
    }

    if (strcmp(req->method, "POST") == 0) {
        if (strcmp(req->path, "/api/register") == 0) {
            return handle_register(client_fd, req, ctx);
        }
        if (strcmp(req->path, "/api/login") == 0) {
            return handle_login(client_fd, req, ctx);
        }
        return http_send_response(client_fd, 404, "text/plain; charset=utf-8", "Not Found", 9, close_conn);
    }

    return http_send_response(client_fd, 405, "text/plain; charset=utf-8", "Method Not Allowed", 18, close_conn);
}

void handle_client_connection(int client_fd, void *user_data)
{
    server_context_t *ctx = (server_context_t *)user_data;

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    http_request_t req;
    if (http_read_request(client_fd, &req) == 0) {
        router_handle_request(client_fd, &req, ctx);
        http_free_request(&req);
    } else {
        http_send_response(client_fd, 400, "text/plain; charset=utf-8", "Bad Request", 11, 1);
    }

    close(client_fd);
}
