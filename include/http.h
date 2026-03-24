#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

typedef struct {
    char method[8];
    char path[1024];
    char version[16];
    char connection[32];
    char content_type[64];
    size_t content_length;
    char *body;
} http_request_t;

int http_read_request(int fd, http_request_t *req);
void http_free_request(http_request_t *req);
const char *http_status_text(int status_code);
int http_send_response(int fd, int status_code, const char *content_type, const void *body, size_t body_len, int close_conn);
int http_send_file_response(int fd, const char *filepath, int close_conn);

#endif
