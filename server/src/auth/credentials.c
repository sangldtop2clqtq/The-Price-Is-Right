#include "pr_credentials.h"
#include "pr_protocol.h"
#include <string.h>

int pr_valid_utf8(const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n;) {
        uint32_t code = s[i++], minimum = 0;
        unsigned more = 0;
        if (code == 0) return 0;
        if (code < 0x80) continue;
        if (code >= 0xc2 && code <= 0xdf) { code &= 0x1f; more = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 0x0f; more = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 7; more = 3; minimum = 0x10000; }
        else return 0;
        if (more > n - i) return 0;
        while (more--) {
            if ((s[i] & 0xc0) != 0x80) return 0;
            code = (code << 6) | (s[i++] & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}
int pr_decode_credentials(const uint8_t *payload, size_t n, char username[33],
                           char password[129], size_t *password_length) {
    pr_reader r;
    pr_reader_init(&r, payload, n);
    size_t un = 0, pn = 0;
    const uint8_t *u = pr_get_string(&r, &un, 32);
    const uint8_t *p = pr_get_string(&r, &pn, 128);
    if (!pr_reader_done(&r) || !u || !p || un < 3 || pn < 8 || !pr_valid_utf8(p, pn)) return 0;
    for (size_t i = 0; i < un; ++i)
        if (!((u[i] >= 'a' && u[i] <= 'z') || (u[i] >= 'A' && u[i] <= 'Z') ||
              (u[i] >= '0' && u[i] <= '9') || u[i] == '_')) return 0;
    memcpy(username, u, un); username[un] = 0;
    memcpy(password, p, pn); password[pn] = 0;
    *password_length = pn;
    return 1;
}
