#ifndef PR_CREDENTIALS_H
#define PR_CREDENTIALS_H
#include <stddef.h>
#include <stdint.h>
int pr_valid_utf8(const uint8_t *, size_t);
int pr_decode_credentials(const uint8_t *, size_t, char [33], char [129], size_t *);
#endif
