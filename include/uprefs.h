/* uprefs - UniPrefs' own settings file (user 28.11.2026): ENV:UniPrefs.prefs and ENVARC:UniPrefs.prefs, an IFF
 * FORM PREF with a PRHD chunk and one chunk per setting, each a list of NUL-terminated strings:
 *
 *   REPO  the core repository, then the modules repository ("owner/repo"; "" = the default)   (Updates, About)
 *   CLSD  the categories closed in the list, "Name,Name"                                      (the program)
 *
 * Chunks this version does not know are kept when the file is written again. Read from ENV: (ENVARC: when ENV:
 * has none), written to both. The pure part (upf_find, upf_build) is tested on the PC (tests/test_core.c).
 * Before 28.11.2026 these settings were the variables UniPrefs/UpdateRepo, UniPrefs/ModulesRepo and
 * UniPrefs/ClosedGroups: read when the file has no chunk for them, deleted when the chunk is written. */
#ifndef UPREFS_H
#define UPREFS_H

#include <string.h>
#include <stdlib.h>
#include "io.h"

/* writing a file: io_save in the program and the tests; a module (no io_save in its kit) defines UPF_SAVE as its
 * own int f(const char *path, const void *buf, u32 len) before including this header */
#ifndef UPF_SAVE
#define UPF_SAVE io_save
#endif

#define UPF_NAME "UniPrefs.prefs"
#define UPF_ENV  "ENV:" UPF_NAME
#define UPF_ARC  "ENVARC:" UPF_NAME
#define UPF_MAX  4096UL                 /* largest file read */

static inline u32 upf_be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static inline void upf_put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

/* 1 = b is a FORM PREF whose chunks all fit */
static inline int upf_ok(const u8 *b, u32 len)
{
    u32 p, n, end;
    if (!b || len < 12 || memcmp(b, "FORM", 4) || memcmp(b + 8, "PREF", 4)) return 0;
    n = upf_be32(b + 4);
    if (n < 4 || n > len - 8) return 0;
    end = 8 + n;
    for (p = 12; p < end; p += 8 + n + (n & 1)) {
        if (p + 8 > end) return 0;
        n = upf_be32(b + p + 4);
        if (n > end - p - 8) return 0;
    }
    return 1;
}

/* the chunk id of a FORM PREF: 1 = found, its data and size */
static inline int upf_find(const u8 *b, u32 len, const char *id, const u8 **data, u32 *dlen)
{
    u32 p, n, end;
    if (!upf_ok(b, len)) return 0;
    end = 8 + upf_be32(b + 4);
    for (p = 12; p + 8 <= end; p += 8 + n + (n & 1)) {
        n = upf_be32(b + p + 4);
        if (!memcmp(b + p, id, 4)) { *data = b + p + 8; *dlen = n; return 1; }
    }
    return 0;
}

/* the idx-th NUL-terminated string of a chunk's data: 1 = found (copied, cut to len) */
static inline int upf_string(const u8 *d, u32 dlen, int idx, char *buf, int len)
{
    u32 p = 0;
    while (idx-- > 0) {
        while (p < dlen && d[p]) p++;
        if (p >= dlen) return 0;
        p++;
    }
    if (p >= dlen) return 0;
    {
        int k = 0;
        while (p < dlen && d[p] && k < len - 1) buf[k++] = (char)d[p++];
        buf[k] = 0;
    }
    return 1;
}

/* a new FORM PREF: PRHD, the chunks of old (when it is one) but id and PRHD, then id with data (none when
 * dlen is 0); out holds outmax bytes; the size written, 0 = too big */
static inline u32 upf_build(const u8 *old, u32 oldlen, const char *id, const u8 *data, u32 dlen, u8 *out, u32 outmax)
{
    u32 o = 12, p, n, end;
    if (outmax < 26) return 0;
    memcpy(out, "FORM\0\0\0\0PREF", 12);
    memcpy(out + o, "PRHD\0\0\0\6\0\0\0\0\0\0", 14);
    o += 14;
    if (upf_ok(old, oldlen)) {
        end = 8 + upf_be32(old + 4);
        for (p = 12; p + 8 <= end; p += 8 + n + (n & 1)) {
            n = upf_be32(old + p + 4);
            if (!memcmp(old + p, id, 4) || !memcmp(old + p, "PRHD", 4)) continue;
            if (o + 8 + n + (n & 1) > outmax) return 0;
            memcpy(out + o, old + p, 8 + n);
            o += 8 + n;
            if (n & 1) out[o++] = 0;
        }
    }
    if (dlen) {
        if (o + 8 + dlen + (dlen & 1) > outmax) return 0;
        memcpy(out + o, id, 4);
        upf_put32(out + o + 4, dlen);
        memcpy(out + o + 8, data, dlen);
        o += 8 + dlen;
        if (dlen & 1) out[o++] = 0;
    }
    upf_put32(out + 4, o - 8);
    return o;
}

/* the file as it is now: ENV:, else ENVARC:; NULL = none (free() the result) */
static inline u8 *upf_load(u32 *len)
{
    u8 *b = NULL;
    *len = 0;
    if (io_load(UPF_ENV, &b, len) && upf_ok(b, *len)) return b;
    free(b);
    b = NULL;
    if (io_load(UPF_ARC, &b, len) && upf_ok(b, *len)) return b;
    free(b);
    *len = 0;
    return NULL;
}

/* the idx-th string of chunk id of the file: 1 = the file has that chunk (buf "" when the string is missing) */
static inline int upf_get(const char *id, int idx, char *buf, int len)
{
    u32 l, dl;
    const u8 *d;
    u8 *b = upf_load(&l);
    int r = 0;
    buf[0] = 0;
    if (b && upf_find(b, l, id, &d, &dl)) { upf_string(d, dl, idx, buf, len); r = 1; }
    free(b);
    return r;
}

/* chunk id = the n strings s[] (n = 0: removed), written to ENV: and ENVARC:; 1 = both written */
static inline int upf_set(const char *id, const char *const *s, int n)
{
    u8 data[1024], out[UPF_MAX];
    u32 dl = 0, l, ol;
    int i, ok;
    u8 *b;
    for (i = 0; i < n; i++) {
        u32 k = (u32)strlen(s[i]) + 1;
        if (dl + k > sizeof(data)) return 0;
        memcpy(data + dl, s[i], k);
        dl += k;
    }
    b = upf_load(&l);
    ol = upf_build(b, l, id, data, dl, out, sizeof(out));
    free(b);
    if (!ol) return 0;
    ok = UPF_SAVE(UPF_ENV, out, ol);
    ok &= UPF_SAVE(UPF_ARC, out, ol);
    return ok;
}

#endif
