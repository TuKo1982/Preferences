/* updnet - HTTP(S) GET for the Updates module (AmigaOS 3, bsdsocket.library + AmiSSL 5).
 *
 * Origin: adapted from httpconn.c / httpconn.h of DropBox-Handler 2.0 by Renaud Schweingruber (the author of
 * this program, his own code reused here): non-blocking socket driven by WaitSelect, AmiSSL TLS client with
 * SNI, HTTP/1.1 status line and headers, Content-Length and chunked bodies, redirects through Location, the
 * header lookup (findHeaderValue) and the chunk parser (readBodyChunked).
 *
 * Changes from httpconn.c:
 * - plain http:// too (a local test server, ENV:UniPrefs/UpdateBase);
 * - one connection per request ("Connection: close"), so no keep-alive drain/replay; a body with neither
 *   Content-Length nor chunked framing is read until the server closes;
 * - WaitSelect in 250 ms slices: between them the caller's poll() keeps its window alive and may abort;
 *   CTRL-C to the task aborts too; n->timeout seconds without progress = time-out;
 * - TLS 1.2 at least (SSL_CTX_set_min_proto_version);
 * - redirects: at most 5, and only to https:// (a relative Location only on an https connection);
 * - bodies kept up to a maximum size; bsdsocket's errno in a variable of this file.
 * - the server certificate verified (httpconn.c used SSL_VERIFY_NONE; the owner chose verification 24.11):
 *   AmiSSL's CA store (SSL_CTX_set_default_verify_paths: the directory X509_get_default_cert_dir() names,
 *   AmiSSL:Certs in the AmiSSL 5 documentation), SSL_VERIFY_PEER, and the host name of every connection,
 *   redirects included (SSL_set1_host); any failure stops the transfer, never an unverified fallback.
 */
#include <sys/types.h>
#include <stdint.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <amitcp/socketbasetags.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <amissl/amissl.h>
#include <amissl/tags.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <proto/dos.h>

#include "updnet.h"

struct Library *SocketBase, *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;
static LONG net_errno, net_herrno;
static int ssl_ok;

/* bsdsocket's errno values (BSD) */
#define N_EINTR       4
#define N_EWOULDBLOCK 35
#define N_EINPROGRESS 36

#define SLICE_US     250000
#define MAX_REDIRECTS 5
#define MAX_HEADERS  32768
#define LINE         2048

typedef struct Conn {
    int fd;
    SSL *ssl;
    SSL_CTX *ctx;
    int tls;
    unsigned long got, total;      /* body progress, for poll() */
} Conn;

static void say(UpdNet *n, const char *fmt, const char *a, long b)
{
    char line[300];
    if (!n->log) return;
    snprintf(line, sizeof(line), fmt, a, b);
    n->log(n->ud, line);
}

static int fail(UpdNet *n, int code, const char *why)
{
    snprintf(n->err, sizeof(n->err), "%s", why);
    return code;
}

/* ---- libraries ------------------------------------------------------------ */

int updnet_open(UpdNet *n)
{
    struct TagItem t[4];
    n->err[0] = 0;
    if (!n->timeout) n->timeout = 30;
    if (SocketBase) return 0;
    if (!(SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4)))
        return fail(n, UPD_ERR, "bsdsocket.library could not be opened: start the TCP/IP stack first.");
    t[0].ti_Tag = SBTM_SETVAL(SBTC_ERRNOLONGPTR); t[0].ti_Data = (ULONG)&net_errno;
    t[1].ti_Tag = SBTM_SETVAL(SBTC_HERRNOLONGPTR); t[1].ti_Data = (ULONG)&net_herrno;
    t[2].ti_Tag = SBTM_SETVAL(SBTC_BREAKMASK); t[2].ti_Data = 0;    /* CTRL-C is handled below, not by bsdsocket */
    t[3].ti_Tag = TAG_DONE; t[3].ti_Data = 0;
    if (SocketBaseTagList(t) != 0) {
        updnet_close(n);
        return fail(n, UPD_ERR, "bsdsocket.library refused the set-up (SocketBaseTagList).");
    }
    return 0;
}

/* AmiSSL, the first time an https:// URL is used (the sequence of DropBox-Handler's net_open) */
static int ssl_up(UpdNet *n)
{
    struct TagItem t[6];
    if (ssl_ok) return 1;
    if (!(AmiSSLMasterBase = OpenLibrary((STRPTR)"amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        fail(n, UPD_ERR, "amisslmaster.library 5 could not be opened: install AmiSSL 5 for https.");
        return 0;
    }
    t[0].ti_Tag = AmiSSL_UsesOpenSSLStructs; t[0].ti_Data = FALSE;
    t[1].ti_Tag = AmiSSL_GetAmiSSLBase;      t[1].ti_Data = (ULONG)&AmiSSLBase;
    t[2].ti_Tag = AmiSSL_GetAmiSSLExtBase;   t[2].ti_Data = (ULONG)&AmiSSLExtBase;
    t[3].ti_Tag = AmiSSL_SocketBase;         t[3].ti_Data = (ULONG)SocketBase;
    t[4].ti_Tag = AmiSSL_ErrNoPtr;           t[4].ti_Data = (ULONG)&net_errno;
    t[5].ti_Tag = TAG_DONE;                  t[5].ti_Data = 0;
    if (OpenAmiSSLTagList(AMISSL_CURRENT_VERSION, t) != 0) {
        AmiSSLBase = AmiSSLExtBase = NULL;
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        fail(n, UPD_ERR, "AmiSSL could not be opened (OpenAmiSSL): is AmiSSL 5 installed?");
        return 0;
    }
    ssl_ok = 1;
    return 1;
}

void updnet_close(UpdNet *n)
{
    (void)n;
    if (ssl_ok) { CloseAmiSSL(); ssl_ok = 0; }
    AmiSSLBase = AmiSSLExtBase = NULL;
    if (AmiSSLMasterBase) { CloseLibrary(AmiSSLMasterBase); AmiSSLMasterBase = NULL; }
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
}

/* ---- waiting --------------------------------------------------------------- */

/* httpconn's waitReady, in slices: 1 = ready, else UPD_TIMEOUT / UPD_ABORT / UPD_ERR */
static int wait_ready(UpdNet *n, Conn *c, int rd, int wr)
{
    long waited = 0;
    for (;;) {
        fd_set rs, ws;
        struct timeval tv;
        ULONG sigs = SIGBREAKF_CTRL_C;
        LONG k;
        FD_ZERO(&rs);
        FD_ZERO(&ws);
        if (rd) FD_SET(c->fd, &rs);
        if (wr) FD_SET(c->fd, &ws);
        tv.tv_sec = 0;
        tv.tv_usec = SLICE_US;
        k = WaitSelect(c->fd + 1, rd ? &rs : NULL, wr ? &ws : NULL, NULL, &tv, &sigs);
        if (sigs & SIGBREAKF_CTRL_C) return fail(n, UPD_ABORT, "Stopped (CTRL-C).");
        if (k > 0) return 1;
        if (k < 0 && net_errno != N_EINTR) return fail(n, UPD_ERR, "Network error while waiting (WaitSelect).");
        if (n->poll && n->poll(n->ud, c->got, c->total)) return fail(n, UPD_ABORT, "Stopped.");
        waited += SLICE_US / 1000;
        if (waited >= (long)n->timeout * 1000) return fail(n, UPD_TIMEOUT, "No answer from the server (time-out).");
    }
}

/* ---- connection -------------------------------------------------------------- */

static void conn_close(Conn *c)
{
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
    if (c->fd >= 0) { CloseSocket(c->fd); c->fd = -1; }
    if (c->ctx) { SSL_CTX_free(c->ctx); c->ctx = NULL; }
}

/* the CA store: AmiSSL's default certificate directory must exist (SSL_CTX_set_default_verify_paths does not
 * say when it is missing, every verification would then fail as "not trusted") */
static int ca_store(UpdNet *n, SSL_CTX *ctx)
{
    const char *dir = X509_get_default_cert_dir();
    BPTR l;
    if (!dir || !*dir || !(l = Lock((STRPTR)dir, ACCESS_READ))) {
        snprintf(n->err, sizeof(n->err), "No CA store found (%s): install AmiSSL 5 with its certificates.", dir && *dir ? dir : "?");
        return UPD_ERR;
    }
    UnLock(l);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        snprintf(n->err, sizeof(n->err), "The CA store %s could not be used.", dir);
        return UPD_ERR;
    }
    return UPD_OK;
}

/* why a certificate was refused */
static int cert_fail(UpdNet *n, const char *host, long v)
{
    if (v == X509_V_ERR_HOSTNAME_MISMATCH)
        snprintf(n->err, sizeof(n->err), "Certificate name mismatch: it is not for %s.", host);
    else if (v == X509_V_ERR_CERT_NOT_YET_VALID || v == X509_V_ERR_CERT_HAS_EXPIRED)
        snprintf(n->err, sizeof(n->err), "Certificate of %s not trusted: %s (check the Amiga's clock).", host,
                 X509_verify_cert_error_string(v));
    else
        snprintf(n->err, sizeof(n->err), "Certificate of %s not trusted: %s.", host, X509_verify_cert_error_string(v));
    return UPD_ERR;
}

static int conn_open(UpdNet *n, Conn *c, const char *host, int port, int tls)
{
    struct hostent *he;
    struct sockaddr_in sa;
    long nb = 1;
    int r;
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->tls = tls;
    if (tls && !ssl_up(n)) return UPD_ERR;
    say(n, "Connecting to %s, port %ld", host, port);
    if (!(he = gethostbyname((STRPTR)host)) || !he->h_addr_list || !he->h_addr_list[0])
        return fail(n, UPD_ERR, "The server name could not be resolved (DNS).");
    if ((c->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return fail(n, UPD_ERR, "No socket (socket()).");
    if (IoctlSocket(c->fd, FIONBIO, (char *)&nb) < 0) { conn_close(c); return fail(n, UPD_ERR, "IoctlSocket(FIONBIO) failed."); }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    if (connect(c->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        long soerr = 0;
        socklen_t sl = sizeof(soerr);
        if (net_errno != N_EINPROGRESS && net_errno != N_EWOULDBLOCK) {
            conn_close(c);
            return fail(n, UPD_ERR, "The server refused the connection.");
        }
        if ((r = wait_ready(n, c, 0, 1)) != 1) { conn_close(c); return r; }
        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl) < 0 || soerr != 0) {
            conn_close(c);
            return fail(n, UPD_ERR, "The connection failed (refused or unreachable).");
        }
    }
    if (!tls) return UPD_OK;

    if (!(c->ctx = SSL_CTX_new(TLS_client_method()))) { conn_close(c); return fail(n, UPD_ERR, "SSL_CTX_new failed."); }
    SSL_CTX_set_min_proto_version(c->ctx, TLS1_2_VERSION);
    if ((r = ca_store(n, c->ctx)) != UPD_OK) { conn_close(c); return r; }
    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    SSL_CTX_set_options(c->ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);  /* a close without close_notify ends the body */
#endif
    if (!(c->ssl = SSL_new(c->ctx))) { conn_close(c); return fail(n, UPD_ERR, "SSL_new failed."); }
    SSL_set_tlsext_host_name(c->ssl, host);                     /* SNI */
    if (SSL_set1_host(c->ssl, host) != 1) { conn_close(c); return fail(n, UPD_ERR, "The host name check could not be set (SSL_set1_host)."); }
    if (SSL_set_fd(c->ssl, c->fd) != 1) { conn_close(c); return fail(n, UPD_ERR, "SSL_set_fd failed."); }
    for (;;) {
        int rc = SSL_connect(c->ssl), e;
        if (rc == 1) break;
        e = SSL_get_error(c->ssl, rc);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            if ((r = wait_ready(n, c, e == SSL_ERROR_WANT_READ, e == SSL_ERROR_WANT_WRITE)) != 1) { conn_close(c); return r; }
        } else {
            long v = SSL_get_verify_result(c->ssl);
            r = v != X509_V_OK ? cert_fail(n, host, v)
                               : fail(n, UPD_ERR, "The TLS handshake failed (TLS 1.2 or later needed).");
            conn_close(c);
            return r;
        }
    }
    {   /* SSL_VERIFY_PEER already stops the handshake on a bad certificate: checked once more, never trusted blindly */
        long v = SSL_get_verify_result(c->ssl);
        if (v != X509_V_OK) { r = cert_fail(n, host, v); conn_close(c); return r; }
    }
    say(n, "TLS connection: %s, certificate verified", SSL_get_version(c->ssl), 0);
    return UPD_OK;
}

static int conn_write(UpdNet *n, Conn *c, const char *b, int len)
{
    int sent = 0, r;
    while (sent < len) {
        if (c->tls) {
            int rc = SSL_write(c->ssl, b + sent, len - sent), e;
            if (rc > 0) { sent += rc; continue; }
            e = SSL_get_error(c->ssl, rc);
            if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) return fail(n, UPD_ERR, "Sending the request failed.");
            if ((r = wait_ready(n, c, e == SSL_ERROR_WANT_READ, e == SSL_ERROR_WANT_WRITE)) != 1) return r;
        } else {
            LONG rc = send(c->fd, (APTR)(b + sent), len - sent, 0);
            if (rc > 0) { sent += rc; continue; }
            if (rc < 0 && net_errno != N_EWOULDBLOCK) return fail(n, UPD_ERR, "Sending the request failed.");
            if ((r = wait_ready(n, c, 0, 1)) != 1) return r;
        }
    }
    return UPD_OK;
}

/* > 0 bytes read, 0 = the server closed, < 0 = UPD_* */
static int conn_read(UpdNet *n, Conn *c, char *b, int len)
{
    int r;
    for (;;) {
        if (c->tls) {
            int rc = SSL_read(c->ssl, b, len), e;
            if (rc > 0) return rc;
            e = SSL_get_error(c->ssl, rc);
            if (e == SSL_ERROR_ZERO_RETURN) return 0;
            if (e == SSL_ERROR_SYSCALL && rc == 0) return 0;      /* closed without close_notify */
            if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) return fail(n, UPD_ERR, "Reading the answer failed (TLS).");
            if ((r = wait_ready(n, c, e == SSL_ERROR_WANT_READ, e == SSL_ERROR_WANT_WRITE)) != 1) return r;
        } else {
            LONG rc = recv(c->fd, (APTR)b, len, 0);
            if (rc >= 0) return (int)rc;
            if (net_errno != N_EWOULDBLOCK) return fail(n, UPD_ERR, "Reading the answer failed.");
            if ((r = wait_ready(n, c, 1, 0)) != 1) return r;
        }
    }
}

/* ---- HTTP ---------------------------------------------------------------- */

typedef struct Buf { char *d; unsigned long len, cap; } Buf;

static int buf_add(Buf *b, const char *p, unsigned long k)
{
    if (b->len + k + 1 > b->cap) {
        unsigned long nc = b->cap ? b->cap : 4096;
        char *nd;
        while (nc < b->len + k + 1) nc *= 2;
        if (!(nd = realloc(b->d, nc))) return 0;
        b->d = nd;
        b->cap = nc;
    }
    memcpy(b->d + b->len, p, k);
    b->len += k;
    b->d[b->len] = 0;
    return 1;
}

/* httpconn.c findHeaderValue */
static int header(const char *h, int hl, const char *name, char *dst, int dl)
{
    int nl = (int)strlen(name), i = 0;
    while (i < hl) {
        int s = i, e = i, k, match = 1;
        while (e < hl && h[e] != '\n') e++;
        if (e - s > nl + 1) {
            for (k = 0; k < nl; k++) {
                char a = h[s + k], b = name[k];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (match && h[s + nl] == ':') {
                int v = s + nl + 1, ve = e, m;
                while (v < e && (h[v] == ' ' || h[v] == '\t')) v++;
                while (ve > v && (h[ve - 1] == '\r' || h[ve - 1] == ' ' || h[ve - 1] == '\t')) ve--;
                m = ve - v < dl - 1 ? ve - v : dl - 1;
                memcpy(dst, h + v, m);
                dst[m] = 0;
                return 1;
            }
        }
        i = e + 1;
    }
    return 0;
}

static int more(UpdNet *n, Conn *c, Buf *w)
{
    static char tmp[8192];
    int r = conn_read(n, c, tmp, sizeof(tmp));
    if (r <= 0) return r;
    if (!buf_add(w, tmp, r)) return fail(n, UPD_ERR, "Out of memory.");
    if (n->poll && n->poll(n->ud, c->got + (unsigned long)r, c->total)) return fail(n, UPD_ABORT, "Stopped.");
    return r;
}

static int too_big(UpdNet *n) { return fail(n, UPD_ERR, "The file is larger than expected."); }

/* httpconn.c readBodyChunked: w holds what came after the headers */
static int body_chunked(UpdNet *n, Conn *c, Buf *w, unsigned long max, Buf *out)
{
    unsigned long pos = 0;
    for (;;) {
        unsigned long i, size = 0, le = 0;
        int found = 0, digits = 0, r;
        for (i = pos; i + 1 < w->len; i++)
            if (w->d[i] == '\r' && w->d[i + 1] == '\n') { le = i; found = 1; break; }
        if (!found) {
            if ((r = more(n, c, w)) <= 0) return r ? r : fail(n, UPD_ERR, "The answer ended too early.");
            continue;
        }
        for (i = pos; i < le; i++) {
            char ch = w->d[i];
            int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else break;
            if (++digits > 7) return too_big(n);
            size = size * 16 + d;
        }
        if (!digits) return fail(n, UPD_ERR, "Bad chunked answer.");
        pos = le + 2;
        if (!size) return UPD_OK;                       /* trailers ignored: the connection closes */
        if (out->len + size > max) return too_big(n);
        while (w->len - pos < size + 2)
            if ((r = more(n, c, w)) <= 0) return r ? r : fail(n, UPD_ERR, "The answer ended too early.");
        if (!buf_add(out, w->d + pos, size)) return fail(n, UPD_ERR, "Out of memory.");
        c->got = out->len;
        pos += size + 2;
    }
}

/* one GET on an open connection: the status, the headers (malloc'd) and, for 200, the body */
static int get_once(UpdNet *n, Conn *c, const char *host, int port, const char *path, unsigned long max,
                    int *status, char **hdrs, Buf *out)
{
    char *req = malloc(LINE + 400), te[64], cl[32];
    Buf w = { NULL, 0, 0 };
    long he = -1;
    unsigned long i;
    int r, k;
    if (!req) return fail(n, UPD_ERR, "Out of memory.");
    k = snprintf(req, LINE + 400, "GET %s HTTP/1.1\r\nHost: %s", path, host);
    if (port != (c->tls ? 443 : 80)) k += snprintf(req + k, LINE + 400 - k, ":%d", port);
    k += snprintf(req + k, LINE + 400 - k, "\r\nUser-Agent: UniPrefs-Updates/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n");
    r = conn_write(n, c, req, k);
    free(req);
    if (r != UPD_OK) return r;
    while (he < 0) {
        for (i = 0; i + 3 < w.len; i++)
            if (!memcmp(w.d + i, "\r\n\r\n", 4)) { he = (long)i + 4; break; }
        if (he >= 0) break;
        if (w.len > MAX_HEADERS) { free(w.d); return fail(n, UPD_ERR, "Bad answer (headers too long)."); }
        if ((r = more(n, c, &w)) <= 0) { free(w.d); return r ? r : fail(n, UPD_ERR, "The server closed the connection."); }
    }
    /* httpconn.c parseStatusLine */
    for (i = 0; i < (unsigned long)he && w.d[i] != ' '; i++) ;
    while (i < (unsigned long)he && w.d[i] == ' ') i++;
    *status = 0;
    for (k = 0; k < 3; k++, i++) {
        if (i >= (unsigned long)he || w.d[i] < '0' || w.d[i] > '9') { free(w.d); return fail(n, UPD_ERR, "Bad answer (status line)."); }
        *status = *status * 10 + (w.d[i] - '0');
    }
    if (!(*hdrs = malloc(he + 1))) { free(w.d); return fail(n, UPD_ERR, "Out of memory."); }
    memcpy(*hdrs, w.d, he);
    (*hdrs)[he] = 0;
    if (*status != 200) { free(w.d); return UPD_OK; }

    /* the body after the headers */
    memmove(w.d, w.d + he, w.len - he);
    w.len -= he;
    if (header(*hdrs, (int)he, "Transfer-Encoding", te, sizeof(te))) {
        for (k = 0; te[k]; k++) if (te[k] >= 'A' && te[k] <= 'Z') te[k] += 32;
        if (strstr(te, "chunked")) { r = body_chunked(n, c, &w, max, out); free(w.d); return r; }
    }
    if (header(*hdrs, (int)he, "Content-Length", cl, sizeof(cl))) {
        unsigned long want = 0;
        for (k = 0; cl[k] >= '0' && cl[k] <= '9'; k++) {
            if (k >= 9) { free(w.d); return too_big(n); }
            want = want * 10 + (cl[k] - '0');
        }
        if (!k || cl[k]) { free(w.d); return fail(n, UPD_ERR, "Bad Content-Length."); }
        if (want > max) { free(w.d); return too_big(n); }
        c->total = want;
        while (w.len < want)
            if ((r = more(n, c, &w)) <= 0) { free(w.d); return r ? r : fail(n, UPD_ERR, "The answer ended too early."); }
            else c->got = w.len;
        w.len = want;                               /* anything after it ignored */
    } else {
        for (;;) {                                  /* until the server closes (Connection: close) */
            if (w.len > max) { free(w.d); return too_big(n); }
            if ((r = more(n, c, &w)) < 0) { free(w.d); return r; }
            if (!r) break;
            c->got = w.len;
        }
    }
    if (w.len > max) { free(w.d); return too_big(n); }
    if (!buf_add(out, w.d ? w.d : "", w.len)) { free(w.d); return fail(n, UPD_ERR, "Out of memory."); }
    free(w.d);
    return UPD_OK;
}

int updnet_get(UpdNet *n, const char *url, unsigned long max, unsigned char **body, unsigned long *len,
               int *status)
{
    char host[256], *path = malloc(LINE), *loc = malloc(LINE);
    int tls, port, redir, r = UPD_ERR;
    *body = NULL;
    *len = 0;
    *status = 0;
    n->err[0] = 0;
    if (!path || !loc) { free(path); free(loc); return fail(n, UPD_ERR, "Out of memory."); }
    if (!upd_split_url(url, &tls, host, sizeof(host), &port, path, LINE)) {
        free(path); free(loc);
        return fail(n, UPD_ERR, "Bad URL (http:// or https:// expected).");
    }
    for (redir = 0; ; redir++) {
        Conn c;
        Buf out = { NULL, 0, 0 };
        char *hdrs = NULL;
        if ((r = conn_open(n, &c, host, port, tls)) != UPD_OK) break;
        r = get_once(n, &c, host, port, path, max, status, &hdrs, &out);
        conn_close(&c);
        if (r != UPD_OK) { free(hdrs); free(out.d); break; }
        if (*status == 301 || *status == 302 || *status == 303 || *status == 307 || *status == 308) {
            int ok = hdrs && header(hdrs, (int)strlen(hdrs), "Location", loc, LINE);
            free(hdrs);
            free(out.d);
            if (!ok) { r = fail(n, UPD_ERR, "Redirect without a Location."); break; }
            if (redir >= MAX_REDIRECTS) { r = fail(n, UPD_ERR, "Too many redirects."); break; }
            if (loc[0] == '/') {
                if (!tls) { r = fail(n, UPD_ERR, "Redirect refused: https only."); break; }
                snprintf(path, LINE, "%s", loc);
            } else if (strncmp(loc, "https://", 8) || !upd_split_url(loc, &tls, host, sizeof(host), &port, path, LINE)) {
                r = fail(n, UPD_ERR, "Redirect refused: https only.");
                break;
            }
            say(n, "Redirected to %s (%ld)", host, *status);
            continue;
        }
        free(hdrs);
        if (*status != 200) { free(out.d); r = UPD_HTTP; snprintf(n->err, sizeof(n->err), "The server answered %d.", *status); break; }
        *body = (unsigned char *)out.d;
        *len = out.len;
        if (!*body && (*body = malloc(1)) != NULL) **body = 0;
        r = UPD_OK;
        break;
    }
    free(path);
    free(loc);
    return r;
}
