/* Template - a UniPrefs binary module: keys of an IFF preferences file
 * (ENV:Sys/template.prefs), a CLI and a page of its own.
 *
 * Copy to <Name>.c, rename the module (search "Template"), describe the
 * fields, then: make (Name.module), make check (on the build machine).
 * Delete the page to get UniPrefs' plain page (one gadget per key).
 */
#include "kit.h"                    /* first and only include */

/* ---- the file ------------------------------------------------------------
 * One PChunkSel per chunk: ID, data size to create when missing, and an
 * optional selector (byte offset, size 1/2/4, value) when the file has
 * several chunks with the same ID. */
enum { CH_TMPL };
static const PChunkSel chunks[] = {
    [CH_TMPL] = { PF_ID('T','M','P','L'), 16, 0, 0, 0 },
};

static const char *const mode_names[] = { "Off", "Slow", "Fast", NULL };

/* ---- the keys ------------------------------------------------------------
 * name: for GET/SET (no spaces); label: plain page and LIST; the field's
 * byte offset and size in the chunk data; min > max (NOR) = no range. */
static const Key keys[] = {
    { .name = "Enabled", .label = "Enabled", .type = KT_BOOL, .chunk = CH_TMPL, .off = 0, .size = 1,
      .min = NOR, .help = "Switches the feature on" },
    { .name = "Mode", .label = "Mode", .type = KT_ENUM, .chunk = CH_TMPL, .off = 1, .size = 1,
      .names = mode_names, .min = NOR },
    { .name = "Delay", .label = "Delay (ms)", .type = KT_INT, .chunk = CH_TMPL, .off = 2, .size = 2,
      .min = 0, .max = 5000 },
    { .name = "Name", .label = "Name", .type = KT_STR, .chunk = CH_TMPL, .off = 4, .size = 12,
      .min = NOR },
};

/* defaults of a file created from scratch (fields not set here are 0) */
static void init(PFile *pf)
{
    u32 size;
    u8 *d = pf_need(pf, &chunks[CH_TMPL], &size);
    if (d) d[1] = 1;                /* Mode=Slow */
}

#ifndef AP_NO_GUI                   /* the page: not built for make check */

static const char *const mode_labels[] = { "Off", "Slow", "Fast", NULL };

static Object *page(Page *p)
{
    Object *delay = slider(0, 5000, 0);
    Object *name = StringObject, StringFrame, MUIA_String_MaxLen, 12, MUIA_CycleChain, 1, End;
    bind(p, "Delay", delay, AD_SLIDER, NULL);
    bind(p, "Name", name, AD_STRING, NULL);
    return VGroup,
        Child, HGroup, Child, check_row(p, "Enabled", "Enabled"), Child, HSpace(0), End,
        Child, ColGroup(2), GroupFrameT("Settings"),
            Child, Label1("Mode"), Child, cycle(p, "Mode", mode_labels),
            Child, Label1("Delay"), Child, delay,
            Child, Label2("Name"), Child, name,
            End,
        Child, VSpace(0),
        End;
}

#endif

static const Module mod = {
    .name = "Template", .title = "Template", .version = "0.1", .kind = MK_IFF, .file = "template.prefs",
    .editor = NULL,                                 /* or the original editor's path */
    .chunks = chunks, .nchunks = SIZEOF(chunks),
    .keys = keys, .nkeys = SIZEOF(keys), .init = init,
    .categories = AP_CATEGORY_LOOK,                 /* "Look, Sound", or your own heading */
    .page = PAGE(page), .wintitle = "Template Preferences",
};

/* what the Updates page shows about the module (read from the file, like $VER): author, the original
 * software it is based on ("-" for none) and one or two sentences of what it sets. Plain ASCII, no tabs. */
AP_INFO("Your Name", "-", "What the Template page sets (ENV:Sys/template.prefs).")
AP_MODULE(mod, Template, "0.1 (23.11.2026)")
