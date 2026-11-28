/* updnet - HTTP(S) GET for the Updates module (Amiga only; see updnet.c).
 * Pure C interface: no AmigaOS or OpenSSL types, so Updates.c needs no network headers. */
#ifndef UPDNET_H
#define UPDNET_H

enum { UPD_OK = 0, UPD_ERR = -1, UPD_TIMEOUT = -2, UPD_ABORT = -3, UPD_HTTP = -4 };

typedef struct UpdNet {
    int timeout;                    /* seconds without any progress before giving up (default 30) */
    /* called several times a second while waiting and after each block read; got/total = body bytes
     * (total 0 = unknown); non-zero = abort */
    int (*poll)(void *ud, unsigned long got, unsigned long total);
    void (*log)(void *ud, const char *line);   /* one line per step, may be NULL */
    void *ud;
    char err[160];                  /* why the last call failed */
} UpdNet;

/* opens bsdsocket.library (0 = fine, else n->err says what is missing); AmiSSL is opened on the
 * first https:// connection */
int  updnet_open(UpdNet *n);
void updnet_close(UpdNet *n);       /* closes AmiSSL and bsdsocket.library */

/* GET url, following up to 5 redirects (to https:// only); the body (at most max bytes) into a
 * malloc'd buffer (*body, *len; NUL after it, not counted). UPD_HTTP: the final status was not 200
 * (*status says which); other negative codes: n->err says why. */
int updnet_get(UpdNet *n, const char *url, unsigned long max, unsigned char **body, unsigned long *len,
               int *status);

/* Updates.c (pure): "http[s]://host[:port]/path" -> parts; 1 = fine */
int upd_split_url(const char *url, int *tls, char *host, int hostlen, int *port, char *path, int pathlen);

#endif
