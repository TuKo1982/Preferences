/* apmodule - the interface between UniPrefs and a binary module.
 *
 * A binary module is an AmigaDOS load file (Modules/<name>.module). Its
 * first code hunk starts with
 *     moveq #-1,d0 ; rts           (run by mistake: returns at once)
 *     dc.l  AP_MAGIC
 *     dc.l  AP_ABI
 *     dc.l  ap_init                (const Module *ap_init(APHost *))
 * UniPrefs LoadSeg()s the file, checks the two longs and calls ap_init with
 * the table below. The module keeps the table and reaches UniPrefs only
 * through it: library bases, C memory and formatting, keys, GUI kit.
 *
 * Part of the UniPrefs module SDK (sdk/README.md).
 *
 * Compatibility: AP_ABI changes when a field changes meaning or the shared
 * structures (Module, Key, ModState, Page, Bind) change layout; UniPrefs
 * loads only modules of its own AP_ABI. New fields go at the end of APHost
 * and raise AP_HOST_VERSION; a module needing them checks host->version.
 */
#ifndef APMODULE_H
#define APMODULE_H

#include <stdarg.h>
#include <stddef.h>
#include "module.h"

#include "apabi.h"

#ifndef AP_NO_GUI
#include <exec/types.h>
#include <intuition/classusr.h>
#include "gui_int.h"

typedef struct APHost {
    ULONG version;                     /* AP_HOST_VERSION */
    ULONG size;                        /* sizeof(APHost) */

    /* libraries opened by UniPrefs */
    struct ExecBase *SysBase;
    struct DosLibrary *DOSBase;
    struct IntuitionBase *IntuitionBase;
    struct GfxBase *GfxBase;
    struct Library *UtilityBase;
    struct Library *MUIMasterBase;     /* NULL until the window opens (not from the CLI) */

    /* C memory and text: one allocator for UniPrefs and its modules */
    void *(*malloc)(size_t n);
    void  (*free)(void *p);
    void *(*realloc)(void *p, size_t n);
    int   (*vsnprintf)(char *buf, size_t n, const char *fmt, va_list ap);

    /* keys and files (module.h, prefsfile.h, io.h) */
    const Key *(*key_find)(const Module *m, const char *name);
    int  (*key_get)(ModState *st, const Key *k, char *buf, int len);
    int  (*key_set)(ModState *st, const Key *k, const char *text, char *err, int errlen);
    int  (*key_raw_get)(ModState *st, const Key *k, s32 *v);
    int  (*key_raw_set)(ModState *st, const Key *k, s32 v);
    u8  *(*key_field)(ModState *st, const Key *k, u32 need);
    int  (*str_ieq)(const char *a, const char *b);
    void (*pf_init)(PFile *pf);
    void (*pf_free)(PFile *pf);
    int  (*pf_take)(PFile *pf, u8 *buf, u32 len);
    u8  *(*pf_find)(PFile *pf, const PChunkSel *sel, u32 *size);
    u8  *(*pf_need)(PFile *pf, const PChunkSel *sel, u32 *size);
    int  (*io_load)(const char *path, u8 **buf, u32 *len);
    int  (*io_exists)(const char *path);
    int  (*io_mode_ok)(u32 displayid);

    /* GUI (gui_int.h, art.h); valid while the window exists */
    Object **app, **win;
    Bind  *(*bind)(Page *p, const char *key, Object *o, int ad, Object *aux);
    void   (*bind_refresh)(Bind *b);
    void   (*bind_text)(Bind *b, char *buf, int len);
    Object *(*run_button)(const char *label, const char *cmd);
    Object *(*check_row)(Page *p, const char *key, const char *label);
    Object *(*str_list)(void);
    Object *(*listview)(Object *l);
    Object *(*slider)(LONG min, LONG max, int quiet);
    Object *(*text_fixed)(const char *widest);
    Object *(*int_string)(int maxlen);
    Object *(*stepper)(Object *str);
    void    (*fill_dir)(Object *l, const char *dir, const char *suffix, int capitalise);
    void    (*notify_refresh)(Bind *b, ULONG attr);
    void    (*time_row)(Page *p, Object *cols, const char *key, const char *label, int reverse);
    Object *(*cycle)(Page *p, const char *key, const char *const *labels);
    Object *(*radio)(Page *p, const char *key, const char *title, const char *const *labels,
                     const char *const *vals, int horiz);
    ULONG   (*mode_nominal)(ULONG id, int height);
    Object *(*art_new)(ULONG type, ULONG tag1, ...);
    Object *(*flaglist_new)(const char *dir);
    Object *(*flaglist_new2)(const char *dir);
    int     (*zones_count)(void);
    LONG    (*zones_east)(int i);
    /* version 2 */
    const char *(*value_get)(Bind *b);
    void    (*value_set)(Bind *b, const char *text);
    Object *(*pixels_new)(ULONG tag1, ...);
    u8     *(*pf_resize)(PFile *pf, const PChunkSel *sel, u32 size);
    Object *(*pixels_tools)(void);
    int     (*page_test)(Page *p);
    u8     *(*orig_data)(const char *path, int hunk, u32 off, u32 len, u32 crc);
    const char *(*cat_str)(const char *catalog, LONG id, const char *def);
    const char *(*cat_menu)(const char *catalog, LONG id, const char *def, char *key);
    void    (*cat_list)(const char *catalog, const char **texts, const LONG *ids);
    /* AP_HOST_VERSION 3 */
    int     (*st_save)(ModState *st, int archive, char *err, int errlen);   /* Save (1) / Use (0) of a state */
    void    (*page_reload)(Page *p, ModState *n, int changed);   /* the page takes n (its file kept if n has none) */
    int     (*page_collect)(Page *p);    /* the page's gadgets into its state: 0 = fine, else the error shown */
    /* AP_HOST_VERSION 4 (read-only) */
    const char *(*version_string)(void); /* the program's "$VER: <name> <version> (<date>)" */
    const Module *const *(*module_list)(void);  /* every loaded module, list order, NULL-terminated */
    /* AP_HOST_VERSION 6 */
    const char *(*start_arg)(const char *name); /* UniPrefs' start argument USER or UNIT (CLI or tool
                                                   type), as the editors take it; NULL when not given */
} APHost;

/* the module reads the bases through its pointer at each call: UniPrefs
 * opens Intuition and MUI only for the window, after loading the modules */
typedef const Module *(*APInit)(APHost *host);

extern APHost ap_host;                 /* hostapi.c */
#endif

#endif
