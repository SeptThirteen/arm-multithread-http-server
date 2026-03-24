#ifndef DATABASE_H
#define DATABASE_H

#include <stddef.h>

typedef struct db_handle db_handle_t;

db_handle_t *db_open(const char *path);
void db_close(db_handle_t *db);
int db_init_schema(db_handle_t *db);
int db_create_user(db_handle_t *db, const char *username, const char *password_hash);
int db_check_user(db_handle_t *db, const char *username, const char *password_hash, int *matched);

#endif
