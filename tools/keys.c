/* keys - get and set one key as text, for the CLI, ARexx and the GUI alike. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "module.h"

int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++, b++;
    }
    return *a == *b;
}

const Key *key_find(const Module *m, const char *name)
{
    int i;
    for (i = 0; i < m->nkeys; i++)
        if (str_ieq(m->keys[i].name, name)) return &m->keys[i];
    return NULL;
}

static int key_index(const ModState *st, const Key *k)
{
    return (int)(k - st->m->keys);
}

static int mask_shift(u32 mask)
{
    int s = 0;
    if (!mask) return 0;
    while (!(mask & 1)) mask >>= 1, s++;
    return s;
}

/* field bytes the key needs inside its chunk */
static u32 field_bytes(const Key *k)
{
    if (k->type == KT_MS) return 8;
    if (k->type == KT_STRLIST) return (u32)k->size * k->count;
    if (k->type == KT_CHAR) return 1;
    return k->size;
}

/* KT_DEC: raw field <-> shown value in units of 10^-decimals */
static long long pow10i(int n) { long long r = 1; while (n-- > 0) r *= 10; return r; }
static long long dec_scaled(const Key *k, s32 raw)
{
    long long num = (long long)raw * k->values[0] * pow10i(k->values[2]), den = k->values[1];
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}
static s32 dec_raw(const Key *k, long long scaled)
{
    long long num = scaled * k->values[1], den = (long long)k->values[0] * pow10i(k->values[2]);
    return (s32)(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}
static void dec_format(const Key *k, long long v, char *buf, int len)
{
    int d = k->values[2];
    long long p = pow10i(d), a = v < 0 ? -v : v;
    if (!d) snprintf(buf, len, "%s%ld", v < 0 ? "-" : "", (long)a);
    else snprintf(buf, len, "%s%ld.%0*ld", v < 0 ? "-" : "", (long)(a / p), d, (long)(a % p));
}
/* "21", "21.5", "-0.25" -> units of 10^-decimals; 0 = not a number */
static int dec_parse(const Key *k, const char *t, long long *out)
{
    int d = k->values[2], neg = 0, n = 0;
    long long ip = 0, fp = 0;
    while (*t == ' ') t++;
    if (*t == '-') neg = 1, t++;
    if (!(*t >= '0' && *t <= '9') && *t != '.' && *t != ',') return 0;
    while (*t >= '0' && *t <= '9') ip = ip * 10 + (*t++ - '0');
    if (*t == '.' || *t == ',') {
        t++;
        while (*t >= '0' && *t <= '9') { if (n < d) { fp = fp * 10 + (*t - '0'); n++; } t++; }
    }
    while (n < d) fp *= 10, n++;
    while (*t == ' ') t++;
    if (*t) return 0;
    *out = (ip * pow10i(d) + fp) * (neg ? -1 : 1);
    return 1;
}

/* KT_VSTR, KT_BYTES: the bytes and their count (variable: to the chunk end) */
static u8 *var_field(ModState *st, const Key *k, u32 *n)
{
    u32 sz;
    u8 *d;
    if (st->m->kind != MK_IFF) return NULL;
    d = pf_need(&st->pf, &st->m->chunks[k->chunk], &sz);
    if (!d || k->off > sz) return NULL;
    if (k->type == KT_BYTES && k->size) {
        if ((u32)k->off + k->size > sz) return NULL;
        *n = k->size;
    } else
        *n = sz - k->off;
    return d + k->off;
}

/* replace the variable part with n bytes of src */
static int var_put(ModState *st, const Key *k, const u8 *src, u32 n)
{
    u8 *d = pf_resize(&st->pf, &st->m->chunks[k->chunk], (u32)k->off + n);
    if (!d) return -1;
    memcpy(d + k->off, src, n);
    if (k->count == 2 && k->off >= 2) pf_put(d + k->off - 2, 2, (s32)n);
    return 0;
}

/* KT_RGB: the three components as 8-bit values */
static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

u8 *key_field(ModState *st, const Key *k, u32 need)
{
    u32 sz;
    u8 *d;
    if (st->m->kind != MK_IFF) return NULL;
    d = pf_need(&st->pf, &st->m->chunks[k->chunk], &sz);
    if (!d || (u32)k->off + need > sz) return NULL;
    return d + k->off;
}

int key_raw_get(ModState *st, const Key *k, s32 *v)
{
    if (st->m->kind == MK_ENV) {
        const char *t = st->env[key_index(st, k)];
        if (!t) t = k->def ? k->def : "0";
        *v = (s32)strtol(t, NULL, 0);
        return 0;
    } else {
        u8 *p = key_field(st, k, k->size);
        s32 r;
        if (!p) return -1;
        r = pf_get(p, k->size, (k->flags & KF_SIGNED) && !k->mask);
        if (k->mask) r = (s32)(((u32)r & k->mask) >> mask_shift(k->mask));
        *v = r;
        return 0;
    }
}

/* MK_ENV: the variable's new text; 1 = changed, 0 = same, -1 = no memory */
static int env_put(ModState *st, const Key *k, const char *text)
{
    int i = key_index(st, k);
    char *n;
    if (st->env[i] && !strcmp(st->env[i], text)) return 0;
    if (!(n = malloc(strlen(text) + 1))) return -1;
    strcpy(n, text);
    free(st->env[i]);
    st->env[i] = n;
    st->envDirty[i] = 1;
    return 1;
}

int key_raw_set(ModState *st, const Key *k, s32 v)
{
    if (st->m->kind == MK_ENV) {
        char tmp[16];
        sprintf(tmp, "%ld", (long)v);
        return env_put(st, k, tmp) < 0 ? -1 : 0;
    } else {
        u8 *p = key_field(st, k, k->size);
        if (!p) return -1;
        if (k->mask) {
            u32 old = (u32)pf_get(p, k->size, 0);
            u32 sh = (u32)mask_shift(k->mask);
            v = (s32)((old & ~k->mask) | (((u32)v << sh) & k->mask));
        }
        pf_put(p, k->size, v);
        return 0;
    }
}

int key_get(ModState *st, const Key *k, char *buf, int len)
{
    s32 v;
    int i;
    buf[0] = 0;
    switch (k->type) {
    case KT_STR: {
        u8 *p;
        if (st->m->kind == MK_ENV) {     /* the variable, else def */
            const char *t = st->env[key_index(st, k)];
            if (!t) t = k->def ? k->def : "";
            snprintf(buf, len, "%s", t);
            return 0;
        }
        if (!(p = key_field(st, k, k->size))) return -1;
        for (i = 0; i < k->size && p[i] && i < len - 1; i++) buf[i] = (char)p[i];
        buf[i] = 0;
        return 0;
    }
    case KT_STRLIST: {
        u8 *p = key_field(st, k, field_bytes(k));
        int n, o = 0, last = -1;
        if (!p) return -1;
        for (n = 0; n < k->count; n++)
            if (p[n * k->size]) last = n;
        for (n = 0; n <= last; n++) {
            const u8 *s = p + n * k->size;
            if (n && o < len - 1) buf[o++] = ',';
            for (i = 0; i < k->size && s[i] && o < len - 1; i++) buf[o++] = (char)s[i];
        }
        buf[o] = 0;
        return 0;
    }
    case KT_MS: {
        u8 *p = key_field(st, k, 8);
        u32 sec, mic;
        if (!p) return -1;
        sec = (u32)pf_get(p, 4, 0);
        mic = (u32)pf_get(p + 4, 4, 0);
        snprintf(buf, len, "%lu", (unsigned long)(sec * 1000 + mic / 1000));
        return 0;
    }
    case KT_CHAR: {
        u8 *p = key_field(st, k, 1);
        if (!p) return -1;
        buf[0] = (char)p[0];
        buf[1] = 0;
        return 0;
    }
    case KT_RGB: {
        u8 *p = key_field(st, k, k->size);
        int w = k->size == 6 ? 2 : 1;
        if (!p) return -1;
        snprintf(buf, len, "#%02X%02X%02X", p[0], p[w], p[2 * w]);
        return 0;
    }
    case KT_DEC:
        if (key_raw_get(st, k, &v)) return -1;
        dec_format(k, dec_scaled(k, v), buf, len);
        return 0;
    case KT_VSTR:
    case KT_BYTES: {
        u32 n;
        const u8 *p = var_field(st, k, &n);
        u32 j, o = 0;
        if (!p) return -1;
        for (j = 0; j < n && o + 3 < (u32)len; j++) {
            if (k->type == KT_VSTR && !p[j]) break;
            if (k->type == KT_VSTR) buf[o++] = (char)p[j];
            else o += snprintf(buf + o, len - o, "%02X", p[j]);
        }
        buf[o] = 0;
        return 0;
    }
    default:
        break;
    }
    if (key_raw_get(st, k, &v)) return -1;
    switch (k->type) {
    case KT_BOOL:
        snprintf(buf, len, "%s", v ? "Yes" : "No");
        break;
    case KT_ENUM:
        for (i = 0; k->names[i]; i++)
            if ((k->values ? k->values[i] : i) == v) {
                snprintf(buf, len, "%s", k->names[i]);
                return 0;
            }
        snprintf(buf, len, "%ld", (long)v);   /* value outside the list: show the number */
        break;
    case KT_HEX:
        snprintf(buf, len, "0x%08lX", (unsigned long)(u32)v);
        break;
    default:
        if (k->flags & KF_SIGNED) snprintf(buf, len, "%ld", (long)v);
        else snprintf(buf, len, "%lu", (unsigned long)(u32)v);
        break;
    }
    return 0;
}

static int parse_int(const char *t, s32 *v)
{
    char *e;
    long l;
    unsigned long u;
    while (*t == ' ') t++;
    if (!*t) return 0;
    if (*t == '$') {                    /* Amiga hex */
        u = strtoul(t + 1, &e, 16);
        l = (long)u;
    } else if (*t == '-') {
        l = strtol(t, &e, 0);
    } else {
        u = strtoul(t, &e, 0);
        l = (long)u;
    }
    while (*e == ' ') e++;
    if (*e) return 0;
    *v = (s32)l;
    return 1;
}

static int parse_bool(const char *t, s32 *v)
{
    static const char *const yes[] = { "yes", "on", "true", "1", NULL };
    static const char *const no[] = { "no", "off", "false", "0", NULL };
    int i;
    for (i = 0; yes[i]; i++) if (str_ieq(t, yes[i])) { *v = 1; return 1; }
    for (i = 0; no[i]; i++) if (str_ieq(t, no[i])) { *v = 0; return 1; }
    return 0;
}

static int range_ok(const Key *k, s32 v)
{
    if (k->min > k->max) return 1;
    if (k->flags & KF_SIGNED) return v >= k->min && v <= k->max;
    return (u32)v >= (u32)k->min && (u32)v <= (u32)k->max;
}

int key_set(ModState *st, const Key *k, const char *text, char *err, int errlen)
{
    s32 v;
    int i, ch = 0;
    err[0] = 0;
    switch (k->type) {
    case KT_STR: {
        u8 *p;
        size_t n = strlen(text);
        if (n >= k->size) {
            snprintf(err, errlen, "%s: at most %d characters", k->name, k->size - 1);
            return 10;
        }
        if (st->m->kind == MK_ENV) {
            int r;
            /* an unset variable equal to its default stays unset */
            if (!st->env[key_index(st, k)] && !strcmp(text, k->def ? k->def : "")) break;
            r = env_put(st, k, text);
            if (r < 0) { snprintf(err, errlen, "out of memory"); return 20; }
            ch = r;
            break;
        }
        if (!(p = key_field(st, k, k->size))) goto nofield;
        if (memcmp(p, text, n + 1)) {
            memset(p, 0, k->size);
            memcpy(p, text, n);
            ch = 1;
        }
        break;
    }
    case KT_STRLIST: {
        u8 *p = key_field(st, k, field_bytes(k));
        u8 *tmp;
        const char *s = text;
        int n = 0;
        if (!p) goto nofield;
        tmp = calloc(1, field_bytes(k));
        if (!tmp) { snprintf(err, errlen, "out of memory"); return 20; }
        while (*s) {
            const char *e = strchr(s, ',');
            size_t l = e ? (size_t)(e - s) : strlen(s);
            while (l && *s == ' ') s++, l--;
            while (l && s[l - 1] == ' ') l--;
            if (n >= k->count || l >= k->size) {
                free(tmp);
                snprintf(err, errlen, "%s: at most %d items of %d characters", k->name, k->count, k->size - 1);
                return 10;
            }
            memcpy(tmp + n * k->size, s, l);
            n++;
            if (!e) break;
            s = e + 1;
        }
        if (memcmp(p, tmp, field_bytes(k))) {
            memcpy(p, tmp, field_bytes(k));
            ch = 1;
        }
        free(tmp);
        break;
    }
    case KT_MS: {
        u8 *p = key_field(st, k, 8);
        u32 sec, mic;
        if (!p) goto nofield;
        if (!parse_int(text, &v) || v < 0 || !range_ok(k, v)) goto badrange;
        sec = (u32)v / 1000;
        mic = ((u32)v % 1000) * 1000;
        /* compare in ms: an unchanged value keeps its original micros */
        if ((u32)pf_get(p, 4, 0) * 1000 + (u32)pf_get(p + 4, 4, 0) / 1000 != (u32)v) {
            pf_put(p, 4, (s32)sec);
            pf_put(p + 4, 4, (s32)mic);
            ch = 1;
        }
        break;
    }
    case KT_RGB: {
        u8 *p = key_field(st, k, k->size);
        int w = k->size == 6 ? 2 : 1, c[3], j;
        const char *t = text;
        if (!p) goto nofield;
        if (*t == '#') t++;
        else if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t += 2;
        for (j = 0; j < 6 && hexval((unsigned char)t[j]) >= 0; j++) ;
        if (j != 6 || t[6]) {
            snprintf(err, errlen, "%s: a colour #RRGGBB", k->name);
            return 10;
        }
        for (j = 0; j < 3; j++) c[j] = hexval((unsigned char)t[2 * j]) * 16 + hexval((unsigned char)t[2 * j + 1]);
        for (j = 0; j < 3; j++)
            if (p[j * w] != c[j]) {        /* 16-bit: the high byte, repeated */
                p[j * w] = (u8)c[j];
                if (w == 2) p[j * w + 1] = (u8)c[j];
                ch = 1;
            }
        break;
    }
    case KT_VSTR: {
        u32 n = strlen(text), old;
        u8 *p = var_field(st, k, &old);
        if (!p) goto nofield;
        if (k->size && n > k->size) {
            snprintf(err, errlen, "%s: at most %d characters", k->name, k->size);
            return 10;
        }
        /* compare as shown: the text stops at a NUL (a pattern's planes
         * read as a name), giving that text back changes nothing */
        {
            u32 shown = 0;
            while (shown < old && p[shown]) shown++;
            if (shown == n && !memcmp(p, text, n)) break;
        }
        if (var_put(st, k, (const u8 *)text, n)) goto nomem;
        ch = 1;
        break;
    }
    case KT_BYTES: {
        u32 n = strlen(text) / 2, old, j;
        u8 *p = var_field(st, k, &old), *tmp;
        if (!p) goto nofield;
        if ((strlen(text) & 1) || (k->size && n != k->size)) {
            if (k->size) snprintf(err, errlen, "%s: %d hex digits", k->name, 2 * k->size);
            else snprintf(err, errlen, "%s: an even number of hex digits", k->name);
            return 10;
        }
        if (!(tmp = malloc(n ? n : 1))) goto nomem;
        for (j = 0; j < n; j++) {
            int a = hexval((unsigned char)text[2 * j]), b = hexval((unsigned char)text[2 * j + 1]);
            if (a < 0 || b < 0) {
                free(tmp);
                snprintf(err, errlen, "%s: hex digits only", k->name);
                return 10;
            }
            tmp[j] = (u8)(a * 16 + b);
        }
        if (old != n || memcmp(p, tmp, n)) {
            if (k->size) memcpy(p, tmp, n);
            else if (var_put(st, k, tmp, n)) { free(tmp); goto nomem; }
            ch = 1;
        }
        free(tmp);
        break;
    }
    case KT_DEC: {
        long long sv;
        s32 old;
        if (!dec_parse(k, text, &sv)) {
            snprintf(err, errlen, "%s: a number with up to %d decimals", k->name, (int)k->values[2]);
            return 10;
        }
        v = dec_raw(k, sv);
        if (!range_ok(k, v)) goto badrange;
        if (key_raw_get(st, k, &old)) goto nofield;
        /* compare as shown: an unchanged value keeps its exact raw value */
        if (dec_scaled(k, old) != sv) {
            if (key_raw_set(st, k, v)) goto nofield;
            ch = 1;
        }
        break;
    }
    case KT_CHAR: {
        u8 *p = key_field(st, k, 1);
        if (!p) goto nofield;
        if (strlen(text) != 1) {
            snprintf(err, errlen, "%s: one character", k->name);
            return 10;
        }
        if (p[0] != (u8)text[0]) {
            p[0] = (u8)text[0];
            ch = 1;
        }
        break;
    }
    default: {
        s32 old;
        if (k->type == KT_BOOL) {
            if (!parse_bool(text, &v)) {
                snprintf(err, errlen, "%s: Yes or No", k->name);
                return 10;
            }
            if (k->mask && v) v = (s32)(k->mask >> mask_shift(k->mask));   /* every bit of the mask */
        } else if (k->type == KT_ENUM) {
            int found = 0;
            for (i = 0; k->names[i]; i++)
                if (str_ieq(text, k->names[i])) {
                    v = k->values ? k->values[i] : i;
                    found = 1;
                    break;
                }
            if (!found) {
                s32 n;
                if (parse_int(text, &n))
                    for (i = 0; k->names[i]; i++)
                        if ((k->values ? k->values[i] : i) == n) { v = n; found = 1; break; }
            }
            if (!found) {
                int o = snprintf(err, errlen, "%s: one of", k->name);
                for (i = 0; k->names[i] && o < errlen - 1; i++)
                    o += snprintf(err + o, errlen - o, " %s", k->names[i]);
                return 10;
            }
        } else {
            if (!parse_int(text, &v) || !range_ok(k, v)) goto badrange;
        }
        if (key_raw_get(st, k, &old)) goto nofield;
        /* an unset variable equal to its default stays unset */
        if (old != v) {
            if (key_raw_set(st, k, v)) goto nofield;
            ch = 1;
        }
        break;
    }
    }
    if (!ch) return 0;
    st->changed = 1;
    /* hooks run only when the value really changed */
    return k->hook ? k->hook(st, k, err, errlen) : 0;

badrange:
    if (k->min <= k->max) {
        if (k->type == KT_HEX)
            snprintf(err, errlen, "%s: a number from 0x%lX to 0x%lX", k->name,
                     (unsigned long)(u32)k->min, (unsigned long)(u32)k->max);
        else
            snprintf(err, errlen, "%s: a number from %ld to %ld", k->name, (long)k->min, (long)k->max);
    } else
        snprintf(err, errlen, "%s: a number", k->name);
    return 10;
nofield:
    snprintf(err, errlen, "%s: field missing in the file (chunk too short)", k->name);
    return 20;
nomem:
    snprintf(err, errlen, "out of memory");
    return 20;
}

void key_describe(const Key *k, char *buf, int len)
{
    int i, o;
    switch (k->type) {
    case KT_BOOL: snprintf(buf, len, "Yes/No"); break;
    case KT_ENUM:
        o = 0;
        buf[0] = 0;
        for (i = 0; k->names[i] && o < len - 1; i++)
            o += snprintf(buf + o, len - o, "%s%s", i ? "/" : "", k->names[i]);
        break;
    case KT_STR: snprintf(buf, len, "text, max %d", k->size - 1); break;
    case KT_STRLIST: snprintf(buf, len, "up to %d items, comma-separated", k->count); break;
    case KT_CHAR: snprintf(buf, len, "one character"); break;
    case KT_MS:
        if (k->min <= k->max) snprintf(buf, len, "ms, %ld-%ld", (long)k->min, (long)k->max);
        else snprintf(buf, len, "ms");
        break;
    case KT_HEX: snprintf(buf, len, "hex"); break;
    case KT_RGB: snprintf(buf, len, "colour #RRGGBB"); break;
    case KT_VSTR: if (k->size) snprintf(buf, len, "text, max %d", k->size); else snprintf(buf, len, "text"); break;
    case KT_BYTES: if (k->size) snprintf(buf, len, "%d bytes in hex", k->size); else snprintf(buf, len, "bytes in hex"); break;
    case KT_DEC: {
        char a[24], b[24];
        if (k->min > k->max) snprintf(buf, len, "decimal, %d places", (int)k->values[2]);
        else {
            dec_format(k, dec_scaled(k, k->min), a, sizeof(a));
            dec_format(k, dec_scaled(k, k->max), b, sizeof(b));
            snprintf(buf, len, "%s to %s", a, b);
        }
        break;
    }
    default:
        if (k->min <= k->max) snprintf(buf, len, "%ld-%ld", (long)k->min, (long)k->max);
        else snprintf(buf, len, "number");
        break;
    }
}
