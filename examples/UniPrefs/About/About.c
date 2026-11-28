/* About - the program's version, author, what it is, its modules, the system's versions and the update
 * repository (docs/about-spec-2026-11-24.md). Nothing to write: Save/Use/Reset ghosted (PB_NOWRITE).
 * The version and the module list come from the host (APHost version 4: version_string, module_list).
 * 0.4 (25.11.2026): a System group: processor (68080 board and core), FPU, chipset, memory, exec and Workbench.
 */
#include "kit.h"
#include "uprefs.h"

#ifndef AP_NO_GUI
/* Urltext.mcc (MUI custom class, not part of MUI 3.8): a link that opens the browser through openurl.library;
 * without it the address is plain text */
#define AB_URLTEXT      "Urltext.mcc"
#define AB_URLTEXT_URL  0xfec900d0UL
#define AB_URLTEXT_TEXT 0xfec900d1UL

static char name[40], verline[60], mods[120], libs[3][24], url[140], cpu[80], fpu[24], chips[16], mem[2][48];

/* the Apollo board registers (Vampire, AC68080 only), as ApolloControl reads them: $DFF3FC high byte = board id,
 * low byte = clock multiplier; $DFF3EA = core release on the V4 boards (ids 3, 4, 5, 7, 8). Measured on the V4
 * A6000 (25.11.2026): $DFF3FC = $080E (V4 Unicorn, x14), $DFF3EA = 12870, AttnFlags = $847F (bit 10 = 68080). */
#define AB_AFF_68080 (1 << 10)
#define AB_VREG_BOARD 0xdff3fcUL
#define AB_VREG_CORE  0xdff3eaUL

static void cpu_text(void)
{
    static const char *const boards[] = {
        "Apollo", "V2 V600", "V2 V500", "V4 Firebird", "V4 Icedrake", "V4 Standalone", "V2 V1200", "V4 Manticore",
        "V4 Unicorn"
    };
    UWORD f = SysBase->AttnFlags;
    if (f & AB_AFF_68080) {
        UWORD b = *(volatile UWORD *)AB_VREG_BOARD;
        int id = b >> 8, mult = b & 0xff;
        int v4 = id == 3 || id == 4 || id == 5 || id == 7 || id == 8;
        const char *bn = id <= 8 ? boards[id] : "Apollo";
        if (v4) snprintf(cpu, sizeof(cpu), "68080 (%s, core %u, x%d)", bn, (unsigned)*(volatile UWORD *)AB_VREG_CORE, mult);
        else snprintf(cpu, sizeof(cpu), "68080 (%s, x%d)", bn, mult);
    } else
        snprintf(cpu, sizeof(cpu), "%s", (f & AFF_68060) ? "68060" : (f & AFF_68040) ? "68040" : (f & AFF_68030) ? "68030"
                 : (f & AFF_68020) ? "68020" : (f & AFF_68010) ? "68010" : "68000");
    snprintf(fpu, sizeof(fpu), "%s", (f & AFF_FPU40) && (f & (AFF_68040 | AFF_68060 | AB_AFF_68080)) ? "internal"
             : (f & AFF_68882) ? "68882" : (f & AFF_68881) ? "68881" : "none");
}

/* "2.0 MB, 1.4 MB free" (KB below 1 MB), at the time the page is built */
static void mem_text(char *out, int len, ULONG type)
{
    ULONG t = AvailMem(type | MEMF_TOTAL), a = AvailMem(type);
    char ts[16], as[16];
    ULONG v[2]; char *o[2]; int i;
    if (!t) { snprintf(out, len, "none"); return; }
    v[0] = t; v[1] = a; o[0] = ts; o[1] = as;
    for (i = 0; i < 2; i++)
        if (v[i] >= 1024UL * 1024) snprintf(o[i], 16, "%lu.%lu MB", (unsigned long)(v[i] >> 20),
                                            (unsigned long)(((v[i] & 0xfffff) * 10) >> 20));
        else snprintf(o[i], 16, "%lu KB", (unsigned long)(v[i] >> 10));
    snprintf(out, len, "%s, %s free", ts, as);
}

static void build(void)
{
    char repo[110];
    const char *v = "UniPrefs";
    struct Library *mui = MUIMasterBase;
    if (!upf_get("REPO", 0, repo, sizeof(repo)))      /* UniPrefs.prefs (28.11.2026), else the old variable */
        if (GetVar((STRPTR)AP_UPDATE_REPO_VAR, (STRPTR)repo, sizeof(repo), GVF_GLOBAL_ONLY) <= 0) repo[0] = 0;
    if (!repo[0]) snprintf(repo, sizeof(repo), "%s", AP_UPDATE_REPO);
    snprintf(url, sizeof(url), "https://github.com/%s", repo);
    mods[0] = 0;
    if (AP->version >= 4) {
        const Module *const *m = module_list();
        int i, native = 0, launch = 0, inst = 0;
        v = version_string();
        if (v[0] == '$' && !strncmp(v + 1, "VER: ", 5)) v += 6;     /* no "$VER:" text in this module */
        for (i = 0; m && m[i]; i++) {
            if (m[i]->kind != MK_LAUNCH) native++;
            else {
                launch++;
                if (io_exists(m[i]->editor)) inst++;
            }
        }
        snprintf(mods, sizeof(mods), "%d (%d page%s, %d launcher%s, %d of their editors installed)",
                 native + launch, native, native == 1 ? "" : "s", launch, launch == 1 ? "" : "s", inst);
    }
    /* "UniPrefs 0.4 (24.11.2026)": the name in the title, the rest under it */
    {
        const char *sp = strchr(v, ' ');
        int n = sp ? (int)(sp - v) : (int)strlen(v);
        snprintf(name, sizeof(name), "%.*s", n, v);
        snprintf(verline, sizeof(verline), "Version %s", sp ? sp + 1 : "unknown");
    }
    snprintf(libs[0], sizeof(libs[0]), "%d.%d", SysBase->LibNode.lib_Version, SysBase->LibNode.lib_Revision);
    snprintf(libs[1], sizeof(libs[1]), "%d.%d", mui ? mui->lib_Version : 0, mui ? mui->lib_Revision : 0);
    {   /* the Workbench release: version.library, as the Version command shows it */
        struct Library *vl = OpenLibrary((STRPTR)"version.library", 0);
        if (vl) {
            snprintf(libs[2], sizeof(libs[2]), "%d.%d", vl->lib_Version, vl->lib_Revision);
            CloseLibrary(vl);
        } else
            snprintf(libs[2], sizeof(libs[2]), "unknown");
    }
    cpu_text();
    {   /* graphics.library's chip revision bits */
        UBYTE c = GfxBase->ChipRevBits0;
        snprintf(chips, sizeof(chips), "%s", (c & GFXF_AA_ALICE) ? "AGA" : (c & (GFXF_HR_AGNUS | GFXF_HR_DENISE)) ? "ECS" : "OCS");
    }
    mem_text(mem[0], sizeof(mem[0]), MEMF_CHIP);
    mem_text(mem[1], sizeof(mem[1]), MEMF_FAST);
}

static Object *info_text(const char *t)
{
    return TextObject, MUIA_Text_Contents, (ULONG)t, MUIA_Text_SetMax, FALSE, End;
}

static Object *page(Page *p)
{
    Object *link, *info, *sys;
    p->buttons = PB_NOWRITE;                   /* nothing to save */
    build();
    link = MUI_NewObject((STRPTR)AB_URLTEXT, AB_URLTEXT_URL, (ULONG)url, AB_URLTEXT_TEXT, (ULONG)url, TAG_DONE);
    if (!link) link = info_text(url);
    info = ColGroup(2), GroupFrameT("Information"),
        Child, Label("Modules:"), Child, info_text(mods[0] ? mods : "unknown"),
        Child, Label("muimaster.library:"), Child, info_text(libs[1]),
        Child, Label("Updates:"), Child, HGroup, MUIA_Group_Spacing, 0, Child, link, Child, HSpace(0), End,
        End;
    sys = ColGroup(2), GroupFrameT("System"),
        Child, Label("Processor:"), Child, info_text(cpu),
        Child, Label("FPU:"), Child, info_text(fpu),
        Child, Label("Chipset:"), Child, info_text(chips),
        Child, Label("Chip memory:"), Child, info_text(mem[0]),
        Child, Label("Fast memory:"), Child, info_text(mem[1]),
        Child, Label("exec.library:"), Child, info_text(libs[0]),
        Child, Label("Workbench:"), Child, info_text(libs[2]),
        End;
    return VGroup,
        Child, VGroup, TextFrame, MUIA_Background, MUII_TextBack, MUIA_InnerTop, 8, MUIA_InnerBottom, 8,
            Child, TextObject, MUIA_Font, MUIV_Font_Big, MUIA_Text_PreParse, (ULONG)"\33c\33b",
                MUIA_Text_Contents, (ULONG)name, End,
            Child, TextObject, MUIA_Text_PreParse, (ULONG)"\33c", MUIA_Text_Contents, (ULONG)verline, End,
            Child, TextObject, MUIA_Text_PreParse, (ULONG)"\33c", MUIA_Text_Contents,
                (ULONG)"\251 2026 Renaud Schweingruber", End,
            End,
        Child, TextObject, GroupFrameT("About"), MUIA_Text_PreParse, (ULONG)"\33c", MUIA_Text_Contents, (ULONG)
            "One program for every preference of the system and of the programs installed\n"
            "with it. Each preference is a module: a page of its own that reads and writes\n"
            "the same files as the original editor, or a button that opens that editor.\n"
            "Modules are files in the program's drawer; the Modules page installs newer ones.", End,
        Child, info,
        Child, sys,
        Child, VSpace(0),
        End;
}

static const char *write_none(Page *p, int archive) { (void)p; (void)archive; return NULL; }
#endif

static const Module mod = {
    .name = "About", .title = "About", .version = "0.7", .kind = MK_ACTION,
    .categories = AP_CATEGORY_GENERAL, .mflags = MF_NOICON,
    .page = PAGE(page), .wintitle = "About UniPrefs", .write = WRITE(write_none),
};

AP_INFO("Renaud Schweingruber",
        "-",
        "The program's version, author and purpose, its modules, the update repository and the machine (processor, FPU, chipset, memory, Kickstart and Workbench versions). Nothing to set.")
AP_MODULE(mod, About, "0.7 (28.11.2026)")
