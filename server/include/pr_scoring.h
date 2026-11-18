#ifndef PR_SCORING_H
#define PR_SCORING_H
#include <stdint.h>
#define PR_MAX_PRICE UINT64_C(1000000000000)
/* Returns 1 only for a valid, on-time submission; no-submission is handled separately. */
int pr_score(uint64_t guess, uint64_t actual, uint32_t response_ms,
             uint32_t duration_ms, uint16_t *score);
#endif
