#ifndef DATABASE_H
#define DATABASE_H

#include <stddef.h>

/* 数据库句柄对外只暴露为不透明类型，避免外部直接依赖 sqlite3 细节。 */
typedef struct db_handle db_handle_t;

/* 打开或创建数据库连接，失败时返回 NULL。 */
db_handle_t *db_open(const char *path);
/* 关闭数据库连接并释放句柄。 */
void db_close(db_handle_t *db);
/* 初始化用户表结构。 */
int db_init_schema(db_handle_t *db);
/* 创建新用户，用户名重复时返回 1。 */
int db_create_user(db_handle_t *db, const char *username, const char *password_hash);
/* 校验用户名和密码哈希是否匹配，matched 用于返回结果。 */
int db_check_user(db_handle_t *db, const char *username, const char *password_hash, int *matched);

#endif
