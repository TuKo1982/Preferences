/* host_io - io.h on Linux: "VOL:path" maps to $ROOT/VOL/path; variables are
 * files in $ROOT/ENV (and $ROOT/ENVARC when archived). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io.h"

static char root[512] = ".";
void host_io_root(const char *r) { snprintf(root, sizeof(root), "%s", r); }

static void map(const char *path, char *out, int len)
{
    const char *c = strchr(path, ':');
    if (!c) { snprintf(out, len, "%s/%s", root, path); return; }
    snprintf(out, len, "%s/%.*s/%s", root, (int)(c - path), path, c + 1);
}

int io_load(const char *path, u8 **buf, u32 *len)
{
    char p[1024];
    FILE *f;
    long n;
    map(path, p, sizeof(p));
    if (!(f = fopen(p, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *buf = malloc(n ? n : 1);
    if (!*buf || fread(*buf, 1, n, f) != (size_t)n) { fclose(f); free(*buf); return 0; }
    fclose(f);
    *len = (u32)n;
    return 1;
}

int io_save(const char *path, const u8 *buf, u32 len)
{
    char p[1024];
    FILE *f;
    map(path, p, sizeof(p));
    if (!(f = fopen(p, "wb"))) return 0;
    if (fwrite(buf, 1, len, f) != len) { fclose(f); return 0; }
    return fclose(f) == 0;
}

int io_exists(const char *path)
{
    char p[1024];
    FILE *f;
    map(path, p, sizeof(p));
    if (!(f = fopen(p, "rb"))) return 0;
    fclose(f);
    return 1;
}

int io_getvar(const char *name, char *buf, int len)
{
    char p[600];
    u8 *b;
    u32 n;
    snprintf(p, sizeof(p), "ENV:%s", name);
    if (!io_load(p, &b, &n)) return 0;
    if ((int)n >= len) n = len - 1;
    memcpy(buf, b, n);
    buf[n] = 0;
    free(b);
    return 1;
}

int io_setvar(const char *name, const char *val, int archive)
{
    char p[600];
    snprintf(p, sizeof(p), "ENV:%s", name);
    if (!io_save(p, (const u8 *)val, strlen(val))) return 0;
    if (archive) {
        snprintf(p, sizeof(p), "ENVARC:%s", name);
        if (!io_save(p, (const u8 *)val, strlen(val))) return 0;
    }
    return 1;
}

int io_mode_ok(u32 id) { return id != 0xFFFFFFFFu; }   /* host: only INVALID_ID refused */

void io_protect(const char *path) { (void)path; }
void io_fault(char *buf, int len) { snprintf(buf, len, "object not found"); }
const char *io_text(long id, const char *def) { (void)id; return def; }
