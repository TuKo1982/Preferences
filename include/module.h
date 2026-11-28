/* module - what a preferences module is: a set of named keys stored either
 * in an IFF FORM PREF file (ENV:Sys/x.prefs), in global environment
 * variables, or nowhere (a launcher for an existing editor).
 * Part of the UniPrefs module SDK. Pure C, no AmigaOS calls (host-testable).
 */
#ifndef MODULE_H
#define MODULE_H

#include "prefsfile.h"

enum { MK_IFF, MK_ENV, MK_LAUNCH, MK_ACTION };   /* MK_ACTION: no file, the module acts (Time) */

enum {
    KT_INT,      /* integer, decimal */
    KT_BOOL,     /* Yes/No; IFF: non-zero, or the mask bits set */
    KT_ENUM,     /* one name of names[] (raw value values[i], or i) */
    KT_STR,      /* NUL-terminated text, size = buffer size */
    KT_HEX,      /* integer shown 0x%08X (display IDs) */
    KT_MS,       /* 8-byte timeval (seconds, micros) shown in milliseconds */
    KT_CHAR,     /* one character in one byte */
    KT_STRLIST,  /* count strings of size bytes each, shown comma-separated */
    KT_DEC,      /* integer field shown as a decimal: raw * values[0] / values[1],
                    values[2] decimals (millipoints as cm: { 254, 7200000, 2 }) */
    KT_RGB,      /* colour shown #RRGGBB: size 3 = 8-bit R,G,B; size 6 = 16-bit R,G,B */
    KT_VSTR,     /* IFF: text from off to the end of the chunk, no NUL; the chunk grows or
                    shrinks with it; count = 2: its length is also the UWORD before it;
                    size = longest text */
    KT_BYTES     /* IFF: size bytes shown in hex; size 0 = to the end of the chunk, resized
                    like KT_VSTR (count = 2 likewise) */
};

#define KF_SIGNED 0x01   /* the field is signed */
#define KF_REBOOT 0x02   /* takes effect at the next boot */

struct ModState;
struct Key;

/* Called after a key was set; may refuse (return non-zero, fill err). */
typedef int (*KeyHook)(struct ModState *st, const struct Key *k, char *err, int errlen);

typedef struct Key {
    const char *name;          /* CLI/ARexx name, no spaces */
    const char *label;         /* GUI label */
    unsigned char type;
    unsigned char chunk;       /* IFF: index in Module.chunks */
    unsigned short off;        /* IFF: byte offset in the chunk data */
    unsigned short size;       /* IFF: field bytes (STR: buffer; STRLIST: stride) */
    unsigned char count;       /* STRLIST: number of strings */
    unsigned short flags;
    u32 mask;                  /* IFF: bits of the field, 0 = whole field */
    s32 min, max;              /* INT/HEX range (min > max: none) */
    const char *const *names;  /* ENUM: NULL-terminated */
    const s32 *values;         /* ENUM: raw values, NULL = 0, 1, 2...; DEC: { mul, div, decimals } */
    const char *def;           /* ENV: raw text used when the variable is unset */
    KeyHook hook;
    const char *help;          /* one line, shown by LIST and as GUI help */
} Key;

struct Page;

/* A module: built into a file of its own (Modules/<name>.module), either a
 * binary module with keys and a page, or a text file naming an editor. */
typedef struct Module {
    const char *name;          /* CLI name = file name without .module */
    const char *title;         /* GUI title */
    const char *version;       /* required: the module's own version, "major.minor" ("0.1"); the
                                  $VER string of AP_MODULE() must start with it */
    unsigned char kind;
    const char *file;          /* MK_IFF: file name in ENV:Sys/ and ENVARC:Sys/ */
    const char *editor;        /* path of the original editor (may be NULL) */
    const PChunkSel *chunks;
    unsigned char nchunks;
    const Key *keys;
    unsigned short nkeys;
    void (*init)(PFile *pf);   /* MK_IFF: defaults for a file created from scratch */
    const char *categories;    /* required: the list headings it shows under, comma-separated,
                                  e.g. "Menus & Desktop, AmigaOS" (AP_CATEGORY_* below) */
    /* GUI, all optional (NULL page = a plain page of every key) */
    void *(*page)(struct Page *p);       /* builds the page, returns a MUI Object */
    const char *wintitle;                /* window title while the page shows */
    void (*after)(struct Page *p);       /* once the page shows its values */
    int  (*before_write)(struct Page *p);/* Save/Use: 0 cancels the write */
    void (*exit)(void);                  /* after the window is gone */
    /* MK_ACTION only (read for that kind alone, so older modules need not have it):
     * Save (archive 1) and Use (0) call it; it does nothing when the user changed
     * nothing; 0 = done, else a message for the user */
    const char *(*write)(struct Page *p, int archive);    /* "" = the module told the user itself: no quit */
    /* MK_IFF: how its editor reads and writes the file, when it differs from the common prefs editor
     * code (0 = that code: ENVARC then ENV, e cleared on ENVARC/Save As, Restore = the start-up state) */
    unsigned short mflags;
    const u32 *keep_ids;       /* chunk IDs written beside chunks[] (every instance, in this order), 0 ends */
    const char *catalog;       /* catalog of cat_load/cat_save (NULL: Sys/prefs.catalog) */
    short cat_load, cat_save;  /* titles of its Open/Save As file requesters */
    const char *txt_load, *txt_save;   /* their built-in English texts */
    const char *icon_tool;     /* default tool of Create Icons icons (NULL: editor) */
    /* MF_AFTERLOAD only: what the editor does to the values after every file it reads (page and CLI) */
    void (*after_load)(PFile *pf);
    /* MF_EXT only (AP_HOST_VERSION 6): every field below exists; NULL = none */
    unsigned long xflags;      /* XF_* below */
    /* MK_IFF: called by Save/Use just before each file is written (arc 1 = ENVARC:, 0 = ENV:): the module may
     * change st->pf for that file alone (an editor that writes the two files differently); 0 = out of memory */
    int (*before_file)(struct ModState *st, int arc);
    /* MK_IFF: called for each chunk of the file whose ID is one of chunks[], in file order, before the host takes
     * it: 0 = take it as usual, 1 = skip it (the values stay), -1 = the file is an IFF error (nothing changes);
     * called with id 0 and no data before the first chunk of each read (to reset what the module collects) */
    int (*check_chunk)(u32 id, const u8 *data, u32 size);
    /* MK_ACTION: the CLI of the editor: FROM (NULL if not given), save = 0 USE, 1 SAVE, 2 neither (FROM alone);
     * 0 = done, -1 = the editor opens its window (UniPrefs opens the page), else the return code; msg says why */
    int (*cli)(const char *from, int save, char *msg, int msglen);
} Module;

/* Module.mflags */
#define MF_ENVFIRST    0x0001  /* Save writes ENV, then ENVARC */
#define MF_NOSTOP      0x0002  /* Save writes the second file even when the first failed */
#define MF_NOPROTECT   0x0004  /* does not clear e on the files it saves */
#define MF_RESTORE_ENV 0x0008  /* Restore reads ENV again (else the state at start-up) */
#define MF_MISS_DEF    0x0010  /* Last Saved/Restore without the file: the defaults, silently */
#define MF_ICON_PREF   0x0020  /* Create Icons: GetIconTags default "PREF", NotifyWorkbench */
#define MF_NOICON      0x0040  /* Save As never makes an icon */
#define MF_ASL_PLAIN   0x0080  /* file requester: title and drawer only (no patterns, icons shown) */
#define MF_SIZE_ERROR  0x0100  /* a chunk of another size is an IFF error (else it is ignored) */
#define MF_VARCHUNKS   0x0200  /* its chunks have no fixed size */
#define MF_PROTECT_ENV 0x0400  /* clears e on the ENV: file too (the 3.0/3.1 editors protect every file they write) */
#define MF_PRHD_ANY    0x0800  /* accepts a PRHD of any version (else only version 0) */
#define MF_AFTERLOAD   0x1000  /* the descriptor has after_load (read only with this flag: older modules lack it) */
#define MF_KEEP_RESET  0x2000  /* a file that has chunks[0] also replaces the keep_ids chunks, even by none (the
                                  editor empties its list when it reads that chunk) */
#define MF_EXT         0x4000  /* the descriptor has xflags and the hooks after it (AP_HOST_VERSION 6) */

/* Module.xflags (MF_EXT) */
#define XF_UNITS       0x0001  /* the editor takes UNIT=n: for n > 0 its files are <file name><n>.prefs */
#define XF_PRHD_STOP   0x0002  /* a PRHD shorter than 6 bytes ends the read: the chunks before it are kept, the
                                  rest is not read; with none before, the file is an IFF error (else: an error) */
#define XF_PRHD_SHORT  0x0004  /* a PRHD shorter than 6 bytes is not looked at (the read goes on) */
#define XF_NEED_PRHD   0x0008  /* a FORM PREF without PRHD is an IFF error */
#define XF_PRHD_OR_CHUNK 0x0010 /* a FORM PREF without PRHD is an IFF error unless the last chunk read was taken */
#define XF_OS3X_READER (XF_PRHD_STOP | XF_PRHD_OR_CHUNK)  /* a shared editor reader: a short PRHD ends the read,
                                  an empty FORM PREF is error 2, the last chunk wins */
#define XF_CHUNK_FIRST 0x0020  /* of two chunks of one kind the first is taken (else the last, as an IFF loop
                                  that reads every chunk into the settings) */
#define XF_ZERO_SKIP   0x0040  /* a chunk of 0 bytes is not read (ReadChunkBytes gives 0, the handler's result 0)
                                  even with MF_SIZE_ERROR */
#define XF_NEED_CHUNK  0x0080  /* a file without one chunk of chunks[] taken is an IFF error */
#define XF_PRHD_FIRST  0x0100  /* the first chunk must be a PRHD, else an IFF error */
#define XF_KEEP_EDITOR 0x0400  /* the page leaves part of the editor's work to it: UniPrefs never replaces the editor
                                  (Replace Original Editor...) */
#define XF_ENVARC_FALLBACK 0x0200 /* at start, when ENV:Sys/<file> is missing or refused, ENVARC:Sys/<file> is read
                                  (the ApolloOS/AROS prefs editors: prefseditor.c:71-80); neither = defaults */
#define XF_F35_READER  XF_ZERO_SKIP  /* another shared editor reader: a PRHD of 0-5 bytes or of another version is
                                  error -8, a short chunk is an error (MF_SIZE_ERROR), a 0-byte one is not read,
                                  the last chunk wins */

/* The usual categories, listed in this order; any other name gets a heading
 * of its own after them (alphabetical). A module may name up to
 * AP_MAXCATS categories of at most AP_CATLEN - 1 characters each. */
/* the editors of an operating system itself, one heading per OS and version (user 24.11); their modules are
 * named <editor>_<suffix>, the suffix of the OS ('_', not '-': Updates 0.4 refuses a '-' in a module name) */
#define AP_CATEGORY_OS30      "AmigaOS 3.0"
#define AP_CATEGORY_OS31      "AmigaOS 3.1"
#define AP_CATEGORY_OS314     "AmigaOS 3.1.4"
#define AP_CATEGORY_OS323     "AmigaOS 3.2.3"
#define AP_CATEGORY_OS33      "AmigaOS 3.3"      /* announced, not released (24.11.2026) */
#define AP_CATEGORY_OS35      "AmigaOS 3.5"
#define AP_CATEGORY_OS39      "AmigaOS 3.9"
#define AP_CATEGORY_APOLLOOS  "ApolloOS"
#define AP_CATEGORY_AMIGAOS   AP_CATEGORY_OS39  /* before 24.11: "AmigaOS" */
#define AP_CATEGORY_SYSTEM    "System"
#define AP_CATEGORY_DISPLAY   "Display"
#define AP_CATEGORY_LOOK      "Look & Feel"
#define AP_CATEGORY_INPUT     "Menus & Desktop"
#define AP_CATEGORY_REQUESTERS "Requesters"
#define AP_CATEGORY_LOCALE    "Locale and time"
#define AP_CATEGORY_SOUND     "Sound"
#define AP_CATEGORY_PRINTING  "Printing"
#define AP_CATEGORY_NETWORK   "Network"
#define AP_CATEGORY_DATATYPES "Datatypes"
#define AP_CATEGORY_GENERAL   "General"          /* the program's own pages (About, Modules): always listed first */
#define AP_CATEGORY_PREFERENCES  AP_CATEGORY_GENERAL  /* before 25.11: "Preferences", listed last */
/* where the program looks for its modules, and the program's file name (Updates) */
#define AP_MODDIR   "PROGDIR:Modules"
/* Replace Original Editor... keeps the editors it replaces there; the modules still read their data (orig_data) */
#define AP_ORIGDIR  "PROGDIR:Modules/Originals"
#define AP_PROGNAME "UniPrefs"
/* the program's target in the release manifests: its name before 0.21, kept so that every Updates reads them */
#define AP_PROG_TARGET "Preferences"
/* Updates (user 28.11.2026): two GitHub repositories ("owner/repo"), each set by an ENV:/ENVARC: variable, the
 * default used when it is not set. The core repository gives the program and its own modules About and Updates,
 * nothing else; the modules repository gives every other module, never those three. */
#define AP_UPDATE_REPO      "TuKo1982/UniPrefs"            /* core */
#define AP_UPDATE_REPO_VAR  "UniPrefs/UpdateRepo"
#define AP_UPDATE_BASE_VAR  "UniPrefs/UpdateBase"          /* ENV: only, tests: replaces the core base URL */
#define AP_MODULES_REPO     ""   /* modules: no default (user 28.11.2026), the user enters it in the Modules page */
#define AP_MODULES_REPO_VAR "UniPrefs/ModulesRepo"
#define AP_MODULES_BASE_VAR "UniPrefs/ModulesBase"         /* ENV: only, tests: replaces the modules base URL */
#define AP_ENVLEN  256      /* MK_ENV: longest variable text + 1 */
#define AP_MAXCATS 4
#define AP_CATLEN  32

typedef struct ModState {
    const Module *m;
    PFile pf;                  /* MK_IFF */
    char **env;                /* MK_ENV: raw text per key (NULL = unset) */
    unsigned char *envDirty;   /* MK_ENV: per key */
    int changed;               /* some key was set since load */
    int fresh;                 /* MK_IFF: no file existed, chunks start zero-filled */
    char note[200];            /* a problem with the file read at start-up, for the user */
    const char *file;          /* MK_IFF: the file name in ENV:Sys/ and ENVARC:Sys/ (NULL: the module's file;
                                  units 1-9: <name>N.prefs) */
} ModState;

/* a check_chunk for a reader that reads a fixed header first: fewer than min bytes are an IFF error (the
 * ReadChunkBytes count is returned), zero = zero_r (1 skip, -1 error, 0 take) */
#define AP_CHUNK_MIN(fn, min, zero_r) \
    static int fn(u32 id, const u8 *d, u32 n) { (void)id; (void)d; return n == 0 ? (zero_r) : n < (min) ? -1 : 0; }

/* the module's extension fields, or NULL when its descriptor has none */
#define MOD_EXT(m) (((m)->mflags & MF_EXT) ? (m) : (const Module *)0)

/* Checks a module descriptor (reg_core.c): 0 = fine, else err says what is
 * wrong. UniPrefs refuses a module that fails it. */
int mod_check(const Module *m, char *err, int errlen);
/* i-th category of m into buf (0..), 0 = no such category */
int mod_category(const Module *m, int i, char *buf, int len);

/* keys.c */
const Key *key_find(const Module *m, const char *name);
int  key_get(ModState *st, const Key *k, char *buf, int len);              /* 0 = ok */
int  key_set(ModState *st, const Key *k, const char *text, char *err, int errlen); /* 0 = ok */
void key_describe(const Key *k, char *buf, int len);                       /* type and range */
int  key_raw_get(ModState *st, const Key *k, s32 *v);                      /* numeric kinds */
int  key_raw_set(ModState *st, const Key *k, s32 v);
u8  *key_field(ModState *st, const Key *k, u32 need);                     /* IFF field or NULL */

int  str_ieq(const char *a, const char *b);

#endif
