#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* 解析后的 HTTP 请求结构体，保存常用请求头和请求体。 */
typedef struct {
    /* 请求方法，例如 GET 或 POST。 */
    char method[8];
    /* 请求路径，例如 / 或 /api/login。 */
    char path[1024];
    /* HTTP 协议版本字符串。 */
    char version[16];
    /* Connection 请求头，用于判断是否保持长连接。 */
    char connection[32];
    /* Content-Type 请求头。 */
    char content_type[64];
    /* 请求体长度。 */
    size_t content_length;
    /* 请求体内容，由调用者在释放时通过 http_free_request 回收。 */
    char *body;
} http_request_t;

/* 从 socket 读取并解析完整 HTTP 请求。 */
int http_read_request(int fd, http_request_t *req);
/* 释放请求体缓冲区。 */
void http_free_request(http_request_t *req);
/* 将状态码转换为文本描述。 */
const char *http_status_text(int status_code);
/* 发送普通 HTTP 响应。 */
int http_send_response(int fd, int status_code, const char *content_type, const void *body, size_t body_len, int close_conn);
/* 按文件内容发送静态资源响应。 */
int http_send_file_response(int fd, const char *filepath, int close_conn);

#endif
