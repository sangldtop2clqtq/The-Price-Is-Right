#include "pr_log.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG_CAPACITY 128
#define LOG_LINE 512
static struct {
    int fd, original_flags, shared_stderr, enabled, debug;
    unsigned head, count;
    uint64_t dropped;
    struct { size_t length, sent; char bytes[LOG_LINE]; } lines[LOG_CAPACITY];
} logger = {.fd = -1};

double pr_server_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
int pr_log_open(void) {
    const char *level = getenv("PR_LOG_LEVEL"), *path = getenv("PR_LOG_FILE");
    logger.enabled = !level || strcmp(level, "off");
    logger.debug = level && !strcmp(level, "debug");
    if (!logger.enabled) return 0;
    logger.fd = path && *path ? open(path, O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK, 0600) : dup(STDERR_FILENO);
    if (logger.fd < 0) return -1;
    logger.original_flags = fcntl(logger.fd, F_GETFL, 0);
    logger.shared_stderr = !path || !*path;
    if (logger.original_flags < 0 || fcntl(logger.fd, F_SETFL, logger.original_flags | O_NONBLOCK) < 0) {
        close(logger.fd); logger.fd = -1; return -1;
    }
    return 0;
}
void pr_log_flush(void) {
    if (logger.fd < 0) return;
    for (unsigned budget = 0; budget < 32 && logger.count; ++budget) {
        unsigned slot = logger.head;
        ssize_t n = write(logger.fd, logger.lines[slot].bytes + logger.lines[slot].sent,
                          logger.lines[slot].length - logger.lines[slot].sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        logger.lines[slot].sent += (size_t)n;
        if (logger.lines[slot].sent == logger.lines[slot].length) {
            logger.head = (logger.head + 1) % LOG_CAPACITY;
            --logger.count;
        }
    }
}
/* Never accept usernames, payloads, connection strings or arbitrary error text here. */
static const char *identifier(const char *text) {
    if (!text || !*text) return "-";
    size_t n = 0;
    for (; text[n]; ++n) {
        unsigned char c = (unsigned char)text[n];
        if (n >= 48 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return "invalid_identifier";
    }
    return text;
}
static void log_context(const char *event, uint64_t connection, uint64_t user, const char *room,
                  uint32_t request, uint16_t code, int status, double elapsed, const char *reason, uint64_t match_id, unsigned round_no) {
    if (!logger.enabled || logger.fd < 0) return;
    if (!logger.debug && !strcmp(event, "response") && code == 0x00f0 && status == 0) return;
    pr_log_flush();
    if (logger.count == LOG_CAPACITY) { ++logger.dropped; return; }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t milliseconds = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    uint64_t microseconds = elapsed > 0 ? (uint64_t)(elapsed * 1000000.0) : 0;
    unsigned slot = (logger.head + logger.count) % LOG_CAPACITY;
    int n = snprintf(logger.lines[slot].bytes, LOG_LINE,
        "{\"time_ms\":%llu,\"event\":\"%s\",\"connection_id\":%llu,\"user_id\":%llu,"
        "\"room_code\":\"%s\",\"request_id\":%u,\"code\":%u,\"status\":%d,"
        "\"elapsed_us\":%llu,\"reason\":\"%s\",\"dropped_logs\":%llu,\"match_id\":%llu,\"round_no\":%u}\n",
        (unsigned long long)milliseconds, identifier(event), (unsigned long long)connection,
        (unsigned long long)user, identifier(room), request, code, status,
        (unsigned long long)microseconds, identifier(reason), (unsigned long long)logger.dropped,
        (unsigned long long)match_id, round_no);
    if (n < 0 || n >= LOG_LINE) { ++logger.dropped; return; }
    logger.lines[slot].length = (size_t)n; logger.lines[slot].sent = 0;
    ++logger.count;
    pr_log_flush();
}
void pr_log_event(const char *event, uint64_t connection, uint64_t user, const char *room,
                  uint32_t request, uint16_t code, int status, double elapsed, const char *reason) {
    log_context(event, connection, user, room, request, code, status, elapsed, reason, 0, 0);
}
void pr_log_match(const char *event, const char *room, uint64_t match_id, unsigned round_no, int code) {
    log_context(event, 0, 0, room, 0, (uint16_t)code, 0, 0, "game_transition", match_id, round_no);
}
void pr_log_close(void) {
    pr_log_flush();
    if (logger.fd >= 0) {
        if (logger.shared_stderr) fcntl(logger.fd, F_SETFL, logger.original_flags);
        close(logger.fd);
    }
    logger.fd = -1;
}
