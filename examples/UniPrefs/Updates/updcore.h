/* updcore - the pure part of the Updates module (modules/Updates/Updates.c): manifest, versions, SHA-256, URLs.
 * No AmigaOS calls: tests/test_core.c tests it on the PC. docs/updates-spec-2026-11-24.md. */
#ifndef UPDCORE_H
#define UPDCORE_H

#include "prefsfile.h"          /* u8, u32 */

#define UPD_MAXITEMS 400       /* 160 before Updates 0.8 */
/* manifest.txt and info.txt stay within what Updates 0.7 and older read (they refuse a manifest of more than
 * 160 files); manifest-all.txt and info-all.txt (Updates 0.8) list every file */
#define UPD_LEGACY_MAX 160
#define UPD_MAXSIZE  16777216UL         /* largest asset a manifest may announce */
#define UPD_HEADER   "Preferences-Manifest 1"
#define UPD_INFO_HEADER "Preferences-Info 1"
#define UPD_INFO_MAX 262144UL           /* largest info.txt read */

/* the program's own info (its file has no $APINFO; make_release.py writes the same line) */
#define UPD_PROG_AUTHOR "Renaud Schweingruber"
#define UPD_PROG_BASEDON "-"
#define UPD_PROG_DESC "The unified preferences program: one window for every preferences page, each page a module file."

typedef struct UpdItem {
    char target[48];                    /* AP_PROG_TARGET or "Modules/<Name>.module" */
    char version[24];
    u32 size;
    char sha[65];                       /* lower-case hex */
    char asset[104];
} UpdItem;

/* what the panel shows about a file: from its "$APINFO:" (binary) or Author=/BasedOn=/Description= (text) */
typedef struct UpdInfo {
    char author[64];
    char based[128];
    char desc[256];
    char os[48];                        /* the OS whose editor it reproduces ("" = none), AP_INFO_OS / OS= */
} UpdInfo;

typedef struct UpdInfoLine { char target[48]; UpdInfo info; } UpdInfoLine;     /* one line of info.txt */

typedef struct UpdSha { u32 h[8], lo, hi, n; u8 buf[64]; } UpdSha;

int upd_version_ok(const char *v);
int upd_vercmp(const char *a, const char *b);                    /* -1, 0, 1 */
int upd_target_ok(const char *t);
int upd_target_path(const char *t, char *path, int len);
/* "#obsolete <target> [<new target>]" lines; rep[i] "" = removed, else its new name */
int upd_parse_obsolete(const char *text, u32 len, char (*out)[48], char (*rep)[48], int max);
int upd_parse_manifest(const char *text, u32 len, UpdItem *items, int max, int *n, char *err, int errlen);
int upd_file_version(const u8 *b, u32 len, const char *target, char *out, int outlen);
int upd_file_info(const u8 *b, u32 len, UpdInfo *out);          /* 1 = found */
int upd_parse_info(const char *text, u32 len, UpdInfoLine *lines, int max, int *n, int *skipped, char *err, int errlen);
const UpdInfoLine *upd_info_find(const UpdInfoLine *lines, int n, const char *target);
const char *upd_info_field(const UpdInfo *inst, const UpdInfo *listed, int field);   /* 0 author, 1 based, 2 desc, 3 os */
const char *upd_status(int found, const char *inst, int listed, const char *avail);
int upd_repo_ok(const char *r);
/* 1 = a file of the core repository (the program, About, Updates), 0 = of the modules repository */
int upd_core_target(const char *t);
/* 1 = a file name Clean old modules deletes: "<name>.module.old" / ".new" in the modules drawer (where = 0), or
 * the program's "UniPrefs.old" / ".new" (or "Preferences.old" / ".new", its name before 0.21) in PROGDIR: (1) */
int upd_old_name(const char *name, int where);
int upd_split_url(const char *url, int *tls, char *host, int hostlen, int *port, char *path, int pathlen);
void upd_sha_init(UpdSha *s);
void upd_sha_update(UpdSha *s, const u8 *p, u32 len);
void upd_sha_final(UpdSha *s, char *hex);                        /* 64 hex digits + NUL */
void upd_sha256(const u8 *p, u32 len, char *hex);

#endif
