#include "pr_server.h"
#include "pr_credentials.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pr_app *app;
    size_t slot;
    uint64_t generation, user_id;
    pr_header header;
    char username[33], name[97];
    pr_hash_job hash_job;
} auth_task;

static pr_client *task_client(auth_task *t) {
    pr_client *c = &t->app->clients[t->slot];
    return c->fd >= 0 && c->generation == t->generation ? c : NULL;
}
static void release_task(auth_task *t) { sodium_memzero(t, sizeof(*t)); free(t); }
static uint64_t parse_id(const char *text) {
    char *end;
    errno = 0;
    unsigned long long id = strtoull(text, &end, 10);
    return errno || *end || !id || id > INT64_MAX ? 0 : (uint64_t)id;
}
static void complete(auth_task *t, uint16_t status) {
    pr_client *c = task_client(t);
    if (c) {
        uint8_t body[128];
        pr_writer w;
        pr_writer_init(&w, body, sizeof(body));
        if (status == PR_OK) {
            if (t->header.code == PR_LOGIN) {
                for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
                    if (t->app->clients[i].fd >= 0 && t->app->clients[i].user_id == t->user_id) {
                        status = PR_ALREADY_ONLINE; break;
                    }
                }
                if (status == PR_OK) {
                    c->user_id = t->user_id;
                    strcpy(c->display_name, t->name);
                }
            }
            if (status == PR_OK) {
                pr_put_u64(&w, t->user_id);
                if (t->header.code == PR_LOGIN) pr_put_string(&w, t->name, strlen(t->name));
            }
        }
        c->auth_busy = 0;
        if (pr_reply(c, &t->header, status, body, status == PR_OK ? w.used : 0) < 0)
            pr_close_client(c);
    }
    release_task(t);
}
static void inserted(void *context, PGresult *result) {
    auth_task *t = context;
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) { complete(t, PR_INTERNAL_ERROR); return; }
    if (PQntuples(result) == 0) { complete(t, PR_USER_EXISTS); return; }
    t->user_id = parse_id(PQgetvalue(result, 0, 0));
    complete(t, t->user_id ? PR_OK : PR_INTERNAL_ERROR);
}
static void hashed(pr_hash_job *job) {
    auth_task *t = job->context;
    if (!task_client(t)) { release_task(t); return; }
    if (job->verify) {
        complete(t, job->result == 0 && t->user_id ? PR_OK : PR_AUTH_FAILED);
    } else if (job->result != 0) complete(t, PR_INTERNAL_ERROR);
    else {
        const char *params[] = {t->username, job->hash, t->username};
        if (pr_db_submit(t->app->db,
            "INSERT INTO users(username,password_hash,display_name) VALUES($1,$2,$3) "
            "ON CONFLICT(username) DO NOTHING RETURNING id", 3, params, inserted, t) < 0)
            complete(t, PR_INTERNAL_ERROR);
    }
}
static void fetched(void *context, PGresult *result) {
    auth_task *t = context;
    if (!task_client(t)) { release_task(t); return; }
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) { complete(t, PR_INTERNAL_ERROR); return; }
    if (PQntuples(result) != 1) { complete(t, PR_AUTH_FAILED); return; }
    const char *hash = PQgetvalue(result, 0, 1), *name = PQgetvalue(result, 0, 2);
    t->user_id = parse_id(PQgetvalue(result, 0, 0));
    size_t hash_n = strlen(hash), name_n = strlen(name);
    if (!t->user_id || hash_n >= sizeof(t->hash_job.hash) || !name_n || name_n >= sizeof(t->name) ||
        !pr_valid_utf8((const uint8_t *)name, name_n)) { complete(t, PR_INTERNAL_ERROR); return; }
    memcpy(t->hash_job.hash, hash, hash_n + 1);
    memcpy(t->name, name, name_n + 1);
    t->hash_job.verify = 1;
    if (pr_hash_worker_submit(t->app->hash_worker, &t->hash_job) < 0) complete(t, PR_INTERNAL_ERROR);
}
int pr_auth_handle(pr_app *app, size_t slot, const pr_header *h, const uint8_t *payload) {
    pr_client *c = &app->clients[slot];
    if (h->code == PR_LOGOUT) {
        if (h->payload_length) return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
        if (!c->user_id) return pr_reply(c, h, PR_NOT_AUTHENTICATED, NULL, 0);
        int result = pr_reply(c, h, PR_OK, NULL, 0);
        pr_room_detach(c);
        c->user_id = 0;
        sodium_memzero(c->display_name, sizeof(c->display_name));
        return result;
    }
    auth_task *t = calloc(1, sizeof(*t));
    if (!t) return pr_reply(c, h, PR_INTERNAL_ERROR, NULL, 0);
    if (!pr_decode_credentials(payload, h->payload_length, t->username,
                                t->hash_job.password, &t->hash_job.password_length)) {
        release_task(t);
        return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
    }
    if (c->user_id) {
        release_task(t);
        return pr_reply(c, h, PR_WRONG_STATE, NULL, 0);
    }
    t->app = app; t->slot = slot; t->generation = c->generation; t->header = *h;
    t->hash_job.done = hashed; t->hash_job.context = t;
    c->auth_busy = 1;
    if (h->code == PR_REGISTER) {
        if (pr_hash_worker_submit(app->hash_worker, &t->hash_job) < 0) complete(t, PR_INTERNAL_ERROR);
    } else {
        const char *params[] = {t->username};
        if (pr_db_submit(app->db,
            "SELECT id,password_hash,display_name FROM users WHERE username=$1", 1, params, fetched, t) < 0)
            complete(t, PR_INTERNAL_ERROR);
    }
    return 0;
}
