/* apcheck - a module on the build machine (make check in module.mk).
 *
 * The module source is built with AP_NO_GUI AP_CHECK, so AP_MODULE names its
 * descriptor ap_check_module, and linked with UniPrefs' own key engine
 * (reg_core, prefsfile, keys, state) and host_io.c.
 *     obj/check/<Name>            the checks UniPrefs makes at load, keys of a new file
 *     obj/check/<Name> file       the keys as read from that preferences file
 *     obj/check/<Name> file K=V.. set keys, then write the result to file.out
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"
#include "state.h"
#include "io.h"

extern const Module *ap_check_module;
const char ap_check_info[] __attribute__((weak)) = "";       /* replaced by the module's AP_INFO, when it has one */
void host_io_root(const char *r);     /* host_io.c */

static const char *const kinds[] = { "IFF preferences file", "environment variables", "external editor", "own page (no keys)" };

/* AP_INFO: "$APINFO:" author TAB based-on TAB description [TAB OS] (AP_INFO_OS), printable ASCII, each field
 * 1..max-1 characters (the sizes the Updates page keeps). 0 = fine or none, 1 = refused */
static int check_info(void)
{
    static const char *const what[4] = { "author", "based on", "description", "OS" };
    static const int max[4] = { 64, 128, 256, 48 };
    const char *p, *f[4];
    int i, len[4], n = 0;
    if (!ap_check_info[0]) { printf("  no AP_INFO: the Updates page shows \"-\" for author, based on and description\n"); return 0; }
    if (strncmp(ap_check_info, "$APINFO:", 8)) { printf("  AP_INFO: does not start with $APINFO:\n"); return 1; }
    f[0] = p = ap_check_info + 8;
    for (; *p; p++) {
        if (*p == '\t') { if (n == 3) { printf("  AP_INFO: more than 4 fields (a tab inside a field?)\n"); return 1; } f[++n] = p + 1; }
        else if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e) { printf("  AP_INFO: not plain printable ASCII\n"); return 1; }
    }
    if (n < 2) { printf("  AP_INFO: 3 fields expected (author, based on, description), a 4th (OS) with AP_INFO_OS\n"); return 1; }
    for (i = 0; i <= n; i++) len[i] = (int)(i < n ? f[i + 1] - f[i] - 1 : (int)strlen(f[i]));
    for (i = 0; i <= n; i++)
        if (len[i] < 1 || len[i] >= max[i]) { printf("  AP_INFO: %s must be 1 to %d characters (\"-\" for none)\n", what[i], max[i] - 1); return 1; }
    printf("  info: %.*s; based on %.*s%s%.*s\n  %.*s\n", len[0], f[0], len[1], f[1], n == 3 ? "; OS " : "",
           n == 3 ? len[3] : 0, n == 3 ? f[3] : "", len[2], f[2]);
    return 0;
}

static int set_keys(ModState *st, char **kv, int n)
{
    char err[200];
    int i;
    for (i = 0; i < n; i++) {
        char *eq = strchr(kv[i], '=');
        const Key *k;
        if (!eq) { printf("  %s: Name=Value expected\n", kv[i]); return 10; }
        *eq = 0;
        if (!(k = key_find(st->m, kv[i]))) { printf("  no key %s\n", kv[i]); return 10; }
        if (key_set(st, k, eq + 1, err, sizeof(err))) { printf("  %s\n", err); return 10; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const Module *m = ap_check_module;
    const char *from = argc > 1 ? argv[1] : NULL;
    char err[200], c[AP_CATLEN], v[400], d[120];
    ModState st;
    int i, rc;
    if (mod_check(m, err, sizeof(err))) {
        printf("%s: REFUSED: %s\n", m && m->name ? m->name : "module", err);
        return 1;
    }
    printf("%s: ok, \"%s\", %s, %d key%s\n", m->name, m->title, kinds[m->kind], m->nkeys, m->nkeys == 1 ? "" : "s");
    printf("  categories:");
    for (i = 0; mod_category(m, i, c, sizeof(c)); i++) printf("%s %s", i ? "," : "", c);
    printf("\n");
    if (check_info()) return 1;
    if (m->kind == MK_LAUNCH || m->kind == MK_ACTION) return 0;   /* no keys to read */
    host_io_root(".");
    if ((rc = st_load(&st, m, from, err, sizeof(err))) != 0) {
        printf("  %s\n", err);
        st_free(&st);
        return rc;
    }
    if (argc > 2 && (rc = set_keys(&st, argv + 2, argc - 2)) != 0) {
        st_free(&st);
        return rc;
    }
    for (i = 0; i < m->nkeys; i++) {
        const Key *k = &m->keys[i];
        if (key_get(&st, k, v, sizeof(v))) strcpy(v, "?");
        key_describe(k, d, sizeof(d));
        printf("  %s=%s ; %s\n", k->name, v, d);
    }
    if (argc > 2 && m->kind == MK_IFF) {
        snprintf(v, sizeof(v), "%s.out", from);
        printf(io_save(v, st.pf.data, st.pf.len) ? "  written to %s\n" : "  cannot write %s\n", v);
    }
    st_free(&st);
    return 0;
}
