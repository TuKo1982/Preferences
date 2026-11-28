/* prefsfile - an IFF FORM PREF file held in memory.
 *
 * The whole file stays as bytes: a module only changes fields inside the
 * chunks it knows, so unknown chunks and untouched bytes are written back
 * exactly as they were read. Pure C, no AmigaOS calls (host-testable).
 */
#ifndef PREFSFILE_H
#define PREFSFILE_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;   /* int is 32 bits on m68k-amigaos and on the host */
typedef int            s32;

#define PF_ID(a,b,c,d) ((u32)(a)<<24 | (u32)(b)<<16 | (u32)(c)<<8 | (u32)(d))

typedef struct {
    u8  *data;
    u32  len;   /* bytes used */
    u32  cap;   /* bytes allocated */
} PFile;

/* Selector to pick one chunk among several with the same ID (FONT has three,
 * told apart by fp_Type). size 0 = first chunk with that ID. */
typedef struct {
    u32 id;
    u16 size;      /* data size to create when the chunk is missing */
    u8  selOff;
    u8  selSize;   /* 0, 1, 2 or 4 */
    s32 selVal;
} PChunkSel;

/* big-endian field access */
s32  pf_get(const u8 *p, int size, int isSigned);
void pf_put(u8 *p, int size, s32 v);

void pf_init(PFile *pf);                       /* empty, not a valid file yet */
void pf_free(PFile *pf);
int  pf_take(PFile *pf, u8 *buf, u32 len);     /* takes ownership of malloc'd buf; 0 if not FORM PREF */
int  pf_new(PFile *pf);                        /* FORM PREF + PRHD (version 0, type 0) */
int  pf_valid(const PFile *pf);

/* Chunk data of the chunk matching sel, or NULL. *size gets its data size. */
u8  *pf_find(PFile *pf, const PChunkSel *sel, u32 *size);
/* Same, but appends a zero-filled chunk (with the selector value written)
 * when it is missing. Returns NULL only on out of memory. */
u8  *pf_need(PFile *pf, const PChunkSel *sel, u32 *size);
/* The chunk's data resized to size bytes (pf_need first); NULL on out of memory. */
u8  *pf_resize(PFile *pf, const PChunkSel *sel, u32 size);
/* Every chunk with this ID removed; copied from another file; present. */
void pf_remove_id(PFile *pf, u32 id);
int  pf_copy_id(PFile *pf, const PFile *src, u32 id);
int  pf_has_id(const PFile *pf, u32 id);
/* The file rebuilt as the editors write it: PRHD of zeros, sels[] in order, every chunk of keep[]. */
int  pf_rebuild(PFile *pf, const PChunkSel *sels, int nsel, const u32 *keep);
/* The PRHD: 0 none, 1 version 0, -1 another version. */
int  pf_prhd(const PFile *pf);
/* The chunks in file order: start with *pos = 0; each call gives the next chunk's data, ID and size, NULL at
 * the end or at a chunk cut short. */
u8  *pf_next(const PFile *pf, u32 *pos, u32 *id, u32 *size);
/* whether chunk data of this size is the one sel picks */
int  pf_match(const u8 *data, u32 size, const PChunkSel *sel);

#endif
