#ifndef ART_H
#define ART_H

#include <exec/types.h>
#include <intuition/classusr.h>

enum { ART_MOUSE, ART_MAP, ART_PREVIEW, ART_WEDGE, ART_SPEAKER, ART_SCREEN, ART_KEY, ART_ICON };
/* ART_ICON: the icon of a file, ARTA_IconPath = the file (its .info), centred, no frame */
/* ART_KEY: a key cap, ARTA_Value = KEY_* */
enum { KEY_SHIFT, KEY_CTRL, KEY_ALT, KEY_AMIGA };

#define ARTA_Type       (TAG_USER | 0x41500001)   /* I   */
#define ARTA_Value      (TAG_USER | 0x41500002)   /* ISG mouse speed 0..2, map minutes east */
#define ARTA_FontIcon   (TAG_USER | 0x41500003)   /* IS  "name/size" */
#define ARTA_FontSystem (TAG_USER | 0x41500004)
#define ARTA_FontScreen (TAG_USER | 0x41500005)
#define ARTA_IconPens   (TAG_USER | 0x41500006)   /* IS  text pen << 16 | field pen << 8 | mode */
#define ARTA_IconPath   (TAG_USER | 0x41500007)   /* IS  file whose icon is shown (NULL: none) */
#define FLA_Dir         (TAG_USER | 0x41500010)   /* I   LOCALE:Flags/<dir> */

int  art_init(void);      /* after muimaster.library is open */
void art_exit(void);
Object *art_new(ULONG type, ULONG tag1, ...);
Object *flaglist_new(const char *dir);
Object *flaglist_new2(const char *dir);   /* two columns: name, file */
int  zones_count(void);       /* time zones, 0 = none */
LONG zones_east(int i);       /* minutes east of GMT, east-going order */

#endif
