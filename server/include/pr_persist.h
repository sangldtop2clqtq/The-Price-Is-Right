#ifndef PR_PERSIST_H
#define PR_PERSIST_H
#include "pr_game.h"
struct pr_app;
typedef struct pr_save_task pr_save_task;
uint64_t pr_wall_ms(void);
char *pr_match_json(const pr_match *);
int pr_persist_enqueue(struct pr_app *, int room);
void pr_persist_tick(struct pr_app *, double now);
void pr_persist_cleanup(struct pr_app *);
#endif
