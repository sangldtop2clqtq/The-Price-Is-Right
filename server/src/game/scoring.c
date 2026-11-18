#include "pr_scoring.h"
#include <math.h>
#include <stddef.h>
int pr_score(uint64_t guess, uint64_t actual, uint32_t response_ms,
             uint32_t duration_ms, uint16_t *score) {
    if (!score || !actual || actual > PR_MAX_PRICE || guess > PR_MAX_PRICE ||
        !duration_ms || response_ms >= duration_ms) return 0;
    uint64_t difference = guess >= actual ? guess - actual : actual - guess;
    if (difference >= actual) { *score = 0; return 1; }
    long double accuracy = (long double)(actual - difference) / (long double)actual;
    long double multiplier = 1.0L - (long double)response_ms / (10.0L * duration_ms);
    *score = (uint16_t)floorl(1000.0L * accuracy * multiplier + 0.5L);
    return 1;
}
