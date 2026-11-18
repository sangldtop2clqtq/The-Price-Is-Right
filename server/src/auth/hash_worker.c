#include "pr_hash_worker.h"
#include "pr_io.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

struct pr_hash_worker {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int pipes[2], stopping;
    size_t count;
    pr_hash_job *pending_head, *pending_tail, *done_head, *done_tail;
};
static void append(pr_hash_job **head, pr_hash_job **tail, pr_hash_job *j) {
    j->next = NULL;
    if (*tail) (*tail)->next = j; else *head = j;
    *tail = j;
}
static void *run_worker(void *context) {
    pr_hash_worker *w = context;
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        while (!w->pending_head && !w->stopping) pthread_cond_wait(&w->condition, &w->mutex);
        if (w->stopping) { pthread_mutex_unlock(&w->mutex); break; }
        pr_hash_job *j = w->pending_head;
        w->pending_head = j->next;
        if (!w->pending_head) w->pending_tail = NULL;
        pthread_mutex_unlock(&w->mutex);
        if (j->verify)
            j->result = crypto_pwhash_str_verify(j->hash, j->password, j->password_length);
        else
            j->result = crypto_pwhash_str_alg(j->hash, j->password, j->password_length,
                crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                crypto_pwhash_ALG_ARGON2ID13);
        sodium_memzero(j->password, sizeof(j->password));
        pthread_mutex_lock(&w->mutex);
        append(&w->done_head, &w->done_tail, j);
        pthread_mutex_unlock(&w->mutex);
        char byte = 1;
        ssize_t result;
        do { result = write(w->pipes[1], &byte, 1); } while (result < 0 && errno == EINTR);
        /* A full pipe already wakes the event loop; completed jobs stay in the list. */
    }
    return NULL;
}
pr_hash_worker *pr_hash_worker_create(void) {
    pr_hash_worker *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    if (pipe(w->pipes) < 0) { free(w); return NULL; }
    if (pr_set_nonblocking(w->pipes[0]) < 0 || pr_set_nonblocking(w->pipes[1]) < 0) goto fail_pipe;
    if (pthread_mutex_init(&w->mutex, NULL) != 0) goto fail_pipe;
    if (pthread_cond_init(&w->condition, NULL) != 0) goto fail_mutex;
    if (pthread_create(&w->thread, NULL, run_worker, w) != 0) goto fail_condition;
    return w;
fail_condition:
    pthread_cond_destroy(&w->condition);
fail_mutex:
    pthread_mutex_destroy(&w->mutex);
fail_pipe:
    close(w->pipes[0]); close(w->pipes[1]); free(w); return NULL;
}
int pr_hash_worker_fd(pr_hash_worker *w) { return w->pipes[0]; }
int pr_hash_worker_submit(pr_hash_worker *w, pr_hash_job *j) {
    pthread_mutex_lock(&w->mutex);
    if (w->count >= 64 || w->stopping) { pthread_mutex_unlock(&w->mutex); return -1; }
    ++w->count;
    append(&w->pending_head, &w->pending_tail, j);
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return 0;
}
void pr_hash_worker_dispatch(pr_hash_worker *w) {
    char bytes[128];
    for (;;) {
        ssize_t n = read(w->pipes[0], bytes, sizeof(bytes));
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    pthread_mutex_lock(&w->mutex);
    pr_hash_job *list = w->done_head;
    w->done_head = w->done_tail = NULL;
    for (pr_hash_job *j = list; j; j = j->next) --w->count;
    pthread_mutex_unlock(&w->mutex);
    while (list) {
        pr_hash_job *next = list->next;
        list->done(list);
        list = next;
    }
}
void pr_hash_worker_destroy(pr_hash_worker *w) {
    if (!w) return;
    pthread_mutex_lock(&w->mutex);
    w->stopping = 1;
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    pthread_join(w->thread, NULL);
    pr_hash_worker_dispatch(w);
    while (w->pending_head) {
        pr_hash_job *j = w->pending_head;
        w->pending_head = j->next;
        j->result = -1;
        sodium_memzero(j->password, sizeof(j->password));
        j->done(j);
    }
    pthread_cond_destroy(&w->condition);
    pthread_mutex_destroy(&w->mutex);
    close(w->pipes[0]); close(w->pipes[1]); free(w);
}
