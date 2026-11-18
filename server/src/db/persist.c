#include "pr_server.h"
#include <stdlib.h>

#define SAVE_LIMIT 64
#define SAVE_ATTEMPTS 3
struct pr_save_task {
    pr_app *app;
    int room, attempts, in_flight, done;
    uint64_t id;
    double retry_at;
    char *document;
    struct pr_save_task *next;
};
static void failure(pr_save_task *task) {
    task->in_flight = 0;
    if (task->app->shutting_down) { task->done = 1; return; }
    pr_log_match("save_retry", NULL, task->id, 0, task->attempts);
    if (task->attempts >= SAVE_ATTEMPTS) {
        task->done = 1;
        pr_game_persisted(task->app, task->room, task->id, 0);
    } else task->retry_at = pr_server_now() + 2.0;
}
static void saved(void *context, PGresult *result) {
    pr_save_task *task = context;
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) != 1) {
        failure(task); return;
    }
    char *end;
    unsigned long long id = strtoull(PQgetvalue(result, 0, 0), &end, 10);
    if (*end || id != task->id) { failure(task); return; }
    task->done = 1; task->in_flight = 0;
    if (!task->app->shutting_down) {
        pr_log_match("save_committed", NULL, task->id, 0, 0);
        pr_game_persisted(task->app, task->room, task->id, 1);
    }
}
int pr_persist_enqueue(pr_app *app, int room) {
    pr_match *m = app->matches[room];
    if (!m || m->save_pending || m->persisted || m->save_failed) return 0;
    m->wall_ended_ms = pr_wall_ms();
    if (m->wall_ended_ms < m->wall_started_ms) m->wall_ended_ms = m->wall_started_ms;
    pr_save_task *task = app->save_count < SAVE_LIMIT ? calloc(1, sizeof(*task)) : NULL;
    if (!task) return -1;
    task->document = pr_match_json(m);
    if (!task->document) { free(task); return -1; }
    task->app = app; task->room = room; task->id = m->id;
    task->next = app->saves; app->saves = task; ++app->save_count;
    m->save_pending = 1;
    return 0;
}
void pr_persist_tick(pr_app *app, double now) {
    pr_save_task **link = &app->saves;
    while (*link) {
        pr_save_task *task = *link;
        if (task->done) {
            *link = task->next;
            free(task->document); free(task); --app->save_count;
            continue;
        }
        if (!task->in_flight && now >= task->retry_at) {
            ++task->attempts;
            const char *params[] = {task->document};
            if (pr_db_submit(app->db, "SELECT public.pr_save_match($1::jsonb)", 1, params, saved, task) < 0)
                failure(task);
            else task->in_flight = 1;
        }
        link = &task->next;
    }
}
void pr_persist_cleanup(pr_app *app) {
    /* DB has already been destroyed, so no callback can retain these tasks. */
    while (app->saves) {
        pr_save_task *task = app->saves;
        app->saves = task->next; free(task->document); free(task);
    }
    app->save_count = 0;
}
