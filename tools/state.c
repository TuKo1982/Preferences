/* state - load a module's current settings and write them back.
 *
 * IFF modules, as the common prefs editor code does it: the defaults, then
 * the chunks of ENV:Sys/<file> on top (no file: the defaults, silently);
 * Use writes ENV:Sys/, Save ENVARC:Sys/ then ENV:Sys/ (Module.mflags for the
 * editors that differ); every write is a new file: PRHD of zeros and the
 * module's chunks. IPrefs picks the change up from ENV:.
 * ENV modules: one global variable per key; only changed keys are written.
 * FROM: an IFF file's chunks replace the settings; a text file of Name=Value
 * lines (";" starts a comment) is applied on top of the current settings.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "module.h"
#include "state.h"
#include "io.h"

void st_free(ModState *st)
{
    int i;
    pf_free(&st->pf);
    if (st->env) {
        for (i = 0; i < st->m->nkeys; i++) free(st->env[i]);
        free(st->env);
    }
    free(st->envDirty);
    st->env = NULL;
    st->envDirty = NULL;
}

/* the defaults of an IFF module: a new file with its chunks, then init() */
static int iff_defaults(ModState *st)
{
    int i;
    if (!pf_new(&st->pf)) return 0;
    for (i = 0; i < st->m->nchunks; i++)
        if (!pf_need(&st->pf, &st->m->chunks[i], NULL)) return 0;
    if (st->m->init) st->m->init(&st->pf);
    return 1;
}

static void err_file(char *err, int errlen, const char *path)
{
    char fault[100];
    char fmt[120];
    io_fault(fault, sizeof(fault));
    snprintf(fmt, sizeof(fmt), "%s\n%%s", io_text(4, "Error in file\n%s"));
    snprintf(err, errlen, fmt, path, fault);
}

static void err_iff(char *err, int errlen, const char *path)
{
    snprintf(err, errlen, io_text(5, "Error in IFF file\n%s"), path);
}

/* the chunks of a file on top of the settings, as the editors read it:
 * 0 done, 1 no such file (err set), -1 not a valid preferences file (err
 * set, nothing changed).
 * The chunks are taken in file order, as an IFF loop that reads each one into the settings: of two chunks of one
 * kind the last wins (XF_CHUNK_FIRST: the first). A refused file changes nothing (the editors keep what they read
 * before the error: not copied). */
static int iff_merge(ModState *st, const char *path, char *err, int errlen)
{
    const Module *m = st->m, *x = MOD_EXT(m);
    unsigned long xf = x ? x->xflags : 0;
    PFile in, w;
    u8 *buf, *d, taken[256];
    u32 len, sz, id, pos = 0;
    int i, prhd = 0, any = 0, last = 0, bad = 0, first = 1;
    if (!io_load(path, &buf, &len)) { err_file(err, errlen, path); return 1; }
    pf_init(&in);
    pf_init(&w);
    if (!pf_take(&in, buf, len)) goto refused;                 /* buf freed by pf_take */
    if (!(w.data = malloc(st->pf.cap ? st->pf.cap : 1))) goto refused;
    memcpy(w.data, st->pf.data, st->pf.len);
    w.len = st->pf.len;
    w.cap = st->pf.cap ? st->pf.cap : 1;
    memset(taken, 0, sizeof(taken));
    if (x && x->check_chunk) x->check_chunk(0, NULL, 0);          /* a new read starts (result not used) */
    while (!bad && (d = pf_next(&in, &pos, &id, &sz)) != NULL) {
        int known = 0;
        if ((xf & XF_PRHD_FIRST) && first && id != PF_ID('P','R','H','D')) { bad = 1; break; }
        first = 0;
        if (id == PF_ID('P','R','H','D')) {
            /* ReadChunkBytes(6): fewer bytes are an error, or end the read, or are not looked at */
            if (sz < 6) {
                if (xf & XF_PRHD_SHORT) { prhd = 1; continue; }
                if ((xf & XF_PRHD_STOP) && any) break;
                bad = 1;
            } else if (d[0] != 0 && !(m->mflags & MF_PRHD_ANY))
                bad = 1;
            prhd = 1;
            continue;
        }
        for (i = 0; i < m->nchunks; i++) if (m->chunks[i].id == id) known = 1;
        if (!known) continue;
        if (sz == 0 && (xf & XF_ZERO_SKIP)) { last = 0; continue; }
        if (x && x->check_chunk) {
            int r = x->check_chunk(id, d, sz);
            if (r < 0) { bad = 1; continue; }
            if (r > 0) { last = 0; continue; }
        }
        for (i = 0; i < m->nchunks && (m->chunks[i].id != id || !pf_match(d, sz, &m->chunks[i])); i++) ;
        if (i == m->nchunks) {
            /* too short to hold its selector (a FONT under 16 bytes): ReadChunkBytes(size) came short all the same
             * (a 0-byte chunk can be an error, V4 25.11) */
            if (!(m->mflags & MF_VARCHUNKS) && (m->mflags & MF_SIZE_ERROR))
                for (i = 0; i < m->nchunks; i++)
                    if (m->chunks[i].id == id && m->chunks[i].selSize
                        && sz < (u32)m->chunks[i].selOff + m->chunks[i].selSize && sz < m->chunks[i].size) bad = 1;
            last = 0;
            continue;
        }
        if (!(m->mflags & MF_VARCHUNKS)) {
            /* ReadChunkBytes(size): a longer chunk gives its first bytes, a shorter one is an IFF error or ignored */
            if (sz < m->chunks[i].size) {
                if (m->mflags & MF_SIZE_ERROR) bad = 1;
                last = 0;
                continue;
            }
            sz = m->chunks[i].size;
        }
        if ((xf & XF_CHUNK_FIRST) && taken[i]) continue;
        {
            u8 *t = pf_resize(&w, &m->chunks[i], sz);
            if (!t) { bad = 1; break; }
            memcpy(t, d, sz);
        }
        taken[i] = 1;
        any = last = 1;
    }
    if (!bad && !prhd && ((xf & XF_NEED_PRHD) || ((xf & XF_PRHD_OR_CHUNK) && !last))) bad = 1;
    if (!bad && (xf & XF_PRHD_FIRST) && first) bad = 1;                  /* an empty FORM: no PRHD first */
    if (!bad && (xf & XF_NEED_CHUNK) && !any) bad = 1;
    if (bad) goto refused;
    for (i = 0; m->keep_ids && m->keep_ids[i]; i++)
        if (pf_has_id(&in, m->keep_ids[i])
            || ((m->mflags & MF_KEEP_RESET) && m->nchunks && pf_find(&in, &m->chunks[0], NULL))) {
            pf_remove_id(&w, m->keep_ids[i]);
            pf_copy_id(&w, &in, m->keep_ids[i]);
        }
    pf_free(&in);
    pf_free(&st->pf);
    st->pf = w;
    if ((m->mflags & MF_AFTERLOAD) && m->after_load) m->after_load(&st->pf);
    st->fresh = 0;
    return 0;
refused:
    pf_free(&in);
    pf_free(&w);
    err_iff(err, errlen, path);
    return -1;
}

static int apply_text(ModState *st, const char *text, u32 len, char *err, int errlen)
{
    char line[600];
    u32 p = 0;
    int lineno = 0;
    while (p < len) {
        u32 n = 0;
        char *eq, *name, *val, *c;
        const Key *k;
        int rc;
        while (p < len && text[p] != '\n') {
            if (n < sizeof(line) - 1) line[n++] = text[p];
            p++;
        }
        p++;
        lineno++;
        line[n] = 0;
        if ((c = strchr(line, ';')) != NULL) *c = 0;
        for (c = line + strlen(line); c > line && (c[-1] == ' ' || c[-1] == '\t' || c[-1] == '\r'); ) *--c = 0;
        name = line;
        while (*name == ' ' || *name == '\t') name++;
        if (!*name) continue;
        eq = strchr(name, '=');
        if (!eq) {
            snprintf(err, errlen, "line %d: Name=Value expected", lineno);
            return 10;
        }
        *eq = 0;
        val = eq + 1;
        for (c = eq; c > name && (c[-1] == ' ' || c[-1] == '\t'); ) *--c = 0;
        while (*val == ' ' || *val == '\t') val++;
        k = key_find(st->m, name);
        if (!k) {
            snprintf(err, errlen, "line %d: unknown key %s", lineno, name);
            return 10;
        }
        if ((rc = key_set(st, k, val, err, errlen)) != 0) return rc;
    }
    return 0;
}

static int load_current(ModState *st, char *err, int errlen)
{
    char path[160];
    int i, rc;
    const Module *x = MOD_EXT(st->m);
    if (st->m->kind == MK_ENV) {
        st->env = calloc(st->m->nkeys, sizeof(char *));
        st->envDirty = calloc(st->m->nkeys, 1);
        if (!st->env || !st->envDirty) goto nomem;
        for (i = 0; i < st->m->nkeys; i++) {
            char v[AP_ENVLEN];
            if (io_getvar(st->m->keys[i].name, v, sizeof(v))) {
                st->env[i] = malloc(strlen(v) + 1);
                if (!st->env[i]) goto nomem;
                strcpy(st->env[i], v);
            }
        }
        return 0;
    }
    if (st->m->kind != MK_IFF) return 0;
    if (!iff_defaults(st)) goto nomem;
    st->fresh = 1;
    snprintf(path, sizeof(path), "ENV:Sys/%s", st->file ? st->file : st->m->file);
    rc = iff_merge(st, path, st->note, sizeof(st->note));
    if (rc != 0 && x && (x->xflags & XF_ENVARC_FALLBACK)) {   /* ENV: missing or refused */
        snprintf(path, sizeof(path), "ENVARC:Sys/%s", st->file ? st->file : st->m->file);
        rc = iff_merge(st, path, st->note, sizeof(st->note));                  /* a refused file changed nothing */
    }
    if (rc >= 0 && st->fresh) st->note[0] = 0;   /* no file: silent */
    return 0;
nomem:
    snprintf(err, errlen, "out of memory");
    return 20;
}

int st_load(ModState *st, const Module *m, const char *from, char *err, int errlen)
{
    return st_load_file(st, m, NULL, from, err, errlen);
}

/* the same for another file of the module (a unit's: printer2.prefs), NULL = its own */
int st_load_file(ModState *st, const Module *m, const char *file, const char *from, char *err, int errlen)
{
    int rc;
    memset(st, 0, sizeof(*st));
    st->m = m;
    st->file = file;
    pf_init(&st->pf);
    err[0] = 0;
    if (from) {
        u8 *buf;
        u32 len;
        if (!io_load(from, &buf, &len)) {
            snprintf(err, errlen, "cannot read %s", from);
            return 20;
        }
        if (len >= 4 && !memcmp(buf, "FORM", 4)) {
            /* the editors' start with FROM: their defaults, then the FROM file, ENV: not read (measured on the
             * V4 25.11) */
            free(buf);
            if (m->kind != MK_IFF || !iff_defaults(st)) return 20;
            st->fresh = 1;
            if (iff_merge(st, from, err, errlen)) return 20;
            st->changed = 1;
        } else {
            if ((rc = load_current(st, err, errlen)) != 0) { free(buf); return rc; }   /* Name=Value: on top of ENV: */
            rc = apply_text(st, (const char *)buf, len, err, errlen);
            free(buf);
            if (rc) return rc;
        }
        return 0;
    }
    return load_current(st, err, errlen);
}

/* the file of a unit (UNIT=n, n > 0) of an editor that has units: the number before ".prefs" (Printer.prefs ->
 * Printer2.prefs) (VSNPrintf "ENV:Sys/Printer%ld.prefs") */
const char *st_unit_file(const Module *m, long unit)
{
    static char name[64];
    const Module *x = MOD_EXT(m);
    const char *dot;
    int n;
    if (!x || !(x->xflags & XF_UNITS) || unit <= 0 || m->kind != MK_IFF || !m->file) return NULL;
    dot = strrchr(m->file, '.');
    n = dot ? (int)(dot - m->file) : (int)strlen(m->file);
    snprintf(name, sizeof(name), "%.*s%ld%s", n, m->file, unit, dot ? dot : "");
    return name;
}

int st_save(ModState *st, int archive, char *err, int errlen)
{
    char path[160];
    int i;
    err[0] = 0;
    if (st->m->kind == MK_IFF) {
        const Module *m = st->m;
        int k, failed = 0;
        if (!pf_rebuild(&st->pf, m->chunks, m->nchunks, m->keep_ids)) goto fail;
        for (k = 0; k < 2; k++) {
            /* the order: ENVARC then ENV, or ENV then ENVARC */
            int arc = (m->mflags & MF_ENVFIRST) ? k == 1 : k == 0;
            if (arc && !archive) continue;
            if (failed && !(m->mflags & MF_NOSTOP)) break;
            snprintf(path, sizeof(path), "%s:Sys/%s", arc ? "ENVARC" : "ENV", st->file ? st->file : m->file);
            if (MOD_EXT(m) && m->before_file && !m->before_file(st, arc)) goto fail;   /* that file's own bytes */
            if (!io_save(path, st->pf.data, st->pf.len)) { failed = 1; continue; }
            if ((arc || (m->mflags & MF_PROTECT_ENV)) && !(m->mflags & MF_NOPROTECT)) io_protect(path);
        }
        if (failed) {
            err_file(err, errlen, path);
            return 20;
        }
    } else if (st->m->kind == MK_ENV) {
        for (i = 0; i < st->m->nkeys; i++)
            if (st->envDirty[i] || (archive && st->env[i])) {
                if (!io_setvar(st->m->keys[i].name, st->env[i] ? st->env[i] : "", archive)) {
                    snprintf(path, sizeof(path), "variable %s", st->m->keys[i].name);
                    goto fail;
                }
                st->envDirty[i] = 0;
            }
    }
    st->changed = 0;
    return 0;
fail:
    snprintf(err, errlen, "cannot write %s", path);
    return 20;
}

int st_needs_reboot(const ModState *st)
{
    int i;
    if (st->m->kind != MK_ENV) return 0;
    for (i = 0; i < st->m->nkeys; i++)
        if (st->envDirty[i] && (st->m->keys[i].flags & KF_REBOOT)) return 1;
    return 0;
}

/* the module's defaults: a new file (init()), or every variable at its def */
int st_defaults(ModState *st, const Module *m, char *err, int errlen)
{
    int i;
    memset(st, 0, sizeof(*st));
    st->m = m;
    pf_init(&st->pf);
    err[0] = 0;
    if (m->kind == MK_IFF) {
        if (!iff_defaults(st)) goto nomem;
        st->fresh = 1;
    } else if (m->kind == MK_ENV) {
        st->env = calloc(m->nkeys ? m->nkeys : 1, sizeof(char *));
        st->envDirty = calloc(m->nkeys ? m->nkeys : 1, 1);
        if (!st->env || !st->envDirty) goto nomem;
        for (i = 0; i < m->nkeys; i++) {
            const char *d = m->keys[i].def ? m->keys[i].def : (m->keys[i].type == KT_STR ? "" : "0");
            if (!(st->env[i] = malloc(strlen(d) + 1))) goto nomem;
            strcpy(st->env[i], d);
            st->envDirty[i] = 1;
        }
    }
    st->changed = 1;
    return 0;
nomem:
    snprintf(err, errlen, "out of memory");
    return 20;
}

/* a copy of a state (for Restore and the menus that read on top of it) */
int st_copy(ModState *dst, const ModState *src)
{
    int i;
    memset(dst, 0, sizeof(*dst));
    dst->m = src->m;
    pf_init(&dst->pf);
    if (src->pf.data) {
        if (!(dst->pf.data = malloc(src->pf.cap ? src->pf.cap : 1))) return 0;
        memcpy(dst->pf.data, src->pf.data, src->pf.len);
        dst->pf.len = src->pf.len;
        dst->pf.cap = src->pf.cap ? src->pf.cap : 1;
    }
    if (src->env) {
        dst->env = calloc(src->m->nkeys ? src->m->nkeys : 1, sizeof(char *));
        dst->envDirty = calloc(src->m->nkeys ? src->m->nkeys : 1, 1);
        if (!dst->env || !dst->envDirty) return 0;
        for (i = 0; i < src->m->nkeys; i++) {
            if (src->env[i] && (dst->env[i] = malloc(strlen(src->env[i]) + 1)) != NULL) strcpy(dst->env[i], src->env[i]);
            dst->envDirty[i] = src->envDirty[i];
        }
    }
    dst->changed = src->changed;
    dst->fresh = src->fresh;
    dst->file = src->file;
    return 1;
}

/* a file read on top of the current state, into n (Last Saved: ENVARC,
 * Restore of the editors that read again: ENV). No file: an error and the
 * state unchanged, or the defaults (MF_MISS_DEF) */
int st_reload(ModState *n, const ModState *cur, const char *path, char *err, int errlen)
{
    int r;
    err[0] = 0;
    if (!st_copy(n, cur)) { snprintf(err, errlen, "out of memory"); return 20; }
    r = iff_merge(n, path, err, errlen);
    if (r == 0) return 0;
    if (r == 1 && (cur->m->mflags & MF_MISS_DEF)) {
        st_free(n);
        return st_defaults(n, cur->m, err, errlen);
    }
    st_free(n);
    return 10;
}

/* what Save last wrote: ENVARC:Sys/<file>, or the ENVARC: copy of each variable */
int st_load_saved(ModState *st, const ModState *cur, char *err, int errlen)
{
    const Module *m = cur->m;
    char path[160];
    int i, rc;
    if (m->kind == MK_IFF) {
        snprintf(path, sizeof(path), "ENVARC:Sys/%s", cur->file ? cur->file : m->file);
        return st_reload(st, cur, path, err, errlen);
    }
    if ((rc = st_load(st, m, NULL, err, errlen)) != 0 || m->kind != MK_ENV) return rc;
    for (i = 0; i < m->nkeys; i++) {
        u8 *buf;
        u32 len;
        char *nv;
        snprintf(path, sizeof(path), "ENVARC:%s", m->keys[i].name);
        if (!io_load(path, &buf, &len)) continue;
        while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
        if ((nv = malloc(len + 1)) != NULL) {
            memcpy(nv, buf, len);
            nv[len] = 0;
            if (!st->env[i] || strcmp(st->env[i], nv)) {
                free(st->env[i]);
                st->env[i] = nv;
                st->envDirty[i] = 1;
                st->changed = 1;
            } else
                free(nv);
        }
        free(buf);
    }
    return 0;
}

/* the settings into a file of the user's (Save As): the IFF file itself, or
 * Name=Value lines (both read back by FROM=) */
int st_save_as(ModState *st, const char *path, char *err, int errlen)
{
    err[0] = 0;
    if (st->m->kind == MK_IFF) {
        if (pf_rebuild(&st->pf, st->m->chunks, st->m->nchunks, st->m->keep_ids) &&
            io_save(path, st->pf.data, st->pf.len)) {
            if (!(st->m->mflags & MF_NOPROTECT)) io_protect(path);
            return 0;
        }
    } else {
        char *text = malloc(st->m->nkeys * 300 + 1), v[256];
        int i, o = 0, ok;
        if (!text) { snprintf(err, errlen, "out of memory"); return 20; }
        text[0] = 0;
        for (i = 0; i < st->m->nkeys; i++)
            if (!key_get(st, &st->m->keys[i], v, sizeof(v)))
                o += snprintf(text + o, 300, "%s=%s\n", st->m->keys[i].name, v);
        ok = io_save(path, (const u8 *)text, o);
        free(text);
        if (ok) return 0;
    }
    snprintf(err, errlen, "cannot write %s", path);
    return 20;
}

/* UniPrefs' start arguments the editors take themselves (APHost.start_arg): USER and UNIT */
static char arg_user[64], arg_unit[16];
long start_unit;

void start_args_set(const char *user, long unit)
{
    if (user != arg_user) snprintf(arg_user, sizeof(arg_user), "%s", user ? user : "");
    start_unit = unit;
    if (unit) snprintf(arg_unit, sizeof(arg_unit), "%ld", unit);
    else arg_unit[0] = 0;
}

/* INSTALLOS: the OS whose modules the user chose to download at start-up (no module of the installed OS was
 * there): the Updates page installs them when it is first shown */
static char arg_install[48];

void start_install_set(const char *os)
{
    snprintf(arg_install, sizeof(arg_install), "%s", os ? os : "");
}

const char *start_arg(const char *name)
{
    if (str_ieq(name, "USER")) return arg_user[0] ? arg_user : NULL;
    if (str_ieq(name, "UNIT")) return arg_unit[0] ? arg_unit : NULL;
    if (str_ieq(name, "INSTALLOS")) return arg_install[0] ? arg_install : NULL;
    return NULL;
}
