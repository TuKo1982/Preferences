/* Updates (page "Modules" since 25.11.2026) - lists the installed program and module files and installs newer ones published as assets of
 * the latest GitHub release of a repository (docs/updates-spec-2026-11-24.md).
 *
 * Scan: the program (PROGDIR:UniPrefs) and every AP_MODDIR/#?.module file, versions read from the files
 *       without loading them ($VER of binary files, Version= of text modules), and their info ("$APINFO:" of
 *       AP_INFO, Author=/BasedOn=/Description= of text modules) for the panel on the right of the list.
 * Check: downloads manifest.txt of <base> = https://github.com/<owner>/<repo>/releases/latest/download/ of two
 *       repositories (user 28.11.2026): the core one (ENV:UniPrefs/UpdateRepo) for the program, About and Updates
 *       only, the modules one (ENV:UniPrefs/ModulesRepo) for every other module only; each file is taken from
 *       its own repository, whatever the other lists. (ENV:UniPrefs/UpdateBase and ModulesBase replace the bases,
 *       for tests.) Then compares; then info.txt (optional: older
 *       releases have none) for the panel of modules not installed.
 * Show / OS: filters of the list (25.11.2026); Select all: the rows shown.
 * Install selected / double click on a row: those modules only (installed or not); Update all: every one with a newer version. Each
 * entry is updated on its own: asset downloaded, size and
 *       SHA-256 checked, written as <file>.new beside the file, the old file renamed <file>.old, the new
 *       one renamed into place. A failure leaves the installed file as it was.
 *
 * The parts above the GUI (manifest, versions, SHA-256, URLs) are pure C, tested on the PC (tests/test_core.c);
 * the network is modules/Updates/updnet.c (bsdsocket.library + AmiSSL 5), linked into this module only.
 */
#include "kit.h"
#include "Updates/updcore.h"
#ifndef AP_NO_GUI
static int put_file(const char *path, const void *b, u32 l);
#define UPF_SAVE put_file
#include "uprefs.h"
#endif

/* ---- pure part (host-tested) ------------------------------------------------------------------------- */

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_blank(int c) { return c == ' ' || c == '\t'; }

/* "1", "0.1", "12.3.4": digits separated by single dots, at most 23 characters */
int upd_version_ok(const char *v)
{
    int n = 0, digits = 0;
    if (!v || !*v) return 0;
    for (; *v; v++, n++) {
        if (n >= 23) return 0;
        if (*v == '.') { if (!digits) return 0; digits = 0; }
        else if (is_digit((unsigned char)*v)) { if (++digits > 9) return 0; }
        else return 0;
    }
    return digits > 0;
}

/* numeric, part by part ("1.10" > "1.9"; a missing part counts 0: "1" = "1.0"): -1, 0, 1 */
int upd_vercmp(const char *a, const char *b)
{
    while (*a || *b) {
        unsigned long x = 0, y = 0;
        while (is_digit((unsigned char)*a)) x = x * 10 + (unsigned long)(*a++ - '0');
        while (is_digit((unsigned char)*b)) y = y * 10 + (unsigned long)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* a module name: 1-30 letters, digits or '_' */
static int name_ok(const char *s, int len)
{
    int i;
    if (len < 1 || len > 30) return 0;
    for (i = 0; i < len; i++)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || is_digit((unsigned char)s[i]) || s[i] == '_'))
            return 0;
    return 1;
}

/* AP_PROG_TARGET (the program), or "Modules/<Name>.module" */
int upd_target_ok(const char *t)
{
    int l = (int)strlen(t);
    if (!strcmp(t, AP_PROG_TARGET)) return 1;
    return l > 15 && !strncmp(t, "Modules/", 8) && !strcmp(t + l - 7, ".module") && name_ok(t + 8, l - 15);
}

/* where a target lives on the Amiga */
int upd_target_path(const char *t, char *path, int len)
{
    if (!upd_target_ok(t)) return 0;
    if (!strcmp(t, AP_PROG_TARGET)) return snprintf(path, len, "PROGDIR:%s", AP_PROGNAME) < len;
    return snprintf(path, len, "%s/%s", AP_MODDIR, t + 8) < len;
}

static int asset_ok(const char *a)
{
    int i;
    if (!*a || *a == '.' || strlen(a) > 100) return 0;
    for (i = 0; a[i]; i++)
        if (!((a[i] >= 'A' && a[i] <= 'Z') || (a[i] >= 'a' && a[i] <= 'z') || is_digit((unsigned char)a[i]) ||
              a[i] == '.' || a[i] == '_' || a[i] == '-'))
            return 0;
    return 1;
}

static int sha_ok(const char *s)
{
    int i;
    for (i = 0; i < 64; i++)
        if (!(is_digit((unsigned char)s[i]) || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return !s[64];
}

#define PERR(...) do { snprintf(err, errlen, __VA_ARGS__); return 0; } while (0)

/* the whole manifest or nothing: 1 = fine (*n items), 0 = refused, err says where and why */
int upd_parse_manifest(const char *text, u32 len, UpdItem *items, int max, int *n, char *err, int errlen)
{
    u32 pos = 0;
    int line = 0, i;
    *n = 0;
    err[0] = 0;
    if (memchr(text, 0, len)) PERR("not a text file");
    while (pos < len) {
        const char *s = text + pos, *e = memchr(s, '\n', len - pos), *f[6];
        char tmp[400];
        int l = e ? (int)(e - s) : (int)(len - pos), nf = 0, k;
        pos += l + (e ? 1 : 0);
        line++;
        if (l > 0 && s[l - 1] == '\r') l--;
        if (line == 1) {
            if (l != (int)strlen(UPD_HEADER) || memcmp(s, UPD_HEADER, l)) PERR("line 1: \"" UPD_HEADER "\" expected");
            continue;
        }
        while (l > 0 && is_blank((unsigned char)*s)) s++, l--;
        while (l > 0 && is_blank((unsigned char)s[l - 1])) l--;
        if (!l || *s == '#') continue;
        if (l >= (int)sizeof(tmp)) PERR("line %d: too long", line);
        memcpy(tmp, s, l);
        tmp[l] = 0;
        for (k = 0; k < l && nf < 6; ) {            /* fields: separated by blanks */
            f[nf++] = tmp + k;
            while (k < l && !is_blank((unsigned char)tmp[k])) k++;
            while (k < l && is_blank((unsigned char)tmp[k])) tmp[k++] = 0;
        }
        if (nf != 5) PERR("line %d: 5 fields expected (target version size sha256 asset)", line);
        if (*n >= max) PERR("line %d: more than %d files", line, max);
        if (strlen(f[0]) >= sizeof(items->target) || !upd_target_ok(f[0])) PERR("line %d: bad target \"%s\"", line, f[0]);
        if (!upd_version_ok(f[1])) PERR("line %d: bad version \"%s\"", line, f[1]);
        {
            u32 sz = 0;
            const char *p = f[2];
            if (*p == '0' || strlen(p) > 8) PERR("line %d: bad size", line);
            for (; *p; p++) {
                if (!is_digit((unsigned char)*p)) PERR("line %d: bad size", line);
                sz = sz * 10 + (u32)(*p - '0');
            }
            if (sz > UPD_MAXSIZE) PERR("line %d: size above %lu", line, (unsigned long)UPD_MAXSIZE);
            items[*n].size = sz;
        }
        if (!sha_ok(f[3])) PERR("line %d: bad SHA-256 (64 lower-case hex digits)", line);
        if (!asset_ok(f[4])) PERR("line %d: bad asset name \"%s\"", line, f[4]);
        for (i = 0; i < *n; i++) {
            if (!strcmp(items[i].target, f[0])) PERR("line %d: %s twice", line, f[0]);
            if (!strcmp(items[i].asset, f[4])) PERR("line %d: asset %s twice", line, f[4]);
        }
        strcpy(items[*n].target, f[0]);
        strcpy(items[*n].version, f[1]);
        strcpy(items[*n].sha, f[3]);
        strcpy(items[*n].asset, f[4]);
        (*n)++;
    }
    if (!line) PERR("empty");
    return 1;
}

/* "#obsolete <target> [<new target>]" lines of manifest.txt: files of earlier releases that this one no longer
 * has, removed or renamed (then the new name); Update all removes them (kept as .old) and installs the new name.
 * '#' lines are comments for Updates 0.4 and older, so a release with them still reads there. The count found
 * (at most max) */
int upd_parse_obsolete(const char *text, u32 len, char (*out)[48], char (*rep)[48], int max)
{
    u32 pos = 0;
    int n = 0;
    while (pos < len && n < max) {
        const char *s = text + pos, *e = memchr(s, '\n', len - pos);
        int l = e ? (int)(e - s) : (int)(len - pos), k, a, b;
        pos += l + (e ? 1 : 0);
        while (l > 0 && (s[l - 1] == '\r' || is_blank((unsigned char)s[l - 1]))) l--;
        if (l < 11 || memcmp(s, "#obsolete", 9) || !is_blank((unsigned char)s[9])) continue;
        for (k = 10; k < l && is_blank((unsigned char)s[k]); k++) ;
        for (a = k; a < l && !is_blank((unsigned char)s[a]); a++) ;     /* the target: k..a */
        for (b = a; b < l && is_blank((unsigned char)s[b]); b++) ;      /* the new name: b..l */
        if (a - k < 1 || a - k >= 48 || l - b >= 48) continue;
        memcpy(out[n], s + k, a - k);
        out[n][a - k] = 0;
        memcpy(rep[n], s + b, l - b);
        rep[n][l - b] = 0;
        if (!upd_target_ok(out[n]) || !strcmp(out[n], AP_PROG_TARGET)) continue;   /* never the program */
        if (rep[n][0] && (!upd_target_ok(rep[n]) || strchr(rep[n], ' ') || strchr(rep[n], '\t'))) continue;
        n++;
    }
    return n;
}

/* the version a file carries, read as Scan reads it: a binary file (hunk header) its "$VER: <name> <ver>"
 * (a module: name "<Name>.module" exactly; the program: the first $VER), a text module its last
 * "Version=" line (as reg_parse_launcher). 1 = found */
int upd_file_version(const u8 *b, u32 len, const char *target, char *out, int outlen)
{
    char want[64];
    u32 i, wl;
    out[0] = 0;
    if (len >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 3 && b[3] == 0xF3) {
        /* '$' apart: no "$VER:" text of its own in this module, the Version command would read it */
        if (!strcmp(target, AP_PROG_TARGET)) snprintf(want, sizeof(want), "%cVER: ", '$');
        else if (!strncmp(target, "Modules/", 8)) snprintf(want, sizeof(want), "%cVER: %s ", '$', target + 8);
        else return 0;
        wl = (u32)strlen(want);
        for (i = 0; i + wl < len; i++) {
            u32 p = i + wl, k = 0;
            char v[24];
            if (memcmp(b + i, want, wl)) continue;
            if (wl == 6) {                             /* the program: skip its name */
                while (p < len && b[p] > ' ') p++;
                if (p >= len || b[p] != ' ') continue;
                p++;
            }
            while (p < len && k < sizeof(v) - 1 && (is_digit(b[p]) || b[p] == '.')) v[k++] = (char)b[p++];
            v[k] = 0;
            if ((p < len && b[p] != ' ' && b[p] != 0) || !upd_version_ok(v)) continue;
            snprintf(out, outlen, "%s", v);
            return 1;
        }
        return 0;
    }
    for (i = 0; i < len; ) {                           /* text module */
        u32 s = i, e = i, ke, vs, ve;
        while (e < len && b[e] != '\n') e++;
        i = e + 1;
        while (s < e && (is_blank(b[s]) || b[s] == '\r')) s++;
        if (s >= e || b[s] == ';') continue;
        for (ke = s; ke < e && b[ke] != '='; ke++) ;
        if (ke >= e) continue;
        vs = ke + 1;
        while (ke > s && (is_blank(b[ke - 1]) || b[ke - 1] == '\r')) ke--;
        if (ke - s != 7 || memcmp(b + s, "Version", 7)) continue;
        while (vs < e && (is_blank(b[vs]) || b[vs] == '\r')) vs++;
        ve = e;
        while (ve > vs && (is_blank(b[ve - 1]) || b[ve - 1] == '\r')) ve--;
        if (ve - vs >= (u32)outlen) ve = vs + (u32)outlen - 1;
        memcpy(out, b + vs, ve - vs);
        out[ve - vs] = 0;
    }
    return out[0] != 0;
}

/* a field: control characters (tabs included) become spaces, cut to the buffer */
static void info_copy(char *dst, int dl, const u8 *src, u32 n)
{
    u32 i;
    if (n > (u32)dl - 1) n = (u32)dl - 1;
    for (i = 0; i < n; i++) dst[i] = src[i] < 0x20 || src[i] == 0x7f ? ' ' : (char)src[i];
    dst[n] = 0;
}

/* the info a file carries, read as Scan reads it: a binary file (hunk header) its first well-formed
 * "$APINFO:" author TAB based-on TAB description (AP_INFO, kit.h), a text module its last Author=, BasedOn=
 * and Description= lines. 1 = found (some field not empty) */
int upd_file_info(const u8 *b, u32 len, UpdInfo *out)
{
    char want[12];
    u32 i, wl;
    memset(out, 0, sizeof(*out));
    if (len >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 3 && b[3] == 0xF3) {
        /* '$' apart: this module's own AP_INFO is then its only "$APINFO:" text */
        snprintf(want, sizeof(want), "%cAPINFO:", '$');
        wl = (u32)strlen(want);
        for (i = 0; i + wl < len; i++) {
            u32 p = i + wl, e = p, t[3], nt = 0, bad = 0;
            if (memcmp(b + i, want, wl)) continue;
            while (e < len && b[e] && e - p < 600) {
                if (b[e] == '\t') { if (nt < 3) t[nt] = e; nt++; }
                else if (b[e] < 0x20) bad = 1;
                e++;
            }
            if (e >= len || b[e] || (nt != 2 && nt != 3) || bad) continue;   /* NUL-terminated, 3 or 4 fields */
            info_copy(out->author, sizeof(out->author), b + p, t[0] - p);
            info_copy(out->based, sizeof(out->based), b + t[0] + 1, t[1] - t[0] - 1);
            info_copy(out->desc, sizeof(out->desc), b + t[1] + 1, (nt == 3 ? t[2] : e) - t[1] - 1);
            if (nt == 3) info_copy(out->os, sizeof(out->os), b + t[2] + 1, e - t[2] - 1);
            return out->author[0] || out->based[0] || out->desc[0] || out->os[0];
        }
        return 0;
    }
    for (i = 0; i < len; ) {                           /* text module, as upd_file_version */
        u32 s = i, e = i, ke, vs, ve;
        while (e < len && b[e] != '\n') e++;
        i = e + 1;
        while (s < e && (is_blank(b[s]) || b[s] == '\r')) s++;
        if (s >= e || b[s] == ';') continue;
        for (ke = s; ke < e && b[ke] != '='; ke++) ;
        if (ke >= e) continue;
        vs = ke + 1;
        while (ke > s && (is_blank(b[ke - 1]) || b[ke - 1] == '\r')) ke--;
        while (vs < e && (is_blank(b[vs]) || b[vs] == '\r')) vs++;
        ve = e;
        while (ve > vs && (is_blank(b[ve - 1]) || b[ve - 1] == '\r')) ve--;
        if (ke - s == 6 && !memcmp(b + s, "Author", 6)) info_copy(out->author, sizeof(out->author), b + vs, ve - vs);
        else if (ke - s == 7 && !memcmp(b + s, "BasedOn", 7)) info_copy(out->based, sizeof(out->based), b + vs, ve - vs);
        else if (ke - s == 11 && !memcmp(b + s, "Description", 11)) info_copy(out->desc, sizeof(out->desc), b + vs, ve - vs);
        else if (ke - s == 2 && !memcmp(b + s, "OS", 2)) info_copy(out->os, sizeof(out->os), b + vs, ve - vs);
    }
    return out->author[0] || out->based[0] || out->desc[0] || out->os[0];
}

/* info.txt: line 1 UPD_INFO_HEADER, then "<target> TAB <author> TAB <based on> TAB <description> [TAB <OS>]" lines
 * (the OS field since 24.11: Updates 0.4 ignores it). Only the header is required (0 = refused, err says why):
 * a line that is not of this form (fewer fields, bad target, control characters, a target twice, more than max)
 * is skipped and counted; fields after the fifth are ignored; empty lines and '#' lines are ignored. Long fields
 * are cut. */
int upd_parse_info(const char *text, u32 len, UpdInfoLine *lines, int max, int *n, int *skipped, char *err, int errlen)
{
    u32 pos = 0;
    int line = 0;
    *n = *skipped = 0;
    err[0] = 0;
    if (memchr(text, 0, len)) PERR("not a text file");
    while (pos < len) {
        const char *s = text + pos, *e = memchr(s, '\n', len - pos), *f[6];
        u32 fl[5];
        int l = e ? (int)(e - s) : (int)(len - pos), nf = 0, k, bad = 0;
        pos += l + (e ? 1 : 0);
        line++;
        if (l > 0 && s[l - 1] == '\r') l--;
        if (line == 1) {
            if (l != (int)strlen(UPD_INFO_HEADER) || memcmp(s, UPD_INFO_HEADER, l)) PERR("line 1: \"" UPD_INFO_HEADER "\" expected");
            continue;
        }
        for (k = 0; k < l && (s[k] == ' ' || s[k] == '\t'); k++) ;
        if (k == l || s[k] == '#') continue;
        f[0] = s;
        for (k = 0; k < l && nf < 5; k++) {
            if (s[k] == '\t') { fl[nf] = (u32)(s + k - f[nf]); f[++nf] = s + k + 1; }
            else if ((unsigned char)s[k] < 0x20 || s[k] == 0x7f) bad = 1;
        }
        if (nf == 3) fl[3] = (u32)(s + l - f[3]);
        if (nf == 4) fl[4] = (u32)(s + l - f[4]);
        if (nf < 3 || bad || fl[0] >= sizeof(lines->target) || fl[0] < 1) { (*skipped)++; continue; }
        {
            char t[48];
            memcpy(t, f[0], fl[0]);
            t[fl[0]] = 0;
            if (!upd_target_ok(t) || upd_info_find(lines, *n, t) || *n >= max) { (*skipped)++; continue; }
            memset(&lines[*n], 0, sizeof(lines[*n]));
            strcpy(lines[*n].target, t);
        }
        info_copy(lines[*n].info.author, sizeof(lines->info.author), (const u8 *)f[1], fl[1]);
        info_copy(lines[*n].info.based, sizeof(lines->info.based), (const u8 *)f[2], fl[2]);
        info_copy(lines[*n].info.desc, sizeof(lines->info.desc), (const u8 *)f[3], fl[3]);
        if (nf >= 4) info_copy(lines[*n].info.os, sizeof(lines->info.os), (const u8 *)f[4], fl[4]);
        (*n)++;
    }
    if (!line) PERR("empty");
    return 1;
}

const UpdInfoLine *upd_info_find(const UpdInfoLine *lines, int n, const char *target)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(lines[i].target, target)) return &lines[i];
    return NULL;
}

/* what the panel shows: the installed file's info when it has one, else info.txt's, else "-";
 * an empty field is "-" too */
const char *upd_info_field(const UpdInfo *inst, const UpdInfo *listed, int field)
{
    const UpdInfo *u = inst && (inst->author[0] || inst->based[0] || inst->desc[0] || inst->os[0]) ? inst : listed;
    const char *v;
    if (!u) return "-";
    v = field == 0 ? u->author : field == 1 ? u->based : field == 2 ? u->desc : u->os;
    return *v ? v : "-";
}

/* what a row says (Check done): installed found? its version; in the manifest? its version */
const char *upd_status(int found, const char *inst, int listed, const char *avail)
{
    if (!listed) return found ? "local only" : "";
    if (!found) return "not installed";
    if (!upd_version_ok(inst)) return "unknown version";
    switch (upd_vercmp(inst, avail)) {
    case -1: return "newer available";
    case 0: return "up to date";
    default: return "installed is newer";
    }
}

/* "owner/repo": GitHub's characters, one slash */
int upd_repo_ok(const char *r)
{
    int slash = 0, part = 0;
    if (!r || strlen(r) > 100) return 0;
    for (; *r; r++) {
        if (*r == '/') { if (!part || slash++) return 0; part = 0; continue; }
        if (!((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z') || is_digit((unsigned char)*r) ||
              *r == '-' || *r == '_' || *r == '.'))
            return 0;
        part++;
    }
    return slash == 1 && part > 0;
}

int upd_core_target(const char *t)
{
    static const char *const core[] = { AP_PROG_TARGET, "Modules/About.module", "Modules/Updates.module", NULL };
    int i;
    for (i = 0; core[i]; i++) {
        const char *a = core[i], *b = t;
        while (*a && *b && (*a | 0x20) == (*b | 0x20)) a++, b++;
        if (!*a && !*b) return 1;
    }
    return 0;
}

static int ieq_tail(const char *s, size_t n, const char *t)   /* the last strlen(t) of s's n characters = t */
{
    size_t k = strlen(t);
    if (n < k) return 0;
    for (s += n - k; *t; s++, t++) if ((*s | 0x20) != (*t | 0x20)) return 0;
    return 1;
}

int upd_old_name(const char *name, int where)
{
    size_t n = strlen(name);
    if (n < 5 || (!ieq_tail(name, n, ".old") && !ieq_tail(name, n, ".new"))) return 0;
    n -= 4;
    if (where) {
        static const char *const prog[] = { AP_PROGNAME, AP_PROG_TARGET, NULL };
        int i;
        for (i = 0; prog[i]; i++) if (n == strlen(prog[i]) && ieq_tail(name, n, prog[i])) return 1;
        return 0;
    }
    return n > 7 && ieq_tail(name, n, ".module");
}

int upd_split_url(const char *url, int *tls, char *host, int hostlen, int *port, char *path, int pathlen)
{
    const char *p;
    int h = 0;
    if (!strncmp(url, "https://", 8)) { *tls = 1; *port = 443; p = url + 8; }
    else if (!strncmp(url, "http://", 7)) { *tls = 0; *port = 80; p = url + 7; }
    else return 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') {
        if (h >= hostlen - 1 || (unsigned char)*p <= ' ' || *p == '@') return 0;
        host[h++] = *p++;
    }
    host[h] = 0;
    if (!h) return 0;
    if (*p == ':') {
        long v = 0;
        p++;
        if (!is_digit((unsigned char)*p)) return 0;
        while (is_digit((unsigned char)*p)) { v = v * 10 + (*p++ - '0'); if (v > 65535) return 0; }
        if (!v) return 0;
        *port = (int)v;
    }
    if (*p && *p != '/') return 0;
    if ((int)strlen(*p ? p : "/") >= pathlen) return 0;
    strcpy(path, *p ? p : "/");
    for (; *p; p++) if ((unsigned char)*p <= ' ') return 0;
    return 1;
}

/* SHA-256 (FIPS 180-4) */

static const u32 sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(UpdSha *s, const u8 *p)
{
    u32 w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++) w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (i = 16; i < 64; i++)
        w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
               (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

void upd_sha_init(UpdSha *s)
{
    static const u32 h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, h0, sizeof(h0));
    s->lo = s->hi = s->n = 0;
}

void upd_sha_update(UpdSha *s, const u8 *p, u32 len)
{
    u32 old = s->lo;
    s->lo += len << 3;
    s->hi += (len >> 29) + (s->lo < old);
    while (len) {
        u32 k = 64 - s->n < len ? 64 - s->n : len;
        memcpy(s->buf + s->n, p, k);
        s->n += k; p += k; len -= k;
        if (s->n == 64) { sha_block(s, s->buf); s->n = 0; }
    }
}

/* the digest as 64 lower-case hex digits */
void upd_sha_final(UpdSha *s, char *hex)
{
    u8 pad[72];
    u32 lo = s->lo, hi = s->hi, k = s->n < 56 ? 56 - s->n : 120 - s->n;
    int i;
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 4; i++) { pad[k + i] = (u8)(hi >> (24 - 8 * i)); pad[k + 4 + i] = (u8)(lo >> (24 - 8 * i)); }
    upd_sha_update(s, pad, k + 8);
    for (i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", (unsigned)((s->h[i / 4] >> (24 - 8 * (i % 4))) & 0xff));
}

void upd_sha256(const u8 *p, u32 len, char *hex)
{
    UpdSha s;
    upd_sha_init(&s);
    upd_sha_update(&s, p, len);
    upd_sha_final(&s, hex);
}

/* ---- the page ------------------------------------------------------------------------------------------- */

#ifndef AP_NO_GUI
#include "Updates/updnet.h"

typedef struct Ent {
    char name[40];          /* shown */
    char target[48];
    char inst[24];          /* "" = no file */
    int found;
    int item;               /* in items[], -1 = not in the manifest */
    const char *status;
    UpdInfo *info;          /* the file's AP_INFO / Author= lines, NULL = none */
} Ent;

static Ent ents[UPD_MAXITEMS * 2];
static int nents;
static UpdItem items[UPD_MAXITEMS];
static int nitems, checked;
static char obsolete[UPD_MAXITEMS][48];      /* "#obsolete" targets of the last Check */
static char renamed[UPD_MAXITEMS][48];       /* their new names ("" = removed) */
static int nobsolete;
static UpdInfoLine *ilines;     /* info.txt of the last Check, NULL = none */
static int nilines;
static const UpdInfo prog_info = { UPD_PROG_AUTHOR, UPD_PROG_BASEDON, UPD_PROG_DESC };
static Object *t_name, *t_inst, *t_avail, *t_status, *t_author, *t_based, *t_os, *f_desc;
static char active_target[48], help_based[128];
static Object *lst, *logl, *s_repo, *s_mrepo, *t_state, *b_scan, *b_check, *b_update, *b_all, *b_remove, *b_cancel, *b_save;
static Object *cy_show, *cy_os, *b_selall, *b_clean;
/* the list's filters (25.11.2026): what is shown, and of which OS (the file's or info.txt's OS field) */
static const char *const show_names[] = { "All", "Installed", "Not installed", "Newer available", NULL };
static const char *const os_names[] = {
    "All", AP_CATEGORY_OS30, AP_CATEGORY_OS31, AP_CATEGORY_OS314, AP_CATEGORY_OS323, AP_CATEGORY_OS35, AP_CATEGORY_OS39,
    AP_CATEGORY_APOLLOOS, "No OS", NULL
};
static int f_show, f_os;
static int busy, cancel;
static char base[2][300];      /* 0 the core repository's release, 1 the modules repository's */
static unsigned char isrc[UPD_MAXITEMS];   /* items[i] comes from base[isrc[i]] */
static const char *const src_name[2] = { "core", "modules" };

static void logs(const char *t)
{
    LONG n = 0;
    DoMethod(logl, MUIM_List_InsertSingle, (ULONG)t, MUIV_List_Insert_Bottom);
    get(logl, MUIA_List_Entries, &n);
    if (n > 0) DoMethod(logl, MUIM_List_Jump, n - 1);
}

static void logp(const char *fmt, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    logs(line);
}

static void state(const char *t) { set(t_state, MUIA_Text_Contents, (ULONG)t); }

static void busy_set(int on, const char *what)
{
    busy = on;
    cancel = 0;
    set(b_scan, MUIA_Disabled, on);
    set(b_check, MUIA_Disabled, on);
    set(b_update, MUIA_Disabled, on);
    set(b_all, MUIA_Disabled, on);
    set(b_remove, MUIA_Disabled, on);
    set(cy_show, MUIA_Disabled, on);
    set(cy_os, MUIA_Disabled, on);
    set(b_selall, MUIA_Disabled, on);
    set(b_save, MUIA_Disabled, on);
    set(s_repo, MUIA_Disabled, on);
    set(b_cancel, MUIA_Disabled, !on);
    state(on ? what : "");
}

/* ---- settings ---- */

static void get_var(const char *name, char *buf, int len)
{
    if (GetVar((STRPTR)name, (STRPTR)buf, len, GVF_GLOBAL_ONLY) < 0) buf[0] = 0;
    buf[len - 1] = 0;
}

static const char *const repo_var[2] = { AP_UPDATE_REPO_VAR, AP_MODULES_REPO_VAR };
static const char *const repo_def[2] = { AP_UPDATE_REPO, AP_MODULES_REPO };
static const char *const base_var[2] = { AP_UPDATE_BASE_VAR, AP_MODULES_BASE_VAR };

/* chunk REPO of UniPrefs.prefs (28.11.2026), else the variable of before; "" = the default */
static void repo_now(int src, char *r, int len)
{
    if (!upf_get("REPO", src, r, len)) get_var(repo_var[src], r, len);
    if (!r[0]) snprintf(r, len, "%s", repo_def[src]);
}

/* the base URL of a repository's latest release, ending in '/'; 0 = none */
static int base_url(int src, char *repo, int rlen)
{
    char o[280];
    repo_now(src, repo, rlen);
    get_var(base_var[src], o, sizeof(o));
    if (o[0]) {
        snprintf(base[src], sizeof(base[src]), "%s%s", o, o[strlen(o) - 1] == '/' ? "" : "/");
        return 1;
    }
    if (!upd_repo_ok(repo)) return 0;
    snprintf(base[src], sizeof(base[src]), "https://github.com/%s/releases/latest/download/", repo);
    return 1;
}

static int put_file(const char *path, const void *b, u32 l)
{
    BPTR f = Open((STRPTR)path, MODE_NEWFILE);
    LONG w;
    if (!f) return 0;
    w = Write(f, (APTR)b, (LONG)l);
    return Close(f) && w == (LONG)l;
}

HOOK(save_func)
{
    char path[80];
    const char *r[2];
    int i, k;
    (void)h; (void)o; (void)args;
    if (busy) return 0;
    for (k = 0; k < 2; k++) {
        LONG v = 0;
        get(k ? s_mrepo : s_repo, MUIA_String_Contents, &v);
        r[k] = v ? (const char *)v : "";
        if (*r[k] && !upd_repo_ok(r[k])) { logp("\"%s\" is not a repository name (owner/repo).", r[k]); return 0; }
    }
    {   /* chunk REPO of ENV(ARC):UniPrefs.prefs (user 28.11.2026); the default = "" */
        const char *s[2];
        for (k = 0; k < 2; k++) s[k] = !strcmp(r[k], repo_def[k]) ? "" : r[k];
        if (!upf_set("REPO", s, 2)) { logp("Could not write %s.", UPF_ARC); return 0; }
    }
    for (k = 0; k < 2; k++)                            /* the variables of before 28.11.2026 */
        for (i = 0; i < 2; i++) {
            snprintf(path, sizeof(path), "%s%s", i ? "ENVARC:" : "ENV:", repo_var[k]);
            DeleteFile((STRPTR)path);
        }
    for (k = 0; k < 2; k++) {
        if (!*r[k]) nnset(k ? s_mrepo : s_repo, MUIA_String_Contents, (ULONG)repo_def[k]);
        logp("%s repository: %s saved.", k ? "Modules" : "Core", *r[k] ? r[k] : *repo_def[k] ? repo_def[k] : "none");
    }
    checked = 0;
    return 0;
}
HOOKDEF(save_hook, save_func);

/* ---- Scan ---- */

static int ends_module(const char *s)
{
    size_t n = strlen(s);
    return n > 7 && !Stricmp((STRPTR)(s + n - 7), (STRPTR)".module");
}

static void add_ent(const char *target, int found)
{
    Ent *e;
    char path[160];
    u8 *b = NULL;
    u32 l = 0;
    if (nents >= (int)SIZEOF(ents)) return;
    e = &ents[nents++];
    memset(e, 0, sizeof(*e));
    e->item = -1;
    e->status = "";
    snprintf(e->target, sizeof(e->target), "%s", target);
    snprintf(e->name, sizeof(e->name), "%s", !strcmp(target, AP_PROG_TARGET) ? AP_PROGNAME
             : strncmp(target, "Modules/", 8) ? target : target + 8);
    if (!strncmp(target, "Modules/", 8)) e->name[strlen(e->name) - 7] = 0;
    e->found = found;
    if (!found || !upd_target_path(target, path, sizeof(path))) return;
    if (io_load(path, &b, &l)) {
        UpdInfo in;
        if (!upd_file_version(b, l, target, e->inst, sizeof(e->inst))) strcpy(e->inst, "?");
        if (strcmp(target, AP_PROG_TARGET) && upd_file_info(b, l, &in) && (e->info = malloc(sizeof(in))) != NULL)
            *e->info = in;
    }
    free(b);
}

static int ent_cmp(const Ent *a, const Ent *b)
{
    int pa = !strcmp(a->target, AP_PROG_TARGET), pb = !strcmp(b->target, AP_PROG_TARGET);
    if (pa != pb) return pb - pa;                      /* the program first */
    return Stricmp((STRPTR)a->name, (STRPTR)b->name);
}

static void show_info(const Ent *e);

/* the work Update all does on a row: newer, the new name of an installed module, or obsolete */
static int has_work(const Ent *e)
{
    return (e->item >= 0 && (!strcmp(e->status, "newer available") || !strcmp(e->status, "new name")))
           || !strcmp(e->status, "obsolete");
}

static const char *ent_os(const Ent *e)
{
    const UpdInfoLine *ln = upd_info_find(ilines, nilines, e->target);
    if (!strcmp(e->target, AP_PROG_TARGET)) return "-";
    return upd_info_field(e->info, ln ? &ln->info : NULL, 3);
}

/* the row passes the filters (Show, OS) */
static int visible(const Ent *e)
{
    if (f_show == 1 && !e->found) return 0;
    if (f_show == 2 && e->found) return 0;
    if (f_show == 3 && !has_work(e)) return 0;
    if (f_os > 0) {
        const char *os = ent_os(e);
        if (!os_names[f_os + 1] ? strcmp(os, "-") != 0 : Stricmp((STRPTR)os, (STRPTR)os_names[f_os]) != 0) return 0;
    }
    return 1;
}

static void show_list(void)
{
    char keep[48];
    int i, j, act = -1;
    snprintf(keep, sizeof(keep), "%s", active_target);  /* Clear makes the list inactive */
    for (i = 1; i < nents; i++)                        /* sorted */
        for (j = i; j > 0 && ent_cmp(&ents[j - 1], &ents[j]) > 0; j--) {
            Ent t = ents[j]; ents[j] = ents[j - 1]; ents[j - 1] = t;
        }
    set(lst, MUIA_List_Quiet, TRUE);
    DoMethod(lst, MUIM_List_Clear);
    for (i = 0, j = 0; i < nents; i++) {
        if (!visible(&ents[i])) continue;
        DoMethod(lst, MUIM_List_InsertSingle, (ULONG)&ents[i], MUIV_List_Insert_Bottom);
        if (keep[0] && !strcmp(ents[i].target, keep)) act = j;
        j++;
    }
    set(lst, MUIA_List_Quiet, FALSE);
    snprintf(active_target, sizeof(active_target), "%s", keep);
    if (act >= 0) set(lst, MUIA_List_Active, act);    /* the same module again: the panel follows */
    else show_info(NULL);
}

static void scan(void)
{
    char path[160];
    BPTR lock;
    struct FileInfoBlock *fib;
    int nmod = 0;
    while (nents > 0) { free(ents[--nents].info); ents[nents].info = NULL; }
    snprintf(path, sizeof(path), "PROGDIR:%s", AP_PROGNAME);
    add_ent(AP_PROG_TARGET, io_exists(path));
    if ((lock = Lock((STRPTR)AP_MODDIR, ACCESS_READ)) != 0) {
        if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
            if (Examine(lock, fib))
                while (ExNext(lock, fib)) {
                    const char *name = (const char *)fib->fib_FileName;
                    char t[48];
                    if (fib->fib_DirEntryType > 0 || !ends_module(name) || strlen(name) > 37) continue;
                    snprintf(t, sizeof(t), "Modules/%s", name);
                    add_ent(t, 1);
                    nmod++;
                }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(lock);
    } else logp("%s not found.", AP_MODDIR);
    logp("Scan: the program and %d module files.", nmod);
}

static void merge(void)
{
    int i, j;
    for (i = 0; i < nitems; i++) {
        for (j = 0; j < nents; j++)
            if (!Stricmp((STRPTR)ents[j].target, (STRPTR)items[i].target)) break;
        if (j == nents) {
            if (nents >= (int)SIZEOF(ents)) break;
            add_ent(items[i].target, 0);
        }
        ents[j].item = i;
    }
    for (j = 0; j < nents; j++) {
        Ent *e = &ents[j];
        int k;
        e->status = upd_status(e->found, e->inst, e->item >= 0, e->item >= 0 ? items[e->item].version : "");
        if (e->found && e->item < 0)                   /* renamed or removed since: Update all removes it */
            for (k = 0; k < nobsolete; k++)
                if (!Stricmp((STRPTR)e->target, (STRPTR)obsolete[k])) { e->status = "obsolete"; break; }
    }
    for (j = 0; j < nents; j++) {                      /* the new name of an installed old one: Update all adds it */
        Ent *e = &ents[j];
        int k, i2;
        if (e->found || e->item < 0) continue;
        for (k = 0; k < nobsolete; k++) {
            if (Stricmp((STRPTR)e->target, (STRPTR)renamed[k])) continue;
            for (i2 = 0; i2 < nents; i2++)
                if (ents[i2].found && !Stricmp((STRPTR)ents[i2].target, (STRPTR)obsolete[k])) break;
            if (i2 < nents) { e->status = "new name"; break; }
        }
    }
}

HOOK(scan_func)
{
    (void)h; (void)o; (void)args;
    if (busy) return 0;
    scan();
    if (checked) merge();
    show_list();
    return 0;
}
HOOKDEF(scan_hook, scan_func);

/* ---- network ---- */

static UpdNet net;
static const char *what_now;

static int net_poll(void *ud, unsigned long got, unsigned long total)
{
    static char t[120];
    (void)ud;
    if (what_now) {
        if (total) snprintf(t, sizeof(t), "%s: %lu of %lu bytes", what_now, got, total);
        else snprintf(t, sizeof(t), "%s: %lu bytes", what_now, got);
        state(t);
    }
    DoMethod(app, MUIM_Application_InputBuffered);     /* the window keeps answering; Cancel sets cancel */
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) cancel = 1;
    return cancel;
}

static void net_log(void *ud, const char *line) { (void)ud; logs(line); }

static int net_up(void)
{
    net.timeout = 30;
    net.poll = net_poll;
    net.log = net_log;
    if (updnet_open(&net)) { logs(net.err); return 0; }
    return 1;
}

/* info.txt of the same release, after manifest.txt: the panel's author, based on and description for the
 * modules not installed (or whose file has none). Missing (releases before Updates 0.3) or bad = not an error. */
static void get_info(int src)
{
    char url[400], perr[120];
    unsigned char *b = NULL;
    unsigned long l = 0;
    int status, r, skipped = 0, n = 0, i, k;
    if (!ilines && !(ilines = malloc(UPD_MAXITEMS * sizeof(*ilines)))) return;
    snprintf(url, sizeof(url), "%sinfo-all.txt", base[src]);   /* every file (0.8); else info.txt (160 at most) */
    what_now = "info.txt";
    r = updnet_get(&net, url, UPD_INFO_MAX, &b, &l, &status);
    if (r == UPD_HTTP && status == 404) {
        free(b); b = NULL; l = 0;
        snprintf(url, sizeof(url), "%sinfo.txt", base[src]);
        r = updnet_get(&net, url, UPD_INFO_MAX, &b, &l, &status);
    }
    what_now = NULL;
    if (r == UPD_HTTP && status == 404) logp("No info.txt in the %s release: module info from the installed files only.", src_name[src]);
    else if (r != UPD_OK) logp("%s info.txt not read: %s", src_name[src], net.err);
    else if (!upd_parse_info((const char *)b, (u32)l, ilines + nilines, UPD_MAXITEMS - nilines, &n, &skipped, perr, sizeof(perr)))
        logp("%s info.txt refused: %s", src_name[src], perr);
    else {
        for (i = k = 0; i < n; i++)                    /* only the files this repository may give */
            if (upd_core_target(ilines[nilines + i].target) == !src) ilines[nilines + k++] = ilines[nilines + i];
        nilines += k;
        logp(skipped ? "%s info.txt: %d modules (%d lines skipped)." : "%s info.txt: %d modules.", src_name[src], k, skipped);
    }
    free(b);
}

/* one repository's manifest, appended to items[]: only the files it may give (the core repository the program,
 * About and Updates; the modules repository the others). 1 = read */
static int check_one(int src)
{
    char url[400], repo[110], perr[120], ov[8];
    unsigned char *b = NULL;
    unsigned long l = 0;
    int status, r, n = 0, i, k, nobs, o0, refused = 0;
    if (!base_url(src, repo, sizeof(repo))) {
        if (!repo[0]) logp("No %s repository set: enter it (owner/repo) at the top of this page and press Save.", src_name[src]);
        else logp("No %s repository set (\"%s\" is not owner/repo).", src_name[src], repo);
        return 0;
    }
    get_var(base_var[src], ov, sizeof(ov));
    /* manifest-all.txt lists every file (releases from Updates 0.8 on); manifest.txt keeps at most 160 for
     * Updates 0.7 and older, which refuse a longer one: read when there is no manifest-all.txt */
    snprintf(url, sizeof(url), "%smanifest-all.txt", base[src]);
    logp("Check: %s", url);
    what_now = "manifest.txt";
    r = updnet_get(&net, url, 262144, &b, &l, &status);
    if (r == UPD_HTTP && status == 404) {
        free(b); b = NULL; l = 0;
        snprintf(url, sizeof(url), "%smanifest.txt", base[src]);
        logp("Check: %s", url);
        r = updnet_get(&net, url, 262144, &b, &l, &status);
    }
    what_now = NULL;
    if (r == UPD_HTTP && status == 404) {
        if (ov[0]) logp("No manifest.txt at %s.", base[src]);
        else logp("No release found in %s.", repo);
        free(b);
        return 0;
    }
    if (r != UPD_OK) { logp("Check of %s failed: %s", repo, net.err); free(b); return 0; }
    if (!upd_parse_manifest((const char *)b, (u32)l, items + nitems, UPD_MAXITEMS - nitems, &n, perr, sizeof(perr))) {
        logp("%s manifest.txt refused: %s", src_name[src], perr);
        free(b);
        return 0;
    }
    for (i = k = 0; i < n; i++) {
        if (upd_core_target(items[nitems + i].target) != !src) { refused++; continue; }
        items[nitems + k] = items[nitems + i];
        isrc[nitems + k++] = (unsigned char)src;
    }
    nitems += k;
    o0 = nobsolete;
    nobs = upd_parse_obsolete((const char *)b, (u32)l, obsolete + o0, renamed + o0, UPD_MAXITEMS - o0);
    for (i = 0; i < nobs; i++)
        if (upd_core_target(obsolete[o0 + i]) == !src) {
            memmove(obsolete[nobsolete], obsolete[o0 + i], 48);
            memmove(renamed[nobsolete], renamed[o0 + i], 48);
            nobsolete++;
        }
    free(b);
    logp(refused ? "%s manifest.txt: %d files (%d of the other repository ignored)." : "%s manifest.txt: %d files.",
         src_name[src], k, refused);
    get_info(src);
    return 1;
}

/* Check without the busy frame: both repositories; 1 = at least one manifest read */
static int check(void)
{
    int ok = 0, src;
    nitems = nobsolete = nilines = 0;
    scan();
    for (src = 0; src < 2 && !cancel; src++) ok |= check_one(src);
    if (!ok) return 0;
    checked = 1;
    merge();
    return 1;
}

HOOK(check_func)
{
    (void)h; (void)o; (void)args;
    if (busy) return 0;
    checked = 0;
    busy_set(1, "Checking...");
    if (net_up()) {
        check();
        updnet_close(&net);
    }
    show_list();
    busy_set(0, NULL);
    return 0;
}
HOOKDEF(check_hook, check_func);

/* one file: 1 = installed */
static int update_one(Ent *e)
{
    const UpdItem *it = &items[e->item];
    char url[400], path[160], tmp[170], old[170], hex[65];
    unsigned char *b = NULL;
    unsigned long l = 0;
    int status, r, had;
    if (!upd_target_path(it->target, path, sizeof(path))) return 0;
    snprintf(url, sizeof(url), "%s%s", base[isrc[e->item]], it->asset);
    logp("%s %s: downloading %s", e->name, it->version, url);
    what_now = e->name;
    r = updnet_get(&net, url, it->size, &b, &l, &status);
    what_now = NULL;
    if (r != UPD_OK) { logp("%s: download failed: %s", e->name, net.err); free(b); return 0; }
    if (l != it->size) { logp("%s: %lu bytes received, %lu expected: not installed.", e->name, l, (unsigned long)it->size); free(b); return 0; }
    upd_sha256(b, (u32)l, hex);
    if (strcmp(hex, it->sha)) { logp("%s: SHA-256 differs from the manifest: not installed.", e->name); free(b); return 0; }
    logp("%s: %lu bytes, SHA-256 matches.", e->name, l);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    snprintf(old, sizeof(old), "%s.old", path);
    if (!put_file(tmp, b, (u32)l)) {
        free(b);
        DeleteFile((STRPTR)tmp);
        logp("%s: could not write %s: not installed.", e->name, tmp);
        return 0;
    }
    free(b);
    had = io_exists(path);
    if (had) {
        if (io_exists(old)) {
            SetProtection((STRPTR)old, 0);
            if (!DeleteFile((STRPTR)old)) {
                DeleteFile((STRPTR)tmp);
                logp("%s: could not delete %s: not installed.", e->name, old);
                return 0;
            }
        }
        if (!Rename((STRPTR)path, (STRPTR)old)) {
            DeleteFile((STRPTR)tmp);
            logp("%s: could not rename %s: not installed.", e->name, path);
            return 0;
        }
    }
    if (!Rename((STRPTR)tmp, (STRPTR)path)) {
        if (had) Rename((STRPTR)old, (STRPTR)path);
        DeleteFile((STRPTR)tmp);
        logp("%s: could not rename %s: not installed.", e->name, tmp);
        return 0;
    }
    logp("%s: %s installed%s.", e->name, it->version, had ? " (the old file kept as .old)" : "");
    return 1;
}

/* an obsolete file (a module renamed or removed by the release): renamed to .old, as an updated file; 1 = done */
static int removable(const Ent *e);

static int remove_one(Ent *e)
{
    char path[160], old[170];
    if (!removable(e)) { logp("%s: listed as obsolete by the release, kept (never removed).", e->name); return 0; }
    if (!upd_target_path(e->target, path, sizeof(path))) return 0;
    snprintf(old, sizeof(old), "%s.old", path);
    if (io_exists(old)) { SetProtection((STRPTR)old, 0); DeleteFile((STRPTR)old); }
    if (!Rename((STRPTR)path, (STRPTR)old)) { logp("%s: could not rename %s: not removed.", e->name, path); return 0; }
    logp("%s: no longer in the release (renamed or removed): removed, kept as .old.", e->name);
    return 1;
}

/* args[0]: 0 = the selected rows (Update selected), 1 = every newer one (Update all), 2 = the row double-clicked */
HOOK(update_func)
{
    LONG pos = MUIV_List_NextSelected_Start, mode = (LONG)args[0];
    int i, nsel = 0, done = 0, failed = 0;
    static char sel[UPD_MAXITEMS * 2];
    (void)h; (void)o;
    if (busy) return 0;
    memset(sel, 0, sizeof(sel));
    if (mode == 2) {                                   /* the double-clicked row alone */
        Ent *e = NULL;
        DoMethod(lst, MUIM_List_GetEntry, MUIV_List_GetEntry_Active, (ULONG)&e);
        if (e) { sel[e - ents] = 1; nsel = 1; }
    } else if (mode == 0) for (;;) {                   /* the selection, before Check redraws the list */
        Ent *e = NULL;
        DoMethod(lst, MUIM_List_NextSelected, (ULONG)&pos);
        if (pos == MUIV_List_NextSelected_End) break;
        DoMethod(lst, MUIM_List_GetEntry, pos, (ULONG)&e);
        if (e) { sel[e - ents] = 1; nsel++; }
    }
    if (mode != 1 && !nsel) {
        logs("Select the modules to install or update in the list (or double-click one), or press Update all.");
        return 0;
    }
    busy_set(1, "Updating...");
    if (!net_up()) { busy_set(0, NULL); return 0; }
    {
        static char seltarget[UPD_MAXITEMS * 2][48];
        int ns = 0;
        for (i = 0; i < nents; i++) if (sel[i]) snprintf(seltarget[ns++], 48, "%s", ents[i].target);
        if (!check()) goto out;                        /* a fresh manifest and a fresh scan */
        memset(sel, 0, sizeof(sel));
        for (i = 0; i < nents; i++) {
            int k;
            for (k = 0; k < ns; k++) if (!strcmp(seltarget[k], ents[i].target)) sel[i] = 1;
            if (mode == 1) sel[i] = visible(&ents[i]) && has_work(&ents[i]);   /* the rows shown only */
        }
    }
    for (i = 0; i < nents && !cancel; i++) {
        Ent *e = &ents[i];
        if (!sel[i]) continue;
        if (!strcmp(e->status, "obsolete")) {
            if (remove_one(e)) done++;
            else failed++;
            continue;
        }
        if (e->item < 0) { logp("%s: not in the manifest, left alone.", e->name); continue; }
        if (mode != 1 && strcmp(e->status, "newer available") && strcmp(e->status, "not installed")
            && strcmp(e->status, "new name")) {
            logp("%s: %s, left alone.", e->name, e->status);  /* a chosen module: newer, or new in the release */
            continue;
        }
        if (update_one(e)) done++;
        else failed++;
    }
    if (cancel) logs("Stopped.");
    if (!done && !failed && !cancel) logs(mode != 1 ? "Nothing newer for the selected modules." : "Everything is up to date.");
    if (done) {
        logp("%d file%s installed or updated%s. Restart UniPrefs to use them.", done, done == 1 ? "" : "s",
             failed ? ", some failed" : "");
        scan();
        merge();
    }
out:
    updnet_close(&net);
    show_list();
    busy_set(0, NULL);
    if (done)
        MUI_Request(app, win, 0, (STRPTR)"Modules", (STRPTR)"OK",
                    (STRPTR)"%ld file(s) installed or updated.\nQuit and start UniPrefs again to use them.", (ULONG)done);
    return 0;
}
HOOKDEF(update_hook, update_func);

/* Remove selected (25.11.2026): the selected installed modules' files are deleted, after a requester. Never the
 * program, this module (the way to install them again) nor About (user 25.11.2026). Their pages stay until
 * UniPrefs is started again (the host loaded them at start-up). */
static int removable(const Ent *e)
{
    return e->found && !strncmp(e->target, "Modules/", 8) && Stricmp((STRPTR)e->target, (STRPTR)"Modules/Updates.module")
           && Stricmp((STRPTR)e->target, (STRPTR)"Modules/About.module");
}

HOOK(remove_func)
{
    LONG pos = MUIV_List_NextSelected_Start;
    static char sel[UPD_MAXITEMS * 2];
    char names[400], path[160];
    int i, n = 0, done = 0, len = 0, skipped = 0;
    (void)h; (void)o; (void)args;
    if (busy) return 0;
    memset(sel, 0, sizeof(sel));
    names[0] = 0;
    for (;;) {
        Ent *e = NULL;
        DoMethod(lst, MUIM_List_NextSelected, (ULONG)&pos);
        if (pos == MUIV_List_NextSelected_End) break;
        DoMethod(lst, MUIM_List_GetEntry, pos, (ULONG)&e);
        if (!e) continue;
        if (!removable(e)) { skipped++; continue; }
        sel[e - ents] = 1;
        if (n < 12) len += snprintf(names + len, sizeof(names) - len, "%s%s", n ? ", " : "", e->name);
        else if (n == 12) len += snprintf(names + len, sizeof(names) - len, "...");
        if (len >= (int)sizeof(names)) len = sizeof(names) - 1;
        n++;
    }
    if (skipped) logs("The program, About, Modules and modules not installed cannot be removed.");
    if (!n) { if (!skipped) logs("Select the installed modules to remove in the list."); return 0; }
    if (MUI_Request(app, win, 0, (STRPTR)"Modules", (STRPTR)"_Remove|_Cancel",
                    (STRPTR)"Remove %ld module(s)?\n\n%s\n\nTheir files are deleted from the modules drawer.",
                    (ULONG)n, (ULONG)names) != 1) return 0;
    for (i = 0; i < nents; i++) {
        Ent *e = &ents[i];
        if (!sel[i] || !upd_target_path(e->target, path, sizeof(path))) continue;
        SetProtection((STRPTR)path, 0);
        if (DeleteFile((STRPTR)path)) { logp("%s: removed.", e->name); done++; }
        else logp("%s: could not delete %s.", e->name, path);
    }
    if (done) {
        logp("%d module%s removed. Their pages stay until UniPrefs is started again.", done, done == 1 ? "" : "s");
        scan();
        if (checked) merge();
        show_list();
    }
    return 0;
}
HOOKDEF(remove_hook, remove_func);

/* the modules of one OS that are not installed (UniPrefs found none at start-up and the user chose Download:
 * start_arg "INSTALLOS", host version 6): Check, then each module of the release whose OS (its file's $APINFO,
 * else info.txt) is that one */
static void install_os(const char *os)
{
    int i, done = 0, failed = 0, n = 0;
    char mrepo[110];
    if (busy) return;
    repo_now(1, mrepo, sizeof(mrepo));
    if (!mrepo[0]) {                                  /* no default (user 28.11.2026): the user enters it */
        logp("No module of %s is installed, and no modules repository is set.", os);
        MUI_Request(app, win, 0, (STRPTR)"Modules", (STRPTR)"OK",
                    (STRPTR)"No modules repository is set.\nEnter it (owner/repo) in the field Modules at the top\n"
                    "of this page, press Save, then Check online.");
        return;
    }
    logp("No module of %s was installed: downloading them.", os);
    busy_set(1, "Installing...");
    if (!net_up()) { busy_set(0, NULL); return; }
    if (!check()) goto out;
    for (i = 0; i < nents && !cancel; i++) {
        Ent *e = &ents[i];
        const UpdInfoLine *ln = upd_info_find(ilines, nilines, e->target);
        if (e->found || e->item < 0 || strcmp(upd_info_field(e->info, ln ? &ln->info : NULL, 3), os)) continue;
        n++;
        if (update_one(e)) done++;
        else failed++;
    }
    if (cancel) logs("Stopped.");
    if (!n && !cancel) logp("The release has no module of %s.", os);
    if (done) {
        logp("%d module%s of %s installed%s. Restart UniPrefs to use them.", done, done == 1 ? "" : "s", os,
             failed ? ", some failed" : "");
        scan();
        merge();
    }
out:
    updnet_close(&net);
    show_list();
    busy_set(0, NULL);
    if (done)
        MUI_Request(app, win, 0, (STRPTR)"Modules", (STRPTR)"OK",
                    (STRPTR)"%ld module(s) of %s installed.\nQuit and start UniPrefs again to use them.",
                    (ULONG)done, (ULONG)os);
}

/* first shown after the start-up offer: install the modules of that OS, once */
static void show(Page *p)
{
    static int asked;
    const char *os;
    (void)p;
    if (asked || AP->version < 6 || !(os = start_arg("INSTALLOS"))) return;
    asked = 1;
    install_os(os);
}

HOOK(cancel_func) { (void)h; (void)o; (void)args; cancel = 1; state("Stopping..."); return 0; }
HOOKDEF(cancel_hook, cancel_func);

/* the panel: the module of the list's active row */
static void show_info(const Ent *e)
{
    const UpdInfo *inst = NULL;
    const UpdInfoLine *ln = NULL;
    if (!t_name) return;
    if (e) {
        inst = strcmp(e->target, AP_PROG_TARGET) ? e->info : &prog_info;
        ln = upd_info_find(ilines, nilines, e->target);
    }
    set(t_name, MUIA_Text_Contents, (ULONG)(e ? e->name : "-"));
    set(t_inst, MUIA_Text_Contents, (ULONG)(e && e->found ? e->inst : "-"));
    set(t_avail, MUIA_Text_Contents, (ULONG)(!e ? "-" : e->item >= 0 ? items[e->item].version : checked ? "-" : "(Check online)"));
    set(t_status, MUIA_Text_Contents, (ULONG)(e && e->status[0] ? e->status : "-"));
    set(t_author, MUIA_Text_Contents, (ULONG)upd_info_field(inst, ln ? &ln->info : NULL, 0));
    set(t_based, MUIA_Text_Contents, (ULONG)upd_info_field(inst, ln ? &ln->info : NULL, 1));
    snprintf(help_based, sizeof(help_based), "%s", upd_info_field(inst, ln ? &ln->info : NULL, 1));
    set(t_based, MUIA_ShortHelp, (ULONG)help_based);  /* the whole text, when the panel cuts it */
    set(t_os, MUIA_Text_Contents, (ULONG)upd_info_field(inst, ln ? &ln->info : NULL, 3));
    set(f_desc, MUIA_Floattext_Text, (ULONG)(e ? upd_info_field(inst, ln ? &ln->info : NULL, 2) : ""));
}

HOOK(active_func)
{
    LONG a = (LONG)args[0];
    Ent *e = NULL;
    (void)h; (void)o;
    if (a >= 0) DoMethod(lst, MUIM_List_GetEntry, a, (ULONG)&e);
    if (e) snprintf(active_target, sizeof(active_target), "%s", e->target);
    else active_target[0] = 0;
    show_info(e);
    return 0;
}
HOOKDEF(active_hook, active_func);

static Object *info_text(void) { return TextObject, MUIA_Text_SetMin, FALSE, End; }

HOOK(disp_func)
{
    char **col = (char **)o;
    const Ent *e = (const Ent *)args;
    (void)h;
    if (!e) {
        col[0] = "\33bName"; col[1] = "\33bInstalled"; col[2] = "\33bAvailable"; col[3] = "\33bStatus";
        return 0;
    }
    col[0] = (char *)e->name;
    col[1] = (char *)(e->found ? e->inst : "-");
    col[2] = (char *)(e->item >= 0 ? items[e->item].version : "");
    col[3] = (char *)e->status;
    if (!strcmp(e->status, "newer available") || !strcmp(e->status, "obsolete") || !strcmp(e->status, "new name")) {
        /* work waiting for Update all: the whole row in bold */
        static char b[4][80];
        int i;
        for (i = 0; i < 4; i++) { snprintf(b[i], sizeof(b[i]), "\33b%s", col[i]); col[i] = b[i]; }
    }
    return 0;
}
HOOKDEF(disp_hook, disp_func);

/* a filter changed: the list again */
HOOK(filter_func)
{
    LONG v = 0;
    (void)h; (void)o; (void)args;
    get(cy_show, MUIA_Cycle_Active, &v); f_show = (int)v;
    get(cy_os, MUIA_Cycle_Active, &v); f_os = (int)v;
    if (!checked && (f_show == 2 || f_show == 3)) logs("Press Check online to list the modules of the release.");
    show_list();
    return 0;
}
HOOKDEF(filter_hook, filter_func);

/* Select all: every row shown */
HOOK(selall_func)
{
    (void)h; (void)o; (void)args;
    DoMethod(lst, MUIM_List_Select, MUIV_List_Select_All, MUIV_List_Select_On, NULL);
    return 0;
}
HOOKDEF(selall_hook, selall_func);

/* Clean old modules (user 28.11.2026): deletes the copies the updates keep (<file>.old) and unfinished
 * downloads (<file>.new) of the modules and of the program, after a requester; never Modules/Originals */
#define CLEAN_MAX 512
HOOK(clean_func)
{
    static char names[CLEAN_MAX][160];
    static const char *const dirs[2] = { AP_MODDIR, "PROGDIR:" };
    int n = 0, d, i, done = 0;
    unsigned long bytes = 0;
    (void)h; (void)o; (void)args;
    if (busy) return 0;
    for (d = 0; d < 2; d++) {
        BPTR lock = Lock((STRPTR)dirs[d], ACCESS_READ);
        struct FileInfoBlock *fib;
        if (!lock) continue;
        if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
            if (Examine(lock, fib))
                while (ExNext(lock, fib) && n < CLEAN_MAX) {
                    const char *name = (const char *)fib->fib_FileName;
                    if (fib->fib_DirEntryType > 0 || !upd_old_name(name, d)) continue;
                    snprintf(names[n++], sizeof(names[0]), "%s%s%s", dirs[d], d ? "" : "/", name);
                    bytes += (unsigned long)fib->fib_Size;
                }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(lock);
    }
    if (!n) { logs("Clean old modules: no old module or program file (.old, .new)."); return 0; }
    if (!MUI_Request(app, win, 0, (STRPTR)"Modules", (STRPTR)"_Delete|_Cancel",
                     (STRPTR)"Delete %ld old files (%ld KB)?\n\nThe copies kept by the updates (.old) and unfinished\n"
                     "downloads (.new) of the modules and of UniPrefs.", (ULONG)n, (ULONG)((bytes + 1023) / 1024)))
        return 0;
    for (i = 0; i < n; i++) {
        SetProtection((STRPTR)names[i], 0);
        if (DeleteFile((STRPTR)names[i])) { done++; logp("Deleted %s.", names[i]); }
        else logp("Could not delete %s.", names[i]);
    }
    logp("Clean old modules: %d of %d files deleted.", done, n);
    return 0;
}
HOOKDEF(clean_hook, clean_func);

static Object *button(const char *label, struct Hook *hk)
{
    Object *b = SimpleButton(label);
    if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Self, 3, MUIM_CallHook, (ULONG)hk, 0);
    return b;
}

static Object *page(Page *p)
{
    char repo[110], mrepo[110];
    Object *g, *lv = NULL, *panel;
    p->buttons = PB_NOWRITE;                       /* Scan, Check and Update act at once */
    p->on_show = show;                             /* the modules of the installed OS, when asked at start-up */
    repo_now(0, repo, sizeof(repo));
    repo_now(1, mrepo, sizeof(mrepo));
    lst = ListObject, InputListFrame, MUIA_List_Format, (ULONG)"BAR,BAR,BAR,", MUIA_List_Title, TRUE,
        MUIA_List_DisplayHook, (ULONG)&disp_hook, End;
    logl = str_list();
    s_repo = StringObject, StringFrame, MUIA_String_MaxLen, 101, MUIA_String_Contents, (ULONG)repo,
        MUIA_CycleChain, 1, End;
    s_mrepo = StringObject, StringFrame, MUIA_String_MaxLen, 101, MUIA_String_Contents, (ULONG)mrepo,
        MUIA_CycleChain, 1, End;
    t_state = TextObject, TextFrame, MUIA_Text_Contents, (ULONG)"", End;
    b_save = button("Sa_ve", &save_hook);
    b_scan = button("_Scan", &scan_hook);
    b_check = button("_Check online", &check_hook);
    b_update = button("_Install selected", &update_hook);
    b_all = SimpleButton("Update _all");
    if (b_all) DoMethod(b_all, MUIM_Notify, MUIA_Pressed, FALSE, MUIV_Notify_Self, 3, MUIM_CallHook,
                        (ULONG)&update_hook, 1);
    b_remove = button("_Remove selected", &remove_hook);
    b_selall = button("Select all", &selall_hook);
    b_clean = button("Clean old modules", &clean_hook);
    cy_show = CycleObject, MUIA_Cycle_Entries, (ULONG)show_names, MUIA_Cycle_Active, f_show, MUIA_CycleChain, 1, End;
    cy_os = CycleObject, MUIA_Cycle_Entries, (ULONG)os_names, MUIA_Cycle_Active, f_os, MUIA_CycleChain, 1, End;
    b_cancel = button("Cance_l", &cancel_hook);
    panel = VGroup, GroupFrameT("Module"), MUIA_HorizWeight, 35,
        Child, t_name = TextObject, MUIA_Text_PreParse, (ULONG)"\33b", MUIA_Text_SetMin, FALSE, End,
        Child, ColGroup(2),
            Child, Label("Installed"), Child, t_inst = info_text(),
            Child, Label("Available"), Child, t_avail = info_text(),
            Child, Label("Status"), Child, t_status = info_text(),
            Child, Label("Author"), Child, t_author = info_text(),
            Child, Label("Based on"), Child, t_based = info_text(),
            Child, Label("OS"), Child, t_os = info_text(),
            End,
        Child, ListviewObject, MUIA_Listview_Input, FALSE, MUIA_Listview_List,
            (ULONG)(f_desc = FloattextObject, ReadListFrame, MUIA_Floattext_Text, (ULONG)"", End), End,
        End;
    g = VGroup,
        Child, HGroup, GroupFrameT("GitHub repositories (owner/repo)"),
            Child, ColGroup(2),
                Child, Label2("UniPrefs, About, Updates"), Child, s_repo,
                Child, Label2("Modules"), Child, s_mrepo,
                End,
            Child, VGroup, MUIA_HorizWeight, 0, Child, VSpace(0), Child, b_save, Child, VSpace(0), End,
            End,
        Child, HGroup,
            Child, Label1("Show"), Child, cy_show, Child, Label1("OS"), Child, cy_os, Child, b_selall, Child, b_clean, End,
        Child, HGroup, MUIA_VertWeight, 200,
            Child, lv = ListviewObject, MUIA_Listview_List, (ULONG)lst, MUIA_Listview_MultiSelect,
                MUIV_Listview_MultiSelect_Default, MUIA_CycleChain, 1, MUIA_HorizWeight, 65, End,
            Child, panel,
            End,
        Child, HGroup, MUIA_Group_SameSize, TRUE,
            Child, b_scan, Child, b_check, Child, b_update, Child, b_all, Child, b_remove, Child, b_cancel, End,
        Child, t_state,
        Child, listview(logl),
        End;
    if (!g) { t_name = NULL; return NULL; }
    DoMethod(cy_show, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, MUIV_Notify_Self, 2, MUIM_CallHook, (ULONG)&filter_hook);
    DoMethod(cy_os, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, MUIV_Notify_Self, 2, MUIM_CallHook, (ULONG)&filter_hook);
    set(b_selall, MUIA_CycleChain, 1);
    DoMethod(lst, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, MUIV_Notify_Self, 3, MUIM_CallHook,
             (ULONG)&active_hook, MUIV_TriggerValue);     /* the panel follows the active row */
    DoMethod(lv, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, MUIV_Notify_Self, 3, MUIM_CallHook,
             (ULONG)&update_hook, 2);                 /* double click: that module alone */
    set(b_all, MUIA_CycleChain, 1);
    set(b_cancel, MUIA_Disabled, TRUE);
    set(b_scan, MUIA_CycleChain, 1); set(b_check, MUIA_CycleChain, 1);
    set(b_update, MUIA_CycleChain, 1); set(b_remove, MUIA_CycleChain, 1); set(b_cancel, MUIA_CycleChain, 1); set(b_save, MUIA_CycleChain, 1);
    scan();
    show_list();
    logs("Check online reads the latest release of both repositories: UniPrefs, About and Updates come from the");
    logs("first one only, every other module from the second one only.");
    logs("Install selected (or double-click): installs or updates the modules chosen in the list.");
    logs("Update all: every newer one shown. Remove selected: deletes the chosen installed modules.");
    logs("Show and OS filter the list; Select all chooses every row shown.");
    return g;
}

static const char *write_none(Page *p, int archive) { (void)p; (void)archive; return NULL; }

static void ex(void)
{
    while (nents > 0) { free(ents[--nents].info); ents[nents].info = NULL; }
    free(ilines);
    ilines = NULL;
    lst = logl = t_name = b_remove = cy_show = cy_os = b_selall = NULL;
    f_show = f_os = 0;
    nitems = nilines = checked = busy = 0;
    active_target[0] = 0;
}
#endif

static const Module mod = {
    .name = "Updates", .title = "Modules", .version = "0.14", .kind = MK_ACTION,
    .categories = AP_CATEGORY_GENERAL, .mflags = MF_NOICON,
    .page = PAGE(page), .wintitle = "Modules", .exit = EXIT(ex), .write = WRITE(write_none),
};

AP_INFO("Renaud Schweingruber",
        "-",
        "Lists the installed program and modules, installs newer or new ones from the latest release of a GitHub repository, and removes modules.")
AP_MODULE(mod, Updates, "0.14 (28.11.2026)")
