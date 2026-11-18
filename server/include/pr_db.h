#ifndef PR_DB_H
#define PR_DB_H
#include <libpq-fe.h>
#include <sys/select.h>

typedef struct pr_db pr_db;
typedef void (*pr_db_callback)(void *, PGresult *);
/* Callbacks run on the event-loop thread. NULL result means unavailable/timeout. */
pr_db *pr_db_create(const char *);
void pr_db_destroy(pr_db *);
int pr_db_submit(pr_db *, const char *, int, const char *const *, pr_db_callback, void *);
void pr_db_tick(pr_db *, double);
void pr_db_watch(pr_db *, fd_set *, fd_set *, int *);
void pr_db_ready(pr_db *, const fd_set *, const fd_set *, double);
#endif
