#ifndef PR_LOG_H
#define PR_LOG_H
#include <stdint.h>
/* Single event-loop owner. Fields are numbers or trusted static identifiers only. */
int pr_log_open(void);
void pr_log_flush(void);
void pr_log_close(void);
double pr_server_now(void);
void pr_log_event(const char *event, uint64_t connection, uint64_t user,
                  const char *room, uint32_t request, uint16_t code, int status,
                  double elapsed_seconds, const char *reason);
void pr_log_match(const char *event, const char *room, uint64_t match_id, unsigned round_no, int code);
#endif
