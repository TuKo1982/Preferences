/* gui_int - shared by gui.c (window, bindings) and gui_pages.c (one
 * builder per module, laid out like the original editor). */
#ifndef GUI_INT_H
#define GUI_INT_H

#include <exec/types.h>
#include <libraries/mui.h>
#include "module.h"

#define MAXBINDS 40

/* How a gadget shows a key. Every adapter turns the gadget into the same
 * text as the CLI (key_get/key_set), so all checks stay in keys.c. */
enum {
    AD_CHECK,      /* checkmark: Yes/No */
    AD_CYCLE,      /* cycle: k->names[active] (GUI labels may differ, same order) */
    AD_RADIO,      /* radio: same */
    AD_SLIDER,     /* slider: number */
    AD_STRING,     /* string (or the string of a popstring): text */
    AD_TIME,       /* slider in 10 ms steps, aux = Text "1.50 s" */
    AD_LIST,       /* list of names: the active entry */
    AD_FONT,       /* popasl string "name/size": name key, size key is the next key */
    AD_DIM,        /* integer string + aux "Default" checkmark: -1 when checked */
    AD_DEPTH,      /* slider of depth, aux = Text "256" / "16 M" */
    AD_MODEID,     /* list of display modes */
    AD_LANGS,      /* aux = available list, o = preferred list: comma-separated */
    AD_TZ,         /* aux = Text "Time Zone: ...", aux2 = map, value in val, arrows change it */
    AD_TOGGLES,    /* tog[] = image buttons, one selected: k->names[i] */
    AD_BEEP,       /* slider, aux = Text "64.0 ms" (6.4 ms a step) */
    AD_VALUE       /* no gadget (o may be NULL): the key text lives in txt; the page shows it
                      and writes it back itself (gadgets shared by several keys) */
};

typedef struct Bind {
    const Key *k;
    Object *o, *aux;
    int ad;
    char *init;        /* text right after loading */
    LONG val;          /* AD_TZ: current minutes west */
    ULONG *ids;        /* AD_MODEID: display IDs in list order */
    int nids;
    char txt[48];      /* text shown in aux (AD_TIME, AD_DEPTH, AD_TZ) */
    const char *const *vals; /* AD_CYCLE/AD_RADIO: key text per entry (NULL = k->names) */
    Object *aux2;      /* AD_TZ: the world map */
    Object *tog[4];    /* AD_TOGGLES */
    int ntog;
    char *value;       /* AD_VALUE: the key text (value_get, value_set) */
} Bind;

#define AP_PAGEMENU 8
typedef struct Page {
    const Module *m;
    ModState st;
    int loaded;
    Bind b[MAXBINDS];
    int nb;
    char entry[80];
    const char *wintitle;
    /* UniPrefs' own: the ENV: file as it was before the first Test */
    int tested, env_had;
    u8 *env_old;
    u32 env_oldlen;
    int nosave;          /* set by the module: its page cannot Save (Save ghosted while it shows) */
    /* set by the module in page(): its own Project menu entries while the page shows
     * (after Save As...), as its original editor has them; label NULL = unused */
    struct { const char *label, *key; struct Hook *hook; } menu[AP_PAGEMENU];
    const char *apply;   /* unused since the uniform Save/Use/Reset row (Use keeps the window) */
    /* MK_ACTION: the host's Open..., Save As..., Reset To Defaults, Last Saved, Restore done by the page
     * (enabled only when set); Open/Save As get the chosen path in args[0] and return 0 or set p->err */
    struct Hook *fileop[5];
    int icons;           /* set before the Save As hook: Create Icons? is checked; the hook returns 2
                            when it made the icon itself (as its editor does), 0 to let UniPrefs do it */
    /* UniPrefs' own */
    ModState start;      /* the state read at start-up: Restore, Cancel after a Test */
    int start_ok, noted;
    void *fr;            /* its file requester, kept for the session as its editor keeps it */
    void (*closing)(struct Page *p);   /* set by the module: called before the window and application go */
    int saving;          /* for before_write: 1 = Save, 0 = Use */
    int shown;           /* for write: 1 = the page shown (its editor would write, changed or not) */
    /* set by the module in page(): a sub-menu of the Settings menu while the page shows (title NULL =
     * none), its items mutually exclusive checkmarks; the hook gets the item index in args[0]. item[0] NULL: the
     * title is one checkmark item that toggles, item[1] its shortcut (or NULL), the hook gets 1 or 0 */
    struct { const char *title; const char *item[4]; int checked; struct Hook *hook; } setmenu;
    /* set by the module in page(): its own Edit menu items while the page shows, before the standard three
     * (a separator after them) or after them (edit_after: a separator before); label NM_BARLABEL = a separator; sub[] = a sub-menu (its hook gets the index in
     * args[0]); the host puts the item objects in edit_obj[] (to ghost them) */
    struct { const char *label, *key; struct Hook *hook; const char *sub[12], *subkey[12]; } edit[12];
    int edit_after;
    Object *edit_obj[12];
    /* set by the module in page(): PB_NOWRITE = the page acts at once, its Save/Use/Reset are ghosted */
    int buttons;
    /* MK_ACTION, set by the module: 1 = changes not written yet (asked about when the window closes) */
    int (*pending)(struct Page *p);
    /* AP_HOST_VERSION 5, set by the module: called each time the page is shown (chosen in the list, or the first
     * page at start); e.g. a query its editor makes at its own start, done when the page is first seen */
    void (*on_show)(struct Page *p);
} Page;

enum { PB_NORMAL = 0, PB_NOWRITE, PB_SAVEONLY };

void page_replace(Page *p, ModState *n, int changed);   /* host: the page takes another state */
int page_collect(Page *p);                               /* host: gadgets -> state */   /* PB_SAVEONLY: the editor only saves (Use ghosted) */

/* gui.c */
extern Object *app, *win;
Bind *bind(Page *p, const char *key, Object *o, int ad, Object *aux);
Object *run_button(const char *label, const char *cmd);   /* starts cmd when pressed */
Object *check_row(Page *p, const char *key, const char *label);  /* [x] label, like the system editors */
void bind_refresh(Bind *b);    /* update a bind's helper texts after a change */
void bind_text(Bind *b, char *buf, int len);   /* what the gadget shows, as key text */

/* kit.c: pieces the module pages are built from (also in APHost) */
Object *str_list(void);                         /* list of strings */
Object *listview(Object *l);
Object *slider(LONG min, LONG max, int quiet);
void kit_cleanup(void);
Object *text_fixed(const char *widest);         /* right-aligned, as wide as widest */
Object *int_string(int maxlen);
Object *stepper(Object *str);                   /* integer string with </> arrows */
void fill_dir(Object *l, const char *dir, const char *suffix, int capitalise);
/* bytes off..off+len of hunk `hunk` of an original editor's load file, if
 * their CRC-32 is crc (malloc'd, or NULL): its images are read at run time,
 * never copied into a module */
u8 *orig_data(const char *path, int hunk, u32 off, u32 len, u32 crc);
void orig_saved_name(const char *path, char *out, int len);   /* where Replace Original Editor... keeps an editor */
/* catalogs, as the editors use them: the text of number id of the catalog
 * (e.g. "Sys/prefs.catalog" and the editor's own numbers), def when none */
const char *cat_str(const char *catalog, LONG id, const char *def);
const char *cat_menu(const char *catalog, LONG id, const char *def, char *key);   /* "O\0Open..." */
void cat_list(const char *catalog, const char **texts, const LONG *ids);         /* in place, 0 ends */
void cat_close(void);
void notify_refresh(Bind *b, ULONG attr);       /* bind_refresh when attr changes */
void time_row(Page *p, Object *cols, const char *key, const char *label, int reverse);
Object *cycle(Page *p, const char *key, const char *const *labels);
Object *radio(Page *p, const char *key, const char *title, const char *const *labels,
              const char *const *vals, int horiz);
ULONG mode_nominal(ULONG id, int height);      /* nominal width/height of a mode, 0 = unknown */

/* gui.c: AD_VALUE binds (no gadget): the page shows value_get() itself and
 * stores what the user chose with value_set(); Save/Use write it */
const char *value_get(Bind *b);
void value_set(Bind *b, const char *text);
/* the editors' Test button: writes the page to ENV: now; Cancel puts the
 * old ENV: file back (IFF modules). 1 = written */
int page_test(Page *p);

/* pixels.c: a bitplane image editor (plane after plane, rows of
 * (width + 15) / 16 words, like pointer.prefs and wbpattern.prefs) */
#define PXA_Width       (TAG_USER | 0x41500020)   /* I   pixels, at most 64 */
#define PXA_Height      (TAG_USER | 0x41500021)   /* I   at most 64 */
#define PXA_Depth       (TAG_USER | 0x41500022)   /* I   planes, 1-3 */
#define PXA_Data        (TAG_USER | 0x41500023)   /* ISG the planes in hex (KT_BYTES text) */
#define PXA_Pen         (TAG_USER | 0x41500024)   /* ISG colour the mouse draws with */
#define PXA_Colors      (TAG_USER | 0x41500025)   /* IS  ULONG r,g,b (32-bit) per colour; NULL = screen pens */
#define PXA_Transparent (TAG_USER | 0x41500026)   /* I   colour 0 shows the background */
#define PXA_Preview     (TAG_USER | 0x41500027)   /* I   no editing, 1:1: 1 = repeated, 2 = on 4 backgrounds; 3 = colour strip (click: PXA_Pen) */
#define PXA_HotX        (TAG_USER | 0x41500028)   /* ISG hot spot, marked (-1 = none) */
#define PXA_HotY        (TAG_USER | 0x41500029)   /* ISG */
#define PXA_SetPoint    (TAG_USER | 0x4150002A)   /* IS  TRUE: the next click sets the hot spot */
#define PXA_Changed     (TAG_USER | 0x4150002B)   /* SG  set TRUE after each change by the user (notify it) */
#define PXA_Tool        (TAG_USER | 0x4150002C)   /* ISG drawing tool, PXT_* */
#define PXA_Undo        (TAG_USER | 0x4150002D)   /* S   TRUE: undo the last stroke */
#define PXA_Redo        (TAG_USER | 0x4150002E)   /* S   TRUE: redo it */
#define PXA_Clear       (TAG_USER | 0x4150002F)   /* S   TRUE: every pixel to colour 0 (can be undone) */
#define PXA_Target      (TAG_USER | 0x41500031)   /* S   tool bar: the editor its buttons act on */
#define PXA_Cell        (TAG_USER | 0x41500032)   /* I   editor: fixed cell size in pixels (0 = follows the space) */
#define PXA_Boxes       (TAG_USER | 0x41500033)   /* I   colour strip: this many boxes (0 = 1 << depth) */
/* drawing tools */
enum { PXT_DOTS, PXT_FREE, PXT_RECT, PXT_FRECT, PXT_OVAL, PXT_FOVAL, PXT_LINE, PXT_FILL,
       PXT_UNDO, PXT_REDO, PXT_CLEAR, PXT_N };
int  pixels_init(void);
void pixels_exit(void);
Object *pixels_new(ULONG tag1, ...);
Object *pixels_tools(void);     /* the tool bar: 2 rows of buttons, then set(bar, PXA_Target, editor) */

#endif
