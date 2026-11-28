/* kit - include this, and only this, at the top of a module source
 * (UniPrefs module SDK, see README.md).
 *
 * One file per module: its keys, its hooks, its page (inside #ifndef
 * AP_NO_GUI), AP_INFO(author, based on, description) and AP_MODULE(descriptor, Name, version) at the end. System
 * headers go here, before the names below become macros.
 * Built as a binary module (AP_PLUGIN), every UniPrefs function and library
 * base named below reaches UniPrefs through the APHost table; built into the
 * host tests (AP_NO_GUI), the same names are the real functions.
 */
#ifndef KIT_H
#define KIT_H

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "module.h"
#include "prefsfile.h"
#include "io.h"

#ifndef AP_NO_GUI
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>
#include <libraries/asl.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <datatypes/soundclass.h>
#include <proto/datatypes.h>
#include "gui_int.h"
#include "art.h"
#include "apmodule.h"

#define HOOK(name) static LONG name(register struct Hook *h __asm("a0"), register Object *o __asm("a2"), \
                                    register ULONG *args __asm("a1"))
#define HOOKDEF(var, fn) static struct Hook var = { { NULL, NULL }, (APTR)fn, NULL, NULL }
/* the GUI entries of a Module, NULL in the host tests */
#define PAGE(fn)   (void *(*)(struct Page *))(fn)
#define AFTER(fn)  (fn)
#define BEFORE(fn) (fn)
#define EXIT(fn)   (fn)
#define WRITE(fn)  (const char *(*)(struct Page *, int))(fn)
/* a text of the module's catalog (#define CATALOG "Sys/prefs.catalog" first),
 * by the number the original editor gives it */
#define TR(id, s)  cat_str(CATALOG, (id), (s))
#define TRL(texts, ids) cat_list(CATALOG, (const char **)(texts), (ids))
#else
#define PAGE(fn)   NULL
#define AFTER(fn)  NULL
#define BEFORE(fn) NULL
#define EXIT(fn)   NULL
#define WRITE(fn)  NULL
#endif

#define NOR 1, 0            /* Key.min > Key.max: no range */
#define SIZEOF(a) (sizeof(a) / sizeof((a)[0]))

#ifdef AP_PLUGIN
extern APHost *AP;
int ap_glue(APHost *host);

#define SysBase       (AP->SysBase)
#define DOSBase       (AP->DOSBase)
#define IntuitionBase (AP->IntuitionBase)
#define GfxBase       (AP->GfxBase)
#define UtilityBase   (AP->UtilityBase)
#define MUIMasterBase (AP->MUIMasterBase)

#define key_find      AP->key_find
#define key_get       AP->key_get
#define key_set       AP->key_set
#define key_raw_get   AP->key_raw_get
#define key_raw_set   AP->key_raw_set
#define key_field     AP->key_field
#define str_ieq       AP->str_ieq
#define pf_init       AP->pf_init
#define pf_free       AP->pf_free
#define pf_take       AP->pf_take
#define pf_find       AP->pf_find
#define pf_need       AP->pf_need
#define io_load       AP->io_load
#define io_exists     AP->io_exists
#define io_mode_ok    AP->io_mode_ok

#define app           (*AP->app)
#define win           (*AP->win)
#define bind          AP->bind
#define bind_refresh  AP->bind_refresh
#define bind_text     AP->bind_text
#define run_button    AP->run_button
#define check_row     AP->check_row
#define str_list      AP->str_list
#define listview      AP->listview
#define slider        AP->slider
#define text_fixed    AP->text_fixed
#define int_string    AP->int_string
#define stepper       AP->stepper
#define fill_dir      AP->fill_dir
#define notify_refresh AP->notify_refresh
#define time_row      AP->time_row
#define cycle         AP->cycle
#define radio         AP->radio
#define mode_nominal  AP->mode_nominal
#define art_new       AP->art_new
#define flaglist_new  AP->flaglist_new
#define flaglist_new2 AP->flaglist_new2
#define zones_count   AP->zones_count
#define zones_east    AP->zones_east
#define value_get     AP->value_get
#define value_set     AP->value_set
#define pixels_new    AP->pixels_new
#define pixels_tools  AP->pixels_tools
#define page_test     AP->page_test
#define orig_data     AP->orig_data
#define cat_str       AP->cat_str
#define cat_menu      AP->cat_menu
#define cat_list      AP->cat_list
#define pf_resize     AP->pf_resize
#define st_save       AP->st_save
#define page_reload   AP->page_reload
#define page_collect  AP->page_collect
#define version_string AP->version_string
#define module_list   AP->module_list
#define start_arg     AP->start_arg

/* the module's entry point and its version string ("version Modules/x.module") */
#define AP_MODULE(var, Name, ver) \
    static const char ap_ver[] __attribute__((used)) = "$VER: " #Name ".module " ver; \
    const Module *ap_init(APHost *host) \
    { \
        /* the descriptor's version and the $VER string agree, or the module refuses */ \
        const char *v = ap_ver + sizeof("$VER: " #Name ".module ") - 1; \
        size_t n = (var).version ? strlen((var).version) : 0; \
        if (!n || strncmp(v, (var).version, n) || (v[n] != ' ' && v[n])) return NULL; \
        return ap_glue(host) ? &var : NULL; \
    }
#elif defined(AP_CHECK)
#define AP_MODULE(var, Name, ver) const Module *ap_check_module = &var;
#else
#define AP_MODULE(var, Name, ver) const Module *ap_mod_##Name = &var;
#endif

/* what the Updates page shows about the module, read from the file without loading it (like $VER):
 * AP_INFO("author", "based on", "description") next to AP_MODULE emits the one string
 * "$APINFO:" author TAB based-on TAB description. Plain ASCII, no tab inside a field; "-" = based on nothing.
 * AP_INFO_OS(author, based on, description, os) adds a fourth field: the operating system (an AP_CATEGORY_*) whose own
 * editor the module reproduces, with its version (only for an editor of the OS itself).
 * make check (AP_CHECK) shows and checks it. Data only: UniPrefs itself never reads it. */
#ifdef AP_CHECK
#define AP_INFO_OS(author, basedon, desc, os) \
    const char ap_check_info[] = "$APINFO:" author "\t" basedon "\t" desc "\t" os;
#define AP_INFO(author, basedon, desc) const char ap_check_info[] = "$APINFO:" author "\t" basedon "\t" desc;
#else
#define AP_INFO_OS(author, basedon, desc, os) \
    static const char ap_info[] __attribute__((used)) = "$APINFO:" author "\t" basedon "\t" desc "\t" os;
#define AP_INFO(author, basedon, desc) \
    static const char ap_info[] __attribute__((used)) = "$APINFO:" author "\t" basedon "\t" desc;
#endif

#endif
