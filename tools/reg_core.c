/* reg_core - the list of loaded modules (pure C, host-testable).
 *
 * Modules come from files: amiga_reg.c loads Modules/<name>.module (binary
 * modules through LoadSeg, text modules through reg_parse_launcher); the host
 * tests add the binary modules' descriptors directly. Removing a file removes
 * the module; nothing here knows any module by name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"

#define MAXMODULES 320
#define MAXCATEGORIES 48

typedef struct Entry {
    const Module *m;
    void *owner;
    void (*release)(void *owner);
} Entry;

static Entry entries[MAXMODULES];
static int nentries;
static const Module *sorted[MAXMODULES + 1];
const Module **all_modules = sorted;
/* sorted[i]'s sort key, made once when it is added: the rank and the name of its first category */
static struct { int rank; char cat[AP_CATLEN]; } sorted_key[MAXMODULES];
static const Module *last_added;
static char cats[MAXCATEGORIES][AP_CATLEN];
static int ncats;

/* the usual categories, in display order; a module may name another one, it
 * then comes after these, in alphabetical order */
static const char *const known_cats[] = {
    AP_CATEGORY_OS30, AP_CATEGORY_OS31, AP_CATEGORY_OS314, AP_CATEGORY_OS323, AP_CATEGORY_OS33, AP_CATEGORY_OS35, AP_CATEGORY_OS39,
    AP_CATEGORY_APOLLOOS, AP_CATEGORY_SYSTEM,
    AP_CATEGORY_NETWORK, AP_CATEGORY_SOUND,      /* right after System (user 28.11.2026: easier to reach) */
    AP_CATEGORY_DISPLAY, AP_CATEGORY_LOOK, AP_CATEGORY_INPUT,
    AP_CATEGORY_REQUESTERS, AP_CATEGORY_LOCALE, AP_CATEGORY_PRINTING,
    AP_CATEGORY_DATATYPES, NULL
};

static int lower(int c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 0x20;
    return c;
}

static int ci_cmp(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r'; }

/* AP_CATEGORY_GENERAL before every other category (user 25.11.2026; before: "Preferences", last) */
#define RANK_FIRST (-1)

int reg_category_rank(const char *cat)
{
    int i;
    if (!ci_cmp(cat, AP_CATEGORY_GENERAL)) return RANK_FIRST;
    for (i = 0; known_cats[i]; i++)
        if (!ci_cmp(cat, known_cats[i])) return i;
    return i;
}

/* the order of the modules inside their categories, as the user arranged it (24.11.2026); a module not listed
 * here (a module of another author) comes after these, by title */
static const char *const known_mods[] = {
    /* the OS headings: not listed, by title inside each OS heading (user 24.11) */
    /* System */ "ApolloSpecific", "coffinStartup", "Executive", "Trident", "XpkMaster",
    /* Display */ "Picasso96Mode", "PVS", "PSI", "Wazp3D",
    /* Look & Feel */ "Birdie", "FullPalette", "MUI", "ReqAttack", "ReqChange", "ReqTools", "Themes", "TitleShadow", "TTFManager", "VisualPrefs",
    /* Menus & Desktop */ "MagicMenu", "MCP", "PopupMenu", "ToolsPrefs",
    /* Sound */ "Calibration14", "AHI", "Translator",
    /* Network */ "DefBrowser", "DropBox", "NetIfManager", "NFS", "OnConnect", "OpenURL", "SMB2", "Synergy", "Wireless",
    "NetPrinter",
    /* Datatypes */ "GIFDatatype", "PNGDatatype", "SVGDatatype",
    /* General */ "About", "Updates", NULL
};

static int mod_rank(const char *name)
{
    int i;
    for (i = 0; known_mods[i]; i++)
        if (!ci_cmp(name, known_mods[i])) return i;
    return i;
}

int reg_module_cmp(const Module *a, const Module *b)
{
    int ra = mod_rank(a->name), rb = mod_rank(b->name);
    return ra != rb ? ra - rb : ci_cmp(a->title, b->title);
}

static int cat_cmp(const char *a, const char *b)
{
    int ra = reg_category_rank(a), rb = reg_category_rank(b);
    return ra != rb ? ra - rb : ci_cmp(a, b);
}

/* i-th item of the comma-separated list, spaces trimmed; -1 = no such item,
 * else its length (may be longer than len - 1: then cut) */
static int list_item(const char *list, int i, char *buf, int len)
{
    const char *p = list, *e;
    int n;
    if (!p) return -1;
    while (i-- > 0) {
        if (!(p = strchr(p, ','))) return -1;
        p++;
    }
    while (is_space((unsigned char)*p)) p++;
    e = p + strcspn(p, ",");
    while (e > p && is_space((unsigned char)e[-1])) e--;
    n = e - p;
    if (len > 0) {
        int c = n < len - 1 ? n : len - 1;
        memcpy(buf, p, c);
        buf[c] = 0;
    }
    return n;
}

int mod_category(const Module *m, int i, char *buf, int len)
{
    if (i >= AP_MAXCATS) return 0;
    return list_item(m->categories, i, buf, len) > 0;
}

int mod_in_category(const Module *m, const char *cat)
{
    char c[AP_CATLEN];
    int i;
    for (i = 0; mod_category(m, i, c, sizeof(c)); i++)
        if (!ci_cmp(c, cat)) return 1;
    return 0;
}

/* ---- checking a descriptor ---------------------------------------------- */

static int bad_name(const char *s, const char *forbidden)
{
    if (!s || !*s) return 1;
    for (; *s; s++)
        if ((unsigned char)*s <= ' ' || strchr(forbidden, *s)) return 1;
    return 0;
}

/* the field bytes, as keys.c reads them (MS and CHAR have a fixed size) */
static u32 key_need(const Key *k)
{
    if (k->type == KT_MS) return 8;
    if (k->type == KT_CHAR) return 1;
    if (k->type == KT_STRLIST) return (u32)k->size * k->count;
    return k->size;
}

#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); return 1; } while (0)

static int check_key(const Module *m, const Key *k, char *err, int errlen)
{
    int j;
    const char *n = k->name ? k->name : "?";
    if (bad_name(k->name, "=,;\"")) FAIL("key %d: bad name", (int)(k - m->keys));
    for (j = 0; &m->keys[j] < k; j++)
        if (!ci_cmp(m->keys[j].name, k->name)) FAIL("key %s: twice", n);
    if (!k->label || !*k->label) FAIL("key %s: no label", n);
    if (k->type > KT_BYTES) FAIL("key %s: unknown type %d", n, k->type);
    if (k->type == KT_ENUM && (!k->names || !k->names[0])) FAIL("key %s: enum without names", n);
    if (k->type == KT_STR && k->size < 2) FAIL("key %s: text needs size >= 2", n);
    if (k->type == KT_STRLIST && (k->size < 2 || !k->count)) FAIL("key %s: list needs size >= 2 and count", n);
    if (k->type == KT_DEC && (!k->values || k->values[0] <= 0 || k->values[1] <= 0 || k->values[2] < 0 || k->values[2] > 6))
        FAIL("key %s: decimal needs values { mul > 0, div > 0, decimals 0-6 }", n);
    if (m->kind == MK_ENV) {
        if (k->type == KT_STRLIST || k->type == KT_MS || k->type == KT_CHAR || k->type == KT_RGB ||
            k->type == KT_VSTR || k->type == KT_BYTES)
            FAIL("key %s: this type needs an IFF file", n);
        if (k->type == KT_STR && k->size > AP_ENVLEN) FAIL("key %s: text size at most %d", n, AP_ENVLEN);
        return 0;
    }
    if (m->kind != MK_IFF) return 0;
    switch (k->type) {
    case KT_RGB:
        if (k->size != 3 && k->size != 6) FAIL("key %s: colour size must be 3 or 6", n);
        break;
    case KT_INT: case KT_BOOL: case KT_ENUM: case KT_HEX: case KT_DEC:
        if (k->size != 1 && k->size != 2 && k->size != 4) FAIL("key %s: field size must be 1, 2 or 4", n);
        break;
    }
    if (k->chunk >= m->nchunks) FAIL("key %s: chunk %d of %d", n, k->chunk, m->nchunks);
    if (k->type == KT_VSTR || (k->type == KT_BYTES && !k->size)) {
        if (k->off > m->chunks[k->chunk].size) FAIL("key %s: starts past the chunk size", n);
        if (k->count == 2 && k->off < 2) FAIL("key %s: no room for its length", n);
        return 0;
    }
    if (k->off + key_need(k) > m->chunks[k->chunk].size)
        FAIL("key %s: bytes %d-%d past the chunk size %d", n, k->off, (int)(k->off + key_need(k) - 1),
             m->chunks[k->chunk].size);
    return 0;
}

/* "0.1", "12.34": digits, one dot, digits */
static int bad_version(const char *v)
{
    int dots = 0, digits = 0;
    if (!v || !*v) return 1;
    for (; *v; v++) {
        if (*v == '.') { if (!digits || dots++) return 1; digits = 0; }
        else if (*v >= '0' && *v <= '9') digits++;
        else return 1;
    }
    return !dots || !digits;
}

int mod_check(const Module *m, char *err, int errlen)
{
    char c[AP_CATLEN], d[AP_CATLEN];
    int i, j, l;
    if (!m) FAIL("no descriptor");
    if (bad_name(m->name, "/:=,;\"*?#") || strlen(m->name) > 30) FAIL("bad name");
    if (!m->title || !*m->title) FAIL("no title");
    if (bad_version(m->version)) FAIL("no version \"major.minor\"");
    if (m->kind > MK_ACTION) FAIL("unknown kind %d", m->kind);
    for (i = 0; (l = list_item(m->categories, i, c, sizeof(c))) >= 0; i++) {
        if (l == 0) FAIL("empty category");
        if (l >= AP_CATLEN) FAIL("category longer than %d characters", AP_CATLEN - 1);
        if (i >= AP_MAXCATS) FAIL("more than %d categories", AP_MAXCATS);
        for (j = 0; j < i; j++)
            if (list_item(m->categories, j, d, sizeof(d)) >= 0 && !ci_cmp(c, d)) FAIL("category %s twice", c);
    }
    if (!i) FAIL("no category");
    if (m->kind == MK_LAUNCH && (!m->editor || !*m->editor)) FAIL("no editor");
#ifndef AP_NO_GUI
    if (m->kind == MK_ACTION && (!m->write || !m->page)) FAIL("an action module needs write and page");
#endif
    if (m->kind == MK_IFF) {
        if (bad_name(m->file, "/:")) FAIL("bad file name");
        if (m->nkeys && (!m->chunks || !m->nchunks)) FAIL("no chunks");
    }
    if (m->nkeys && !m->keys) FAIL("no keys");
    for (i = 0; i < m->nkeys; i++)
        if (check_key(m, &m->keys[i], err, errlen)) return 1;
    return 0;
}

/* ---- the list ----------------------------------------------------------- */

static void add_cat(const char *c)
{
    int i, j;
    for (i = 0; i < ncats; i++)
        if (!ci_cmp(cats[i], c)) return;
    if (ncats >= MAXCATEGORIES) return;
    for (j = ncats; j > 0 && cat_cmp(cats[j - 1], c) > 0; j--) strcpy(cats[j], cats[j - 1]);
    strcpy(cats[j], c);
    ncats++;
}

/* one more module into the sorted list (first category, then title), after its equals: the list a
 * full insertion sort of the modules in load order gives. Before 0.19 the whole list was sorted again
 * at each module: cubic in work, 15.6 s of the start-up with 146 modules on a V4 (25.11.2026); the key
 * (category rank and name) is made once per module, not at each compare (0.34 s more on the V4) */
static void reg_insert(const Module *m)
{
    char c[AP_CATLEN];
    int j, rank, d;
    mod_category(m, 0, c, sizeof(c));
    rank = reg_category_rank(c);
    for (j = nentries - 1; j > 0; j--) {
        d = sorted_key[j - 1].rank != rank ? sorted_key[j - 1].rank - rank : ci_cmp(sorted_key[j - 1].cat, c);
        if (!d) d = ci_cmp(sorted[j - 1]->title, m->title);
        if (d <= 0) break;
        sorted[j] = sorted[j - 1];
        sorted_key[j] = sorted_key[j - 1];
    }
    sorted[j] = m;
    sorted_key[j].rank = rank;
    strcpy(sorted_key[j].cat, c);
    sorted[nentries] = NULL;
    for (j = 0; mod_category(m, j, c, sizeof(c)); j++) add_cat(c);
}

int reg_add(const Module *m, void *owner, void (*release)(void *owner), const char **why)
{
    static char err[80];
    const char *dummy;
    if (!why) why = &dummy;
    if (mod_check(m, err, sizeof(err))) { *why = err; return 0; }
    if (mod_find(m->name)) { *why = "a module of that name is already loaded"; return 0; }
    if (nentries >= MAXMODULES) { *why = "too many modules"; return 0; }
    entries[nentries].m = m;
    entries[nentries].owner = owner;
    entries[nentries].release = release;
    nentries++;
    last_added = m;
    reg_insert(m);
    return 1;
}

const Module *reg_last_added(void) { return last_added; }

int reg_ncategories(void) { return ncats; }
const char *reg_category(int c) { return c >= 0 && c < ncats ? cats[c] : NULL; }

const Module *mod_find(const char *name)
{
    int i;
    for (i = 0; i < nentries; i++)
        if (!ci_cmp(entries[i].m->name, name)) return entries[i].m;
    return NULL;
}

/* a module by its name, or by the name it had before the OS suffixes (24.11): "<name>" finds "<name>_<suffix>"
 * when it is the only <name>_<OS suffix> loaded; *several = how many matched (more than 1: NULL) */
const Module *mod_lookup(const char *name, int *several)
{
    static const char *const suf[] = { "OS30", "OS31", "OS314", "OS323", "OS33", "OS35", "OS39", "Apollo", NULL };
    const Module *m = mod_find(name), *found = NULL;
    size_t n = strlen(name);
    int i, k, count = 0;
    if (several) *several = m ? 1 : 0;
    if (m) return m;
    for (i = 0; i < nentries; i++) {
        const char *e = entries[i].m->name;
        if (strlen(e) <= n + 1 || e[n] != '_') continue;
        {
            char head[64];
            if (n >= sizeof(head)) continue;
            memcpy(head, e, n); head[n] = 0;
            if (ci_cmp(head, name)) continue;
        }
        for (k = 0; suf[k]; k++)
            if (!ci_cmp(e + n + 1, suf[k])) { found = entries[i].m; count++; break; }
    }
    if (several) *several = count;
    return count == 1 ? found : NULL;
}

void reg_free(void)
{
    while (nentries > 0) {
        Entry *e = &entries[--nentries];
        if (e->release) e->release(e->owner);
    }
    sorted[0] = NULL;
    ncats = 0;
    last_added = NULL;
}

/* ---- text modules --------------------------------------------------------
 * A text module opens an editor UniPrefs does not replace:
 *     Name=MCP
 *     Title=MCP
 *     Editor=SYS:Prefs/MCP
 *     Categories=Menus & Desktop
 * ";" starts a comment line; Name, Editor and Categories are needed. Other keys are ignored: Author=,
 * BasedOn=, Description= and OS= are for the Updates page only (it reads them from the file). */

typedef struct TextModule {
    Module m;
    char text[1];               /* the values, NUL-separated */
} TextModule;

static void release_text(void *owner) { free(owner); }

int reg_parse_launcher(const char *text, const char **err)
{
    static const char *const fields[] = { "Name", "Title", "Editor", "Categories", "Version", NULL };
    const char *val[5] = { NULL, NULL, NULL, NULL, NULL };
    int vlen[5] = { 0, 0, 0, 0, 0 };
    const char *p = text;
    size_t total = 0;
    TextModule *t;
    char *o;
    int i;

    while (*p) {
        const char *e = strchr(p, '\n'), *eq, *ve;
        if (!e) e = p + strlen(p);
        while (p < e && is_space((unsigned char)*p)) p++;
        if (p < e && *p != ';' && (eq = memchr(p, '=', e - p)) != NULL) {
            const char *ke = eq, *vs = eq + 1;
            while (ke > p && is_space((unsigned char)ke[-1])) ke--;
            while (vs < e && is_space((unsigned char)*vs)) vs++;
            ve = e;
            while (ve > vs && is_space((unsigned char)ve[-1])) ve--;
            for (i = 0; fields[i]; i++)
                if ((size_t)(ke - p) == strlen(fields[i]) && !memcmp(p, fields[i], ke - p)) {
                    val[i] = vs;
                    vlen[i] = ve - vs;
                }
        }
        p = *e ? e + 1 : e;
    }
    if (!val[0] || !vlen[0]) { *err = "no Name="; return 0; }
    if (!val[2] || !vlen[2]) { *err = "no Editor="; return 0; }
    if (!val[3] || !vlen[3]) { *err = "no Categories="; return 0; }
    if (!val[4] || !vlen[4]) { *err = "no Version="; return 0; }
    for (i = 0; i < 5; i++) total += vlen[i] + 1;
    if (!(t = calloc(1, sizeof(*t) + total))) { *err = "out of memory"; return 0; }
    o = t->text;
    for (i = 0; i < 5; i++) {
        const char *start = o;
        if (val[i]) memcpy(o, val[i], vlen[i]);
        o[vlen[i]] = 0;
        o += vlen[i] + 1;
        switch (i) {
        case 0: t->m.name = start; break;
        case 1: t->m.title = *start ? start : t->m.name; break;
        case 2: t->m.editor = start; break;
        case 3: t->m.categories = start; break;
        case 4: t->m.version = start; break;
        }
    }
    t->m.kind = MK_LAUNCH;
    if (!reg_add(&t->m, t, release_text, err)) {
        free(t);
        return 0;
    }
    return 1;
}
