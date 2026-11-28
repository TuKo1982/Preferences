#ifndef REGISTRY_H
#define REGISTRY_H

#include "module.h"

/* reg_core.c: the loaded modules, sorted by first category then title,
 * NULL-terminated; and the categories they name, in list order */
extern const Module **all_modules;
const Module *mod_find(const char *name);
const Module *mod_lookup(const char *name, int *several);
/* 1 = added; 0 = refused, *why says why (mod_check, or a duplicate name) */
int  reg_add(const Module *m, void *owner, void (*release)(void *owner), const char **why);
void reg_free(void);
int  reg_parse_launcher(const char *text, const char **err);  /* text module -> reg_add */
int  reg_category_rank(const char *cat);      /* the usual categories first, in this order */
int  reg_module_cmp(const Module *a, const Module *b);   /* list order inside a category (< 0: a first) */
int  reg_ncategories(void);
const char *reg_category(int c);              /* c-th category in list order */
int  mod_in_category(const Module *m, const char *cat);

/* amiga_reg.c: loads PROGDIR:Modules/<only>.module, or every module when
 * only is NULL (or that file did not give the module). Returns how many
 * modules were added. reg_free() unloads them. */
int reg_load(const char *only);
/* one module file, from anywhere (CHECK=): the module it added, or NULL
 * (the reason is printed) */
const Module *reg_load_file(const char *path);
const Module *reg_last_added(void);           /* reg_core.c */

#endif
