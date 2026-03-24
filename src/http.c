#define _GNU_SOURCE

#include "http.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define REQ_BUF_CAP (64 * 1024)

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent_total = 0;

    while (sent_total < len) {
        ssize_t sent = send(fd, p + sent_total, len - sent_total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        sent_total += (size_t)sent;
    }

    return 0;
}

static const char *find_header_end(const char *buf, size_t len)
{
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return buf + i;
        }
    }
    return NULL;
}

static void trim_spaces(char *s)
{
    char *start = s;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int parse_start_line(char *line, http_request_t *req)
{
    if (sscanf(line, "%7s %1023s %15s", req->method, req->path, req->version) != 3) {
        return -1;
    }
    return 0;
}

static int parse_headers(char *headers, http_request_t *req)
{
    req->content_length = 0;
    req->connection[0] = '\0';
    req->content_type[0] = '\0';

    char *saveptr = NULL;
    char *line = strtok_r(headers, "\r\n", &saveptr);
    int first = 1;

    while (line != NULL) {
        if (first) {
            if (parse_start_line(line, req) != 0) {
                return -1;
            }
            first = 0;
        } else {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                *colon = '\0';
                char *key = line;
                char *value = colon + 1;
                trim_spaces(key);
                trim_spaces(value);

                if (strcasecmp(key, "Content-Length") == 0) {
                    req->content_length = (size_t)strtoull(value, NULL, 10);
                } else if (strcasecmp(key, "Connection") == 0) {
                    snprintf(req->connection, sizeof(req->connection), "%s", value);
                } else if (strcasecmp(key, "Content-Type") == 0) {
                    snprintf(req->content_type, sizeof(req->content_type), "%s", value);
                }
            }
        }

        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    return 0;
}

int http_read_request(int fd, http_request_t *req)
{
    memset(req, 0, sizeof(*req));

    char *buf = (char *)malloc(REQ_BUF_CAP);
    if (buf == NULL) {
        return -1;
    }

    size_t used = 0;
    const char *header_end = NULL;

    while (used < REQ_BUF_CAP) {
        ssize_t n = recv(fd, buf + used, REQ_BUF_CAP - used, 0);
        if (n == 0) {
            free(buf);
            return -1;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return -1;
        }

        used += (size_t)n;
        header_end = find_header_end(buf, used);
        if (header_end != NULL) {
            break;
        }
    }

    if (header_end == NULL) {
        free(buf);
        return -1;
    }

    size_t header_len = (size_t)(header_end - buf);
    size_t body_offset = header_len + 4;

    char *header_copy = (char *)malloc(header_len + 1);
    if (header_copy == NULL) {
        free(buf);
        return -1;
    }
    memcpy(header_copy, buf, header_len);
    header_copy[header_len] = '\0';

    if (parse_headers(header_copy, req) != 0) {
        free(header_copy);
        free(buf);
        return -1;
    }
    free(header_copy);

    if (req->content_length > 0) {
        req->body = (char *)calloc(req->content_length + 1, 1);
        if (req->body == NULL) {
            free(buf);
            return -1;
        }

        size_t already = 0;
        if (used > body_offset) {
            already = used - body_offset;
            if (already > req->content_length) {
                already = req->content_length;
            }
            memcpy(req->body, buf + body_offset, already);
        }

        while (already < req->content_length) {
            ssize_t n = recv(fd, req->body + already, req->content_length - already, 0);
            if (n <= 0) {
                free(buf);
                http_free_request(req);
                return -1;
            }
            already += (size_t)n;
        }
    }

    free(buf);
    return 0;
}

void http_free_request(http_request_t *req)
{
    if (req == NULL) {
        return;
    }
    free(req->body);
    req->body = NULL;
}

const char *http_status_text(int status_code)
{
    switch (status_code) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 409:
        return "Conflict";
    case 500:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}

int http_send_response(int fd, int status_code, const char *content_type, const void *body, size_t body_len, int close_conn)
{
    char header[512];
    int n = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n\r\n",
        status_code,
        http_status_text(status_code),
        content_type != NULL ? content_type : "text/plain; charset=utf-8",
        body_len,
        close_conn ? "close" : "keep-alive"
    );

    if (n <= 0 || (size_t)n >= sizeof(header)) {
        return -1;
    }

    if (send_all(fd, header, (size_t)n) != 0) {
        return -1;
    }

    if (body_len > 0 && body != NULL) {
        if (send_all(fd, body, body_len) != 0) {
            return -1;
        }
    }

    return 0;
}

static const char *guess_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }

    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcasecmp(ext, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcasecmp(ext, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcasecmp(ext, ".json") == 0) {
        return "application/json; charset=utf-8";
    }
    if (strcasecmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcasecmp(ext, ".gif") == 0) {
        return "image/gif";
    }
    if (strcasecmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }

    return "application/octet-stream";
}

int http_send_file_response(int fd, const char *filepath, int close_conn)
{
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        return http_send_response(fd, 404, "text/plain; charset=utf-8", "Not Found", 9, close_conn);
    }

    struct stat st;
    if (fstat(file_fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(file_fd);
        return http_send_response(fd, 404, "text/plain; charset=utf-8", "Not Found", 9, close_conn);
    }

    const char *mime = guess_mime_type(filepath);
    char header[512];
    int n = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: %s\r\n\r\n",
        mime,
        (long long)st.st_size,
        close_conn ? "close" : "keep-alive"
    );

    if (n <= 0 || (size_t)n >= sizeof(header)) {
        close(file_fd);
        return -1;
    }

    if (send_all(fd, header, (size_t)n) != 0) {
        close(file_fd);
        return -1;
    }

    off_t offset = 0;
    while (offset < st.st_size) {
        ssize_t sent = sendfile(fd, file_fd, &offset, (size_t)(st.st_size - offset));
        if (sent <= 0) {
            close(file_fd);
            return -1;
        }
    }

    close(file_fd);
    return 0;
}
