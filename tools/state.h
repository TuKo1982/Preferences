#ifndef STATE_H
#define STATE_H

#include "module.h"

int  st_load(ModState *st, const Module *m, const char *from, char *err, int errlen); /* 0 ok, else RC */
int  st_load_file(ModState *st, const Module *m, const char *file, const char *from, char *err, int errlen);
const char *st_unit_file(const Module *m, long unit);   /* XF_UNITS: "<name><unit>.prefs" for unit > 0, else NULL */
int  st_save(ModState *st, int archive, char *err, int errlen);                       /* 0 ok, else RC */
int  st_needs_reboot(const ModState *st);   /* call before st_save */
void st_free(ModState *st);
int  st_defaults(ModState *st, const Module *m, char *err, int errlen);   /* Reset To Defaults */
int  st_load_saved(ModState *st, const ModState *cur, char *err, int errlen); /* Last Saved (ENVARC) */
int  st_reload(ModState *n, const ModState *cur, const char *path, char *err, int errlen); /* file on top */
int  st_copy(ModState *dst, const ModState *src);
/* the start arguments USER and UNIT (CLI or tool types, set by main.c), NULL when not given */
const char *start_arg(const char *name);
void start_args_set(const char *user, long unit);
void start_install_set(const char *os);       /* start_arg("INSTALLOS") */
extern long start_unit;
int  st_save_as(ModState *st, const char *path, char *err, int errlen);   /* Save As */

#endif
