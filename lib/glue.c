/* glue - the C runtime of a binary module, taken from UniPrefs (apmodule.h):
 * one allocator for both sides (io_load buffers are freed by modules). */
#include "kit.h"

APHost *AP;

/* real symbols too, for the libnix pieces linked in (memcpy calls CopyMem) */
#undef SysBase
#undef DOSBase
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

void *malloc(size_t n) { return AP->malloc(n); }
void free(void *p) { AP->free(p); }
void *realloc(void *p, size_t n) { return AP->realloc(p, n); }
void *calloc(size_t a, size_t b)
{
    void *p = AP->malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) { return AP->vsnprintf(buf, n, fmt, ap); }

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = AP->vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

/* sprintf too: else -lamiga's RawDoFmt sprintf is linked (16-bit %d, no usable return), 23.11 */
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = AP->vsnprintf(buf, 0x7fffffff, fmt, ap);
    va_end(ap);
    return r;
}

/* MUI's varargs entries (libmui.a would want a MUIMasterBase symbol) */
Object *MUI_NewObject(CONST_STRPTR cls, Tag tag1, ...)
{
    return MUI_NewObjectA(cls, (struct TagItem *)&tag1);
}

Object *MUI_MakeObject(LONG type, ...)
{
    va_list ap;
    ULONG *params;
    va_start(ap, type);
    params = (ULONG *)ap;            /* m68k: the arguments follow on the stack */
    va_end(ap);
    return MUI_MakeObjectA(type, params);
}

LONG MUI_Request(Object *app_, Object *win_, ULONG flags, CONST_STRPTR title, CONST_STRPTR gadgets,
                 CONST_STRPTR format, ...)
{
    va_list ap;
    LONG r;
    va_start(ap, format);
    r = MUI_RequestA(app_, win_, flags, title, gadgets, format, (APTR)ap);
    va_end(ap);
    return r;
}

APTR MUI_AllocAslRequestTags(ULONG type, Tag tag1, ...)
{
    return MUI_AllocAslRequest(type, (struct TagItem *)&tag1);
}

BOOL MUI_AslRequestTags(APTR req, Tag tag1, ...)
{
    return MUI_AslRequest(req, (struct TagItem *)&tag1);
}

int ap_glue(APHost *host)
{
    if (!host || host->version < AP_HOST_VERSION || host->size < sizeof(APHost)) return 0;
    AP = host;
    SysBase = host->SysBase;
    DOSBase = host->DOSBase;
    return 1;
}
