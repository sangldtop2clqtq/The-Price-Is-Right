#include "pr_credentials.h"
#include "pr_scoring.h"
#include "pr_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t credentials(uint8_t *out, const char *user, const char *password) {
    pr_writer w;
    pr_writer_init(&w, out, 256);
    pr_put_string(&w, user, strlen(user));
    pr_put_string(&w, password, strlen(password));
    assert(w.ok); return w.used;
}
static void test_validation(void) {
    uint8_t payload[256];
    char username[33], password[129];
    size_t pn;
    size_t n = credentials(payload, "User_123", "mật khẩu đủ dài");
    assert(pr_decode_credentials(payload, n, username, password, &pn));
    assert(!strcmp(username, "User_123") && pn == strlen("mật khẩu đủ dài"));
    assert(!pr_decode_credentials(payload, n - 1, username, password, &pn));
    payload[n] = 0;
    assert(!pr_decode_credentials(payload, n + 1, username, password, &pn));
    const char *bad_users[] = {"ab", "a b", "a'b", "việt", "abcdefghijklmnopqrstuvwxyz0123456"};
    for (size_t i = 0; i < sizeof(bad_users)/sizeof(bad_users[0]); ++i) {
        n = credentials(payload, bad_users[i], "password123");
        assert(!pr_decode_credentials(payload, n, username, password, &pn));
    }
    n = credentials(payload, "valid", "short");
    assert(!pr_decode_credentials(payload, n, username, password, &pn));
    n = credentials(payload, "valid", "password123");
    payload[n - 1] = 0;
    assert(!pr_decode_credentials(payload, n, username, password, &pn));
    const uint8_t invalid[][4] = {{0xc0,0x80,0,0},{0xed,0xa0,0x80,0},{0xf4,0x90,0x80,0x80},{0x80,0,0,0}};
    const size_t lengths[] = {2,3,4,1};
    for (size_t i = 0; i < 4; ++i) assert(!pr_valid_utf8(invalid[i], lengths[i]));
    assert(!pr_valid_utf8((const uint8_t *)"\xe2\x82", 2));
    assert(pr_valid_utf8((const uint8_t *)"\xf0\x9f\x98\x80", 4));
    char max_user[33], max_pass[129];
    memset(max_user, 'a', 32); max_user[32] = 0;
    memset(max_pass, 'b', 128); max_pass[128] = 0;
    n = credentials(payload, max_user, max_pass);
    assert(pr_decode_credentials(payload, n, username, password, &pn) && pn == 128);
}
static void test_scoring(void) {
    uint16_t score = 999;
    assert(pr_score(490000,500000,5000,20000,&score) && score == 956);
    assert(pr_score(490000,500000,10000,20000,&score) && score == 931);
    assert(pr_score(500000,500000,0,20000,&score) && score == 1000);
    assert(pr_score(0,500000,1,20000,&score) && score == 0);
    assert(pr_score(1000000,500000,1,20000,&score) && score == 0);
    assert(pr_score(510000,500000,5000,20000,&score) && score == 956);
    assert(pr_score(PR_MAX_PRICE,PR_MAX_PRICE,0,30000,&score) && score == 1000);
    assert(pr_score(1,1,19999,20000,&score) && score == 900);
    assert(!pr_score(1,0,1,20000,&score));
    assert(!pr_score(PR_MAX_PRICE + 1,1,1,20000,&score));
    assert(!pr_score(1,PR_MAX_PRICE + 1,1,20000,&score));
    assert(!pr_score(1,1,20000,20000,&score));
    assert(!pr_score(1,1,20001,20000,&score));
    assert(!pr_score(1,1,0,0,&score));
    assert(!pr_score(1,1,0,20000,NULL));
}
int main(void) {
    test_validation(); test_scoring();
    puts("PASS: credential bounds/UTF-8, slide scoring examples and deadline boundaries");
    return 0;
}
