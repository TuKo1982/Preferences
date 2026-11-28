/* io - the few system services the pure-C part needs.
 * amiga_io.c implements them with dos.library; tests/host_io.c with stdio.
 */
#ifndef IO_H
#define IO_H

#include "prefsfile.h"

int io_load(const char *path, u8 **buf, u32 *len);          /* 1 = ok, buf is malloc'd */
int io_save(const char *path, const u8 *buf, u32 len);      /* 1 = ok */
int io_exists(const char *path);
int io_getvar(const char *name, char *buf, int len);        /* 1 = set */
int io_setvar(const char *name, const char *val, int archive); /* 1 = ok; archive: ENVARC too */
int io_mode_ok(u32 displayid);                              /* display ID known to graphics */
void io_protect(const char *path);                          /* clear e, as the editors do on ENVARC/Save As */
void io_fault(char *buf, int len);                          /* the text of the last dos error */
const char *io_text(long id, const char *def);              /* a Sys/prefs.catalog text */

#endif
