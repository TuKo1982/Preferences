/* Editor - example UniPrefs module: the EDITOR environment variable (the
 * text editor that programs start), kept in ENV: and ENVARC:.
 *
 *   UniPrefs Editor GET=EDITOR
 *   UniPrefs Editor SET EDITOR=C:Ed SAVE
 *
 * A module of kind MK_ENV: each key is a global variable of the same name.
 * It is listed under a category of its own ("Shell") and under "System".
 */
#include "kit.h"

static const Key keys[] = {
    { .name = "EDITOR", .label = "Text editor", .type = KT_STR, .size = 60, .min = NOR,
      .def = "", .help = "The program started to edit a text (ENV:EDITOR)" },
};

#ifndef AP_NO_GUI

static Object *page(Page *p)
{
    Object *str = StringObject, StringFrame, MUIA_String_MaxLen, 60, MUIA_CycleChain, 1, End;
    Object *pop = PopaslObject,
        MUIA_Popstring_String, str,
        MUIA_Popstring_Button, PopButton(MUII_PopFile),
        ASLFR_TitleText, "Text editor",
        End;
    bind(p, "EDITOR", str, AD_STRING, NULL);
    return VGroup,
        Child, VGroup, GroupFrameT("Text editor"),
            Child, HGroup, Child, Label2("Program"), Child, pop, End,
            Child, TextObject, MUIA_Font, MUIV_Font_Tiny,
                MUIA_Text_Contents, "Programs that open a text editor start this one.", End,
            End,
        Child, VSpace(0),
        End;
}

#endif

static const Module mod = {
    .name = "Editor", .title = "Text editor", .version = "0.1", .kind = MK_ENV,
    .keys = keys, .nkeys = SIZEOF(keys),
    .categories = "Shell, " AP_CATEGORY_SYSTEM,
    .page = PAGE(page), .wintitle = "Text Editor",
};

AP_INFO("UniPrefs SDK example", "-", "The text editor the Shell and other programs start (ENV:EDITOR).")
AP_MODULE(mod, Editor, "0.1 (23.11.2026)")
