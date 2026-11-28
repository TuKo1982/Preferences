/* apabi - the numbers of the UniPrefs module format, shared by C and by
 * modstart.S (plain #defines only). See apmodule.h. */
#ifndef APABI_H
#define APABI_H

#define AP_MAGIC        0x414C5046      /* 'ALPF' */
#define AP_ABI          4               /* 4: ModState.file, Page.buttons/pending; 3: Module.version; 2: Module.categories */
#define AP_HOST_VERSION 6               /* fields of APHost (apmodule.h); 6: start_arg, Module MF_EXT fields; 5: Page.on_show; 4: version_string, module_list; 3: st_save, page_reload; 2: value_get/set, pixels_new, pf_resize */

#endif
