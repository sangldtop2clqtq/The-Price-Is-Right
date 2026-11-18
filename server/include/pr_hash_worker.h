#ifndef PR_HASH_WORKER_H
#define PR_HASH_WORKER_H
#include <stddef.h>
#include <sodium.h>
typedef struct pr_hash_worker pr_hash_worker;
typedef struct pr_hash_job {
    int verify, result;
    char password[129], hash[crypto_pwhash_STRBYTES];
    size_t password_length;
    void (*done)(struct pr_hash_job *);
    void *context;
    struct pr_hash_job *next;
} pr_hash_job;
pr_hash_worker *pr_hash_worker_create(void);
int pr_hash_worker_fd(pr_hash_worker *);
int pr_hash_worker_submit(pr_hash_worker *, pr_hash_job *);
void pr_hash_worker_dispatch(pr_hash_worker *);
void pr_hash_worker_destroy(pr_hash_worker *);
#endif
