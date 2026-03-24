#include "database.h"

#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct db_handle {
    sqlite3 *conn;
    pthread_mutex_t mutex;
};

db_handle_t *db_open(const char *path)
{
    db_handle_t *db = (db_handle_t *)calloc(1, sizeof(db_handle_t));
    if (db == NULL) {
        return NULL;
    }

    if (sqlite3_open(path, &db->conn) != SQLITE_OK) {
        sqlite3_close(db->conn);
        free(db);
        return NULL;
    }

    pthread_mutex_init(&db->mutex, NULL);
    return db;
}

void db_close(db_handle_t *db)
{
    if (db == NULL) {
        return;
    }

    if (db->conn != NULL) {
        sqlite3_close(db->conn);
        db->conn = NULL;
    }

    pthread_mutex_destroy(&db->mutex);
    free(db);
}

int db_init_schema(db_handle_t *db)
{
    if (db == NULL) {
        return -1;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL UNIQUE,"
        "password_hash TEXT NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    char *errmsg = NULL;
    pthread_mutex_lock(&db->mutex);
    int rc = sqlite3_exec(db->conn, sql, NULL, NULL, &errmsg);
    pthread_mutex_unlock(&db->mutex);

    if (rc != SQLITE_OK) {
        if (errmsg != NULL) {
            fprintf(stderr, "db_init_schema error: %s\n", errmsg);
            sqlite3_free(errmsg);
        }
        return -1;
    }

    return 0;
}

int db_create_user(db_handle_t *db, const char *username, const char *password_hash)
{
    if (db == NULL || username == NULL || password_hash == NULL) {
        return -1;
    }

    static const char *sql = "INSERT INTO users(username, password_hash) VALUES(?, ?);";
    sqlite3_stmt *stmt = NULL;

    pthread_mutex_lock(&db->mutex);
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password_hash, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
    }

    if (stmt != NULL) {
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db->mutex);

    if (rc == SQLITE_CONSTRAINT) {
        return 1;
    }
    if (rc != SQLITE_DONE) {
        return -1;
    }
    return 0;
}

int db_check_user(db_handle_t *db, const char *username, const char *password_hash, int *matched)
{
    if (db == NULL || username == NULL || password_hash == NULL || matched == NULL) {
        return -1;
    }

    *matched = 0;
    static const char *sql = "SELECT password_hash FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;

    pthread_mutex_lock(&db->mutex);
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
    }

    if (rc == SQLITE_ROW) {
        const unsigned char *stored = sqlite3_column_text(stmt, 0);
        if (stored != NULL && strcmp((const char *)stored, password_hash) == 0) {
            *matched = 1;
        }
        rc = SQLITE_DONE;
    }

    if (stmt != NULL) {
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db->mutex);

    if (rc != SQLITE_DONE) {
        return -1;
    }
    return 0;
}
