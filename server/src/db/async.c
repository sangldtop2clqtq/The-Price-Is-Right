#include "pr_db.h"
#include "pr_log.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DB_QUEUE_LIMIT 64
#define DB_TIMEOUT 5.0
typedef struct job {
    const char *sql;
    int count;
    char *params[3];
    pr_db_callback callback;
    void *context;
    PGresult *result;
    struct job *next;
    double queued_at;
} job;
struct pr_db {
    PGconn *connection;
    char *conninfo;
    int state; /* 0 disconnected, 1 connecting, 2 online */
    PostgresPollingStatusType poll;
    int flushing, active, schema_ready;
    size_t count;
    double deadline, retry_at, now;
    job *head, *tail;
};
static void finish_job(pr_db *db, PGresult *result) {
    job *j = db->head;
    db->head = j->next;
    if (!db->head) db->tail = NULL;
    --db->count;
    db->active = 0;
    pr_log_event("db_result", 0, 0, NULL, 0, 0,
                 result ? (int)PQresultStatus(result) : -1,
                 pr_server_now() - j->queued_at, result ? "complete" : "unavailable");
    j->callback(j->context, result);
    if (j->result) PQclear(j->result);
    for (int i = 0; i < j->count; ++i) free(j->params[i]);
    free(j);
}
static void fail_connection(pr_db *db, double now) {
    pr_log_event("db_disconnect", 0, 0, NULL, 0, 0, 0, 0, "connection_reset");
    if (db->connection) PQfinish(db->connection);
    db->connection = NULL;
    db->state = 0;
    db->schema_ready = 0;
    db->flushing = 0;
    db->retry_at = now + 2.0;
    while (db->head) finish_job(db, NULL);
}
/* Avoid DNS resolution blocking PQconnectStart in the network event loop. */
static int safe_endpoint(const char *conninfo) {
    char *error = NULL;
    PQconninfoOption *options = PQconninfoParse(conninfo, &error);
    if (!options) { PQfreemem(error); return 0; }
    const char *host = NULL, *hostaddr = NULL;
    for (PQconninfoOption *o = options; o->keyword; ++o) {
        if (!strcmp(o->keyword, "host")) host = o->val;
        if (!strcmp(o->keyword, "hostaddr")) hostaddr = o->val;
    }
    struct in_addr address;
    int valid = hostaddr && *hostaddr ? inet_pton(AF_INET, hostaddr, &address) == 1 :
        host && *host && !strchr(host, ',') && (*host == '/' || inet_pton(AF_INET, host, &address) == 1);
    PQconninfoFree(options);
    return valid;
}
pr_db *pr_db_create(const char *conninfo) {
    pr_db *db = calloc(1, sizeof(*db));
    if (!db) return NULL;
    if (conninfo && *conninfo) {
        if (!safe_endpoint(conninfo)) {
            fprintf(stderr, "PR_DATABASE_URL requires an explicit Unix socket host or numeric IPv4 host/hostaddr\n");
            free(db); return NULL;
        }
        db->conninfo = strdup(conninfo);
        if (!db->conninfo) { free(db); return NULL; }
    }
    return db;
}
void pr_db_destroy(pr_db *db) {
    if (!db) return;
    fail_connection(db, 0);
    free(db->conninfo);
    free(db);
}
int pr_db_submit(pr_db *db, const char *sql, int count, const char *const *params,
                 pr_db_callback callback, void *context) {
    if (!db || db->state != 2 || !db->schema_ready || db->count >= DB_QUEUE_LIMIT || count < 0 || count > 3) return -1;
    job *j = calloc(1, sizeof(*j));
    if (!j) return -1;
    j->queued_at = pr_server_now();
    j->sql = sql; j->count = count; j->callback = callback; j->context = context;
    size_t total = 0;
    for (int i = 0; i < count; ++i) {
        size_t n = params && params[i] ? strlen(params[i]) + 1 : 65537;
        if (n > 65536 - total || !(j->params[i] = strdup(params[i]))) {
            for (int k = 0; k < i; ++k) free(j->params[k]);
            free(j); return -1;
        }
        total += n;
    }
    if (db->tail) db->tail->next = j; else db->head = j;
    db->tail = j;
    ++db->count;
    return 0;
}
static void schema_checked(void *context, PGresult *result) {
    pr_db *db = context;
    if (result && PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1) {
        db->schema_ready = 1;
        pr_log_event("db_ready", 0, 0, NULL, 0, 0, 0, 0, "schema_v2");
        fprintf(stderr, "DB ready (schema v2)\n");
    } else {
        fprintf(stderr, "DB schema unavailable; apply migrations\n");
        fail_connection(db, db->now);
    }
}
static void connect_poll(pr_db *db, double now) {
    db->poll = PQconnectPoll(db->connection);
    if (db->poll == PGRES_POLLING_OK) {
        if (PQsetnonblocking(db->connection, 1) != 0) { fail_connection(db, now); return; }
        db->state = 2;
        job *j = calloc(1, sizeof(*j));
        if (!j) { fail_connection(db, now); return; }
        j->queued_at = now;
        j->sql = "SELECT version FROM schema_migrations WHERE version=2";
        j->callback = schema_checked; j->context = db;
        db->head = db->tail = j; db->count = 1;
    } else if (db->poll == PGRES_POLLING_FAILED) fail_connection(db, now);
}
void pr_db_tick(pr_db *db, double now) {
    if (!db || !db->conninfo) return;
    db->now = now;
    if (db->state == 0 && now >= db->retry_at) {
        db->connection = PQconnectStart(db->conninfo);
        if (!db->connection || PQstatus(db->connection) == CONNECTION_BAD) {
            fail_connection(db, now); return;
        }
        db->state = 1; db->deadline = now + DB_TIMEOUT;
        connect_poll(db, now);
    }
    if ((db->state == 1 || db->active) && now >= db->deadline) {
        pr_log_event("db_timeout", 0, 0, NULL, 0, 0, -1, DB_TIMEOUT, "deadline_expired");
        fprintf(stderr, "DB timeout; pending operations failed\n");
        fail_connection(db, now); return;
    }
    if (db->state == 2 && db->head && !db->active) {
        job *j = db->head;
        const char *params[3];
        for (int i = 0; i < j->count; ++i) params[i] = j->params[i];
        if (!PQsendQueryParams(db->connection, j->sql, j->count, NULL, params, NULL, NULL, 0)) {
            fail_connection(db, now); return;
        }
        db->active = 1; db->deadline = now + DB_TIMEOUT;
        db->flushing = PQflush(db->connection);
        if (db->flushing < 0) fail_connection(db, now);
    }
}
void pr_db_watch(pr_db *db, fd_set *reads, fd_set *writes, int *max_fd) {
    if (!db || !db->connection) return;
    int fd = PQsocket(db->connection);
    if (fd < 0 || fd >= FD_SETSIZE) return;
    if (db->state == 2 || db->poll == PGRES_POLLING_READING) FD_SET(fd, reads);
    if ((db->state == 1 && db->poll == PGRES_POLLING_WRITING) || db->flushing) FD_SET(fd, writes);
    if (fd > *max_fd) *max_fd = fd;
}
void pr_db_ready(pr_db *db, const fd_set *reads, const fd_set *writes, double now) {
    if (!db || !db->connection) return;
    db->now = now;
    int fd = PQsocket(db->connection);
    if (fd < 0 || fd >= FD_SETSIZE) { fail_connection(db, now); return; }
    if (db->state == 1) {
        if ((db->poll == PGRES_POLLING_READING && FD_ISSET(fd, reads)) ||
            (db->poll == PGRES_POLLING_WRITING && FD_ISSET(fd, writes))) connect_poll(db, now);
        return;
    }
    if (FD_ISSET(fd, writes)) {
        db->flushing = PQflush(db->connection);
        if (db->flushing < 0) { fail_connection(db, now); return; }
    }
    if (FD_ISSET(fd, reads) && !PQconsumeInput(db->connection)) { fail_connection(db, now); return; }
    while (db->active && !PQisBusy(db->connection)) {
        PGresult *result = PQgetResult(db->connection);
        if (result) {
            if (db->head->result) PQclear(db->head->result);
            db->head->result = result;
        } else finish_job(db, db->head->result);
    }
}
