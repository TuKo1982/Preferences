/* prefsfile - see prefsfile.h */
#include <stdlib.h>
#include <string.h>
#include "prefsfile.h"

#define ID_FORM PF_ID('F','O','R','M')
#define ID_PREF PF_ID('P','R','E','F')
#define ID_PRHD PF_ID('P','R','H','D')

s32 pf_get(const u8 *p, int size, int isSigned)
{
    u32 v = 0;
    int i;
    for (i = 0; i < size; i++)
        v = (v << 8) | p[i];
    if (isSigned) {
        if (size == 1 && (v & 0x80)) v |= 0xFFFFFF00UL;
        if (size == 2 && (v & 0x8000)) v |= 0xFFFF0000UL;
    }
    return (s32)v;
}

void pf_put(u8 *p, int size, s32 v)
{
    int i;
    for (i = size - 1; i >= 0; i--) {
        p[i] = (u8)(v & 0xFF);
        v = (s32)((u32)v >> 8);
    }
}

void pf_init(PFile *pf)
{
    pf->data = NULL;
    pf->len = pf->cap = 0;
}

void pf_free(PFile *pf)
{
    free(pf->data);
    pf_init(pf);
}

int pf_valid(const PFile *pf)
{
    u32 n;
    if (!pf->data || pf->len < 12) return 0;
    if ((u32)pf_get(pf->data, 4, 0) != ID_FORM) return 0;
    if ((u32)pf_get(pf->data + 8, 4, 0) != ID_PREF) return 0;
    n = (u32)pf_get(pf->data + 4, 4, 0);
    return n >= 4 && n + 8 <= pf->len;
}

int pf_take(PFile *pf, u8 *buf, u32 len)
{
    pf_free(pf);
    pf->data = buf;
    pf->len = pf->cap = len;
    if (!pf_valid(pf)) {
        pf_free(pf);
        return 0;
    }
    /* ignore trailing bytes beyond the FORM */
    pf->len = (u32)pf_get(pf->data + 4, 4, 0) + 8;
    return 1;
}

static int grow(PFile *pf, u32 need)
{
    u8 *n;
    u32 cap;
    if (need <= pf->cap) return 1;
    cap = need + 64;
    n = realloc(pf->data, cap);
    if (!n) return 0;
    pf->data = n;
    pf->cap = cap;
    return 1;
}

int pf_new(PFile *pf)
{
    pf_free(pf);
    if (!grow(pf, 26)) return 0;
    memset(pf->data, 0, 26);
    pf_put(pf->data, 4, (s32)ID_FORM);
    pf_put(pf->data + 4, 4, 18);
    pf_put(pf->data + 8, 4, (s32)ID_PREF);
    pf_put(pf->data + 12, 4, (s32)ID_PRHD);
    pf_put(pf->data + 16, 4, 6);
    pf->len = 26;
    return 1;
}

int pf_match(const u8 *data, u32 size, const PChunkSel *sel)
{
    if (!sel->selSize) return 1;
    if ((u32)sel->selOff + sel->selSize > size) return 0;
    return pf_get(data + sel->selOff, sel->selSize, 0) == sel->selVal;
}

u8 *pf_find(PFile *pf, const PChunkSel *sel, u32 *size)
{
    u32 p = 12, end;
    if (!pf_valid(pf)) return NULL;
    end = pf->len;
    while (p + 8 <= end) {
        u32 id = (u32)pf_get(pf->data + p, 4, 0);
        u32 sz = (u32)pf_get(pf->data + p + 4, 4, 0);
        if (p + 8 + sz > end) break;            /* truncated chunk: stop */
        if (id == sel->id && pf_match(pf->data + p + 8, sz, sel)) {
            if (size) *size = sz;
            return pf->data + p + 8;
        }
        p += 8 + sz + (sz & 1);
    }
    return NULL;
}

u8 *pf_need(PFile *pf, const PChunkSel *sel, u32 *size)
{
    u8 *d = pf_find(pf, sel, size);
    u32 sz, add, at;
    if (d) return d;
    if (!pf_valid(pf) && !pf_new(pf)) return NULL;
    sz = sel->size;
    add = 8 + sz + (sz & 1);
    if (!grow(pf, pf->len + add)) return NULL;
    at = pf->len;
    memset(pf->data + at, 0, add);
    pf_put(pf->data + at, 4, (s32)sel->id);
    pf_put(pf->data + at + 4, 4, (s32)sz);
    if (sel->selSize && (u32)sel->selOff + sel->selSize <= sz)
        pf_put(pf->data + at + 8 + sel->selOff, sel->selSize, sel->selVal);
    pf->len += add;
    pf_put(pf->data + 4, 4, (s32)(pf->len - 8));
    if (size) *size = sz;
    return pf->data + at + 8;
}

/* the chunk's data resized to size bytes (new bytes zero), the rest of the
 * file moved; NULL only on out of memory */
u8 *pf_resize(PFile *pf, const PChunkSel *sel, u32 size)
{
    u32 old, p, oldpad, newpad;
    u8 *d = pf_need(pf, sel, &old);
    if (!d) return NULL;
    if (old == size) return d;
    p = (u32)(d - pf->data) - 8;
    oldpad = old + (old & 1);
    newpad = size + (size & 1);
    if (newpad > oldpad) {
        if (!grow(pf, pf->len + newpad - oldpad)) return NULL;
        d = pf->data + p + 8;
    }
    memmove(d + newpad, d + oldpad, pf->len - (p + 8 + oldpad));
    if (size > old) memset(d + old, 0, newpad - old);
    else if (size & 1) d[size] = 0;                     /* the pad byte */
    pf->len = pf->len - oldpad + newpad;
    pf_put(pf->data + p + 4, 4, (s32)size);
    pf_put(pf->data + 4, 4, (s32)(pf->len - 8));
    return d;
}

/* the chunk at byte p of the file (its header), the next one after it */
static u32 next_chunk(const PFile *pf, u32 p)
{
    u32 sz = (u32)pf_get(pf->data + p + 4, 4, 0);
    return p + 8 + sz + (sz & 1);
}

static int append_raw(PFile *pf, u32 id, const u8 *data, u32 sz)
{
    u32 add = 8 + sz + (sz & 1), at;
    if (!grow(pf, pf->len + add)) return 0;
    at = pf->len;
    memset(pf->data + at, 0, add);
    pf_put(pf->data + at, 4, (s32)id);
    pf_put(pf->data + at + 4, 4, (s32)sz);
    if (sz) memcpy(pf->data + at + 8, data, sz);
    pf->len += add;
    pf_put(pf->data + 4, 4, (s32)(pf->len - 8));
    return 1;
}

/* every chunk with this ID out of the file */
void pf_remove_id(PFile *pf, u32 id)
{
    u32 p = 12;
    if (!pf_valid(pf)) return;
    while (p + 8 <= pf->len) {
        u32 n = next_chunk(pf, p);
        if (n > pf->len) break;
        if ((u32)pf_get(pf->data + p, 4, 0) == id) {
            memmove(pf->data + p, pf->data + n, pf->len - n);
            pf->len -= n - p;
            pf_put(pf->data + 4, 4, (s32)(pf->len - 8));
            continue;
        }
        p = n;
    }
}

/* every chunk with this ID of src added at the end of pf, in order */
int pf_copy_id(PFile *pf, const PFile *src, u32 id)
{
    u32 p = 12;
    if (!pf_valid(src)) return 1;
    while (p + 8 <= src->len) {
        u32 n = next_chunk(src, p), sz = (u32)pf_get(src->data + p + 4, 4, 0);
        if (n > src->len + 1 || p + 8 + sz > src->len) break;
        if ((u32)pf_get(src->data + p, 4, 0) == id && !append_raw(pf, id, src->data + p + 8, sz)) return 0;
        p = n;
    }
    return 1;
}

int pf_has_id(const PFile *pf, u32 id)
{
    u32 p = 12;
    if (!pf_valid(pf)) return 0;
    while (p + 8 <= pf->len) {
        if ((u32)pf_get(pf->data + p, 4, 0) == id) return 1;
        p = next_chunk(pf, p);
    }
    return 0;
}

/* the file as the editors write it: FORM PREF, a PRHD of zeros, then each of
 * sels[] (first match, its own size) and every chunk of keep[] in that order */
int pf_rebuild(PFile *pf, const PChunkSel *sels, int nsel, const u32 *keep)
{
    PFile n;
    int i;
    pf_init(&n);
    if (!pf_new(&n)) return 0;
    for (i = 0; i < nsel; i++) {
        u32 sz;
        u8 *d = pf_find(pf, &sels[i], &sz);
        if (d && !append_raw(&n, sels[i].id, d, sz)) { pf_free(&n); return 0; }
    }
    for (i = 0; keep && keep[i]; i++)
        if (!pf_copy_id(&n, pf, keep[i])) { pf_free(&n); return 0; }
    pf_free(pf);
    *pf = n;
    return 1;
}

/* the PRHD of a file: 0 none, 1 version 0, -1 another version */
int pf_prhd(const PFile *pf)
{
    PChunkSel s = { PF_ID('P','R','H','D'), 6, 0, 0, 0 };
    u32 sz;
    u8 *d = pf_find((PFile *)pf, &s, &sz);
    if (!d) return 0;
    return sz >= 1 && d[0] == 0 ? 1 : -1;
}

u8 *pf_next(const PFile *pf, u32 *pos, u32 *id, u32 *size)
{
    u32 p = *pos ? *pos : 12, sz;
    if (!pf_valid(pf) || p + 8 > pf->len) return NULL;
    sz = (u32)pf_get(pf->data + p + 4, 4, 0);
    if (p + 8 + sz > pf->len) return NULL;     /* truncated chunk: the end */
    *id = (u32)pf_get(pf->data + p, 4, 0);
    *size = sz;
    *pos = p + 8 + sz + (sz & 1);
    return pf->data + p + 8;
}
