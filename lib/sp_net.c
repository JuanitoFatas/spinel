/* sp_net.c -- POSIX TCP / poll / process / shell primitives.
 * See sp_net.h. Pure C, no spinel-runtime dependency; no OpenSSL.
 *
 * Extracted from tep's lib/tep/sphttp.c (the POSIX-runtime core that
 * is generic across HTTP-shaped Spinel programs), per matz/spinel#1054
 * and OriPekelman/tep#12. HTTP framing + WebSocket accessors + TLS stay
 * framework-side.
 *
 * sp_net is a POSIX prefork/poll runtime (sockets, poll(2), fork,
 * sigaction). */
#include "sp_net.h"
#include "sp_alloc.h"   /* sp_ffi_bin_len: the binary-safe return contract */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/un.h>   /* UNIXSocket / UNIXServer */

#define SP_NET_BUFSIZE 65536

/* ---------- graceful shutdown ---------- */

static volatile sig_atomic_t sp_net_term_flag = 0;
/* Self-pipe: the term handler writes a byte here so a blocked accept (via
 * poll) wakes even when the signal lands in the check-then-accept window. */
static int sp_net_sigpipe[2] = {-1, -1};

static void sp_net_term_signal(int sig) {
    (void)sig;
    sp_net_term_flag = 1;
    if (sp_net_sigpipe[1] >= 0) {
        ssize_t w = write(sp_net_sigpipe[1], "x", 1);  /* write() is async-signal-safe */
        (void)w;
    }
}

int sp_net_install_term_handlers(void) {
    if (sp_net_sigpipe[0] < 0 && pipe(sp_net_sigpipe) == 0) {
        for (int i = 0; i < 2; i++) {
            int fl = fcntl(sp_net_sigpipe[i], F_GETFL, 0);
            if (fl >= 0) fcntl(sp_net_sigpipe[i], F_SETFL, fl | O_NONBLOCK);
            int fd = fcntl(sp_net_sigpipe[i], F_GETFD, 0);
            if (fd >= 0) fcntl(sp_net_sigpipe[i], F_SETFD, fd | FD_CLOEXEC);
        }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sp_net_term_signal;
    sigemptyset(&sa.sa_mask);
    /* No SA_RESETHAND and no SA_RESTART: a repeated term signal must keep
     * hitting this handler (idempotent flag set) rather than the default
     * action -- the latter killed the process with 143 instead of letting
     * the accept loop exit cleanly. Without SA_RESTART, a signal during a
     * blocked accept/poll surfaces as EINTR. */
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    return 0;
}

int sp_net_shutdown_requested(void) {
    return (int)sp_net_term_flag;
}

/* Byte count written into the recv buffer by the most recent recv. The FFI
 * `:binstr` return mode reads this to build a binary-safe String of exactly
 * this many bytes (rather than strlen-truncating at the first NUL), so the same
 * recv functions serve both text (`:str`) and binary (`:binstr`) callers.
 * Single-threaded (the Fiber model is cooperative), so no locking is needed. */

/* Disable Nagle on a connection fd. TCP_NODELAY is a per-connection option, so
 * it must be set on each accepted fd, not just the listener -- otherwise small
 * writes (response headers, WebSocket frames) stall against delayed-ACK. */
void sp_net_set_nodelay(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

/* ---------- TCP socket lifecycle ---------- */

/* TCPServer.new(host, port): like sp_net_listen but bound to a specific
   address, resolved through getaddrinfo (so "localhost" works). A NULL/empty
   host binds INADDR_ANY. Returns the listening fd, or -1. (#2922) */
int sp_net_listen_host(const char *host, int port, int backlog) {
    if (port < 0 || port > 65535) return -1;
    signal(SIGPIPE, SIG_IGN);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo((host && *host) ? host : NULL, portbuf, &hints, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
            listen(fd, backlog > 0 ? backlog : 1024) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* The local port a bound/listening fd ended up on (port 0 => ephemeral). */
int sp_net_local_port(int fd) {
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (getsockname(fd, (struct sockaddr *)&ss, &len) != 0) return -1;
    if (ss.ss_family == AF_INET)  return (int)ntohs(((struct sockaddr_in *)&ss)->sin_port);
    if (ss.ss_family == AF_INET6) return (int)ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
    return -1;
}

/* The local/peer IP of a socket fd as text into ipbuf; returns the port, or
   -1. peer != 0 reads the remote end (getpeername). */
int sp_net_sock_ip(int fd, int peer, char *ipbuf, int cap) {
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    int r = peer ? getpeername(fd, (struct sockaddr *)&ss, &len)
                 : getsockname(fd, (struct sockaddr *)&ss, &len);
    if (r != 0) return -1;
    if (ss.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        if (!inet_ntop(AF_INET, &a->sin_addr, ipbuf, (socklen_t)cap)) return -1;
        return (int)ntohs(a->sin_port);
    }
    if (ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        if (!inet_ntop(AF_INET6, &a->sin6_addr, ipbuf, (socklen_t)cap)) return -1;
        return (int)ntohs(a->sin6_port);
    }
    return -1;
}

int sp_net_listen(int port, int reuseport) {
    if (port < 0 || port > 65535) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    if (reuseport) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    }
#endif
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    /* Don't die on a write to a peer that closed. */
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 1024) < 0) { close(fd); return -1; }
    return fd;
}

#ifdef SP_THREADS
int sp_sched_wait_io(int fd, short events);   /* lib/sp_sched.c: scheduler-aware I/O wait */
#endif
int sp_net_accept(int sfd) {
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int fd;
#ifdef SP_THREADS
    /* Scheduler-aware accept: the listen fd is non-blocking, so accept returns
       EAGAIN when no connection is pending and the green thread parks (freeing
       its worker) until the monitor's poll reports the fd readable. Idempotent. */
    sp_net_set_nonblock(sfd);
    for (;;) {
        if (sp_net_term_flag) return -1;
        fd = accept(sfd, (struct sockaddr *)&caddr, &clen);
        /* The connection fd is non-blocking too, so recv/send on it park the
           green thread (scheduler-aware) instead of blocking the OS worker. */
        if (fd >= 0) { sp_net_set_nodelay(fd); sp_net_set_nonblock(fd); return fd; }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!sp_sched_wait_io(sfd, POLLIN)) return -1;
            continue;
        }
        return -1;
    }
#else
    for (;;) {
        if (sp_net_term_flag) return -1;
        /* Wait for an incoming connection OR a term signal (which wakes the
         * self-pipe). Polling first closes the check-then-block race: a
         * signal that set the flag/pipe before we block makes poll return
         * immediately, so we never enter a blocking accept with the flag
         * already set. */
        struct pollfd pfds[2];
        pfds[0].fd = sfd;               pfds[0].events = POLLIN; pfds[0].revents = 0;
        nfds_t npfd = 1;
        if (sp_net_sigpipe[0] >= 0) {
            pfds[1].fd = sp_net_sigpipe[0]; pfds[1].events = POLLIN; pfds[1].revents = 0;
            npfd = 2;
        }
        int pr = poll(pfds, npfd, -1);
        if (sp_net_term_flag) return -1;
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (!(pfds[0].revents & POLLIN)) continue;  /* woken by the self-pipe alone */
        fd = accept(sfd, (struct sockaddr *)&caddr, &clen);
        if (fd >= 0) { sp_net_set_nodelay(fd); return fd; }
        if (errno == EINTR) {
            if (sp_net_term_flag) return -1;
            continue;   /* unrelated signal (SIGCHLD, ...) -- retry */
        }
        return -1;
    }
#endif
}

int sp_net_accept_nb(int sfd) {
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int fd;
    do {
        fd = accept(sfd, (struct sockaddr *)&caddr, &clen);
    } while (fd < 0 && errno == EINTR);
    if (fd >= 0) sp_net_set_nodelay(fd);
    return fd;
}

/* ---- UDP and UNIX-domain sockets ---- */

/* An unbound UDP socket; #bind attaches it to a local address later. */
int sp_net_udp_open(int family) {
    signal(SIGPIPE, SIG_IGN);
    return socket(family > 0 ? family : AF_INET, SOCK_DGRAM, 0);
}
/* The family the fd was opened with. An address resolved for the other family
   cannot be bound or sent to, so every UDP call resolves within it. */
int sp_net_fd_family(int fd) {
    struct sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    if (fd < 0 || getsockname(fd, (struct sockaddr *)&ss, &sl) != 0) return AF_UNSPEC;
    return (int)ss.ss_family;
}
/* First resolution matching `family` (any, when AF_UNSPEC). */
static struct addrinfo *sp_net_udp_resolve(int fd, const char *host, int port,
                                           int passive, struct addrinfo **head) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = sp_net_fd_family(fd);
    hints.ai_socktype = SOCK_DGRAM;
    if (passive) hints.ai_flags = AI_PASSIVE;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    *head = NULL;
    if (getaddrinfo((host && *host) ? host : NULL, portbuf, &hints, head) != 0) return NULL;
    return *head;
}
int sp_net_udp_bind(int fd, const char *host, int port) {SP_GC_ROOT_STR(host);
    if (fd < 0 || port < 0 || port > 65535) return -1;
    struct addrinfo *head = NULL;
    struct addrinfo *ai = sp_net_udp_resolve(fd, host, port, (host && *host) ? 0 : 1, &head);
    if (!ai) return -1;
    int rc = bind(fd, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(head);
    return rc;
}
/* #connect fixes the peer so #send / #write need no address. */
int sp_net_udp_connect(int fd, const char *host, int port) {SP_GC_ROOT_STR(host);
    if (fd < 0 || port < 0 || port > 65535) return -1;
    struct addrinfo *head = NULL;
    struct addrinfo *ai = sp_net_udp_resolve(fd, host, port, 0, &head);
    if (!ai) return -1;
    int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(head);
    return rc;
}
int sp_net_udp_send_to(int fd, const char *data, int len, const char *host, int port) {SP_GC_ROOT_STR(data);SP_GC_ROOT_STR(host);
    if (fd < 0) return -1;
    if (!host || !*host) return (int)send(fd, data, (size_t)len, 0);
    struct addrinfo *head = NULL;
    struct addrinfo *ai = sp_net_udp_resolve(fd, host, port, 0, &head);
    if (!ai) return -1;
    int rc = (int)sendto(fd, data, (size_t)len, 0, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(head);
    return rc;
}
/* Reads one datagram into `buf`; fills the sender's IP/port when asked.
   Returns the byte count, or -1. */
int sp_net_udp_recv_from(int fd, char *buf, int cap, char *ipbuf, int ipcap, int *port_out) {
    if (fd < 0 || cap <= 0) return -1;
    struct sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    ssize_t n;
    do { n = recvfrom(fd, buf, (size_t)cap, 0, (struct sockaddr *)&ss, &sl); }
    while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    if (ipbuf && ipcap > 0) {
        ipbuf[0] = '\0';
        if (ss.ss_family == AF_INET) {
            struct sockaddr_in *a = (struct sockaddr_in *)&ss;
            inet_ntop(AF_INET, &a->sin_addr, ipbuf, (socklen_t)ipcap);
            if (port_out) *port_out = ntohs(a->sin_port);
        }
        else if (ss.ss_family == AF_INET6) {
            struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
            inet_ntop(AF_INET6, &a->sin6_addr, ipbuf, (socklen_t)ipcap);
            if (port_out) *port_out = ntohs(a->sin6_port);
        }
        else if (port_out) *port_out = 0;
    }
    return (int)n;
}

/* UNIX-domain stream sockets. `path` is a filesystem path; a server unlinks a
   stale socket file first, as CRuby's UNIXServer does not (it raises) -- so we
   do not either, and bind fails on a leftover file. */
static int sp_net_unix_addr(const char *path, struct sockaddr_un *sa) {
    if (!path || !*path) return -1;
    size_t n = strlen(path);
    if (n >= sizeof sa->sun_path) return -1;
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    memcpy(sa->sun_path, path, n + 1);
    return 0;
}
int sp_net_unix_listen(const char *path, int backlog) {SP_GC_ROOT_STR(path);
    struct sockaddr_un sa;
    if (sp_net_unix_addr(path, &sa) != 0) return -1;
    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); return -1; }
    if (listen(fd, backlog > 0 ? backlog : 128) != 0) { close(fd); return -1; }
    return fd;
}
int sp_net_unix_connect(const char *path) {SP_GC_ROOT_STR(path);
    struct sockaddr_un sa;
    if (sp_net_unix_addr(path, &sa) != 0) return -1;
    signal(SIGPIPE, SIG_IGN);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); return -1; }
    return fd;
}
/* The bound path of a UNIX socket (empty for an unnamed peer). */
int sp_net_unix_path(int fd, int peer, char *buf, int cap) {
    struct sockaddr_un sa;
    socklen_t sl = sizeof sa;
    memset(&sa, 0, sizeof sa);
    int rc = peer ? getpeername(fd, (struct sockaddr *)&sa, &sl)
                  : getsockname(fd, (struct sockaddr *)&sa, &sl);
    if (rc != 0 || cap <= 0) { if (cap > 0) buf[0] = '\0'; return -1; }
    snprintf(buf, (size_t)cap, "%s", sa.sun_path);
    return 0;
}

/* ---- Socket class methods ---- */
int sp_net_gethostname(char *buf, int cap) {
    if (!buf || cap <= 0) return -1;
    buf[0] = '\0';
    return gethostname(buf, (size_t)cap);
}
/* A connected pair of the given domain/type; fds[0]/fds[1] on success. */
int sp_net_socketpair(int domain, int type, int protocol, int fds[2]) {
    signal(SIGPIPE, SIG_IGN);
    return socketpair(domain > 0 ? domain : AF_UNIX,
                      type > 0 ? type : SOCK_STREAM, protocol, fds);
}
int sp_net_socket(int domain, int type, int protocol) {
    signal(SIGPIPE, SIG_IGN);
    return socket(domain, type, protocol);
}
/* getaddrinfo, one resolution at a time: `idx` selects the entry so the caller
   can walk them without owning the addrinfo list. Fills family/socktype/
   protocol/ip/port; returns 0 on success, -1 past the end or on failure. */
int sp_net_getaddrinfo_at(const char *host, int port, int socktype, int idx,
                          int *family, int *stype, int *proto,
                          char *ipbuf, int ipcap, int *port_out) {
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = socktype > 0 ? socktype : 0;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo((host && *host) ? host : NULL, portbuf, &hints, &res) != 0) return -1;
    int i = 0, rc = -1;
    for (ai = res; ai; ai = ai->ai_next, i++) {
        if (i != idx) continue;
        if (family) *family = ai->ai_family;
        if (stype)  *stype  = ai->ai_socktype;
        if (proto)  *proto  = ai->ai_protocol;
        if (ipbuf && ipcap > 0) {
            ipbuf[0] = '\0';
            if (ai->ai_family == AF_INET) {
                struct sockaddr_in *a = (struct sockaddr_in *)ai->ai_addr;
                inet_ntop(AF_INET, &a->sin_addr, ipbuf, (socklen_t)ipcap);
                if (port_out) *port_out = ntohs(a->sin_port);
            }
            else if (ai->ai_family == AF_INET6) {
                struct sockaddr_in6 *a = (struct sockaddr_in6 *)ai->ai_addr;
                inet_ntop(AF_INET6, &a->sin6_addr, ipbuf, (socklen_t)ipcap);
                if (port_out) *port_out = ntohs(a->sin6_port);
            }
            else if (port_out) *port_out = 0;
        }
        rc = 0;
        break;
    }
    freeaddrinfo(res);
    return rc;
}

/* ---- socket options ---- */
/* getsockopt/setsockopt over the integer-valued options, which is every option
   Ruby programs reach for in practice (SO_REUSEADDR, SO_KEEPALIVE,
   TCP_NODELAY, SO_RCVBUF, ...). Returns -1 on failure. */
int sp_net_setsockopt_int(int fd, int level, int optname, int value) {
    if (fd < 0) return -1;
    return setsockopt(fd, level, optname, &value, (socklen_t)sizeof value);
}
int sp_net_getsockopt_int(int fd, int level, int optname) {
    int value = 0;
    socklen_t len = (socklen_t)sizeof value;
    if (fd < 0 || getsockopt(fd, level, optname, &value, &len) != 0) return -1;
    return value;
}
int sp_net_shutdown(int fd, int how) {
    if (fd < 0) return -1;
    return shutdown(fd, how);
}

int sp_net_connect(const char *host, int port) {
    if (port < 0 || port > 65535) return -1;
    /* A client that never listens still needs SIGPIPE ignored: a write
     * to an upstream that closed would otherwise kill the process. */
    signal(SIGPIPE, SIG_IGN);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) return -1;

    int fd = -1;
    struct addrinfo *ai;
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SP_THREADS
    /* Non-blocking so recv/send park the green thread instead of blocking the
       worker (the connect itself stays blocking -- it completes promptly). */
    sp_net_set_nonblock(fd);
#endif
    return fd;
}

int sp_net_close(int fd) {
    return close(fd);
}

int sp_net_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ---------- TCP I/O ---------- */

/* Wait until `fd` is ready for `events` (POLLIN for a read, POLLOUT for a
   write) before retrying a syscall that returned EAGAIN/EWOULDBLOCK. The
   accepted client fds are non-blocking, so a recv/send can would-block even
   after the scheduler's poll reported the fd ready (a transient kernel state,
   a partial drain, etc.); treating that as EOF/error wrongly tears down a live
   connection (#1500). Block on the single fd rather than busy-spinning, and
   recheck the term flag on the 1s timeout so a shutdown still breaks the wait.
   Returns 1 to retry the syscall, 0 to give up (term requested or poll error). */
static int sp_net_wait_io(int fd, short events) {
    if (sp_net_term_flag) return 0;
#ifdef SP_THREADS
    /* Scheduler-aware: park this green thread and hand its OS worker to another
       thread until the fd is ready, rather than blocking the worker in poll. */
    return sp_sched_wait_io(fd, events);
#else
    for (;;) {
        if (sp_net_term_flag) return 0;
        struct pollfd pf;
        pf.fd = fd; pf.events = events; pf.revents = 0;
        int pr = poll(&pf, 1, 1000);
        if (pr > 0) return 1;
        if (pr == 0) continue;            /* timeout: recheck term, keep waiting */
        if (errno == EINTR) continue;
        return 0;
    }
#endif
}

int sp_net_write_str(int fd, const char *s) {
    size_t len = strlen(s);
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, s + off, len - off, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && sp_net_wait_io(fd, POLLOUT)) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

int sp_net_write_bytes(int fd, const char *data, int n) {
    size_t total = (n < 0) ? 0 : (size_t)n;
    size_t off = 0;
    while (off < total) {
        ssize_t w = send(fd, data + off, total - off, 0);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && sp_net_wait_io(fd, POLLOUT)) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/* Per-worker in the threaded build: Thread runs with real parallelism and no
 * GVL, so two green threads on two OS workers calling this concurrently would
 * otherwise hand back the same buffer and race on its contents. Allocated on
 * first use rather than reserved in every thread's TLS block, so a worker that
 * never touches a socket pays nothing (SP_MAX_WORKERS is 256, and three 64 KB
 * buffers reserved unconditionally would be 48 MB of TLS). */
static SP_TLS char *sp_net_recv_buf;
const char *sp_net_recv_some(int fd, int maxlen) {
    if (!sp_net_recv_buf && !(sp_net_recv_buf = (char *)malloc(SP_NET_BUFSIZE)))
        return "";
    if (maxlen <= 0 || maxlen >= SP_NET_BUFSIZE) maxlen = SP_NET_BUFSIZE - 1;
    ssize_t n;
    for (;;) {
        /* Cooperative shutdown: bail rather than retry into the signal. */
        if (sp_net_term_flag) {
            sp_ffi_bin_len = 0;
            sp_net_recv_buf[0] = '\0';
            return sp_net_recv_buf;
        }
        n = recv(fd, sp_net_recv_buf, (size_t)maxlen, 0);
        if (n >= 0) break;
        if (errno == EINTR) continue;   /* e.g. SIGCHLD in a prefork server */
        /* Non-blocking fd would-block: wait for readability and retry rather
           than reporting an empty result the caller can't tell from EOF. */
        if ((errno == EAGAIN || errno == EWOULDBLOCK) && sp_net_wait_io(fd, POLLIN)) continue;
        sp_ffi_bin_len = 0;
        sp_net_recv_buf[0] = '\0';
        return sp_net_recv_buf;
    }
    sp_ffi_bin_len = (int)n;            /* exact byte count for the :binstr path */
    sp_net_recv_buf[n] = '\0';
    return sp_net_recv_buf;
}

static SP_TLS char *sp_net_recv_all_buf;   /* per-worker, see sp_net_recv_buf */
const char *sp_net_recv_all(int fd, int max_bytes) {
    if (!sp_net_recv_all_buf && !(sp_net_recv_all_buf = (char *)malloc(SP_NET_BUFSIZE)))
        return "";
    if (max_bytes <= 0 || max_bytes >= SP_NET_BUFSIZE) max_bytes = SP_NET_BUFSIZE - 1;
    int total = 0;
    while (total < max_bytes) {
        if (sp_net_term_flag) break;
        ssize_t n = recv(fd, sp_net_recv_all_buf + total, (size_t)(max_bytes - total), 0);
        if (n < 0) {
            if (errno == EINTR) continue;   /* retry rather than truncate */
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && sp_net_wait_io(fd, POLLIN)) continue;
            break;
        }
        if (n == 0) break;                   /* clean EOF */
        total += (int)n;
    }
    sp_ffi_bin_len = total;              /* exact byte count for the :binstr path */
    sp_net_recv_all_buf[total] = '\0';
    return sp_net_recv_all_buf;
}

/* ---------- poll(2) ---------- */

/* The set grows with the caller's connection count rather than sitting at a
 * fixed size. It used to be a 256-entry array whose overflow answered -1, and
 * -1 is also what a caller reads as "this fd is not in the set this round":
 * from the 257th fd on, a connection went deaf with nothing said about it.
 * A server holding one socket per user reaches 256 on its first busy minute.
 *
 * Growth doubles from 64. The only remaining failure is the allocator saying
 * no, which is not something a caller can route around, so it is reported
 * rather than returned quietly. */
#define SP_NET_POLL_INIT 64
static struct pollfd *sp_net_poll_set;
static int            sp_net_poll_n   = 0;
static int            sp_net_poll_cap = 0;

int sp_net_poll_reset(void) {
    sp_net_poll_n = 0;
    return 0;
}

int sp_net_poll_add(int fd, int mode_bits) {
    if (sp_net_poll_n >= sp_net_poll_cap) {
        int cap = sp_net_poll_cap ? sp_net_poll_cap * 2 : SP_NET_POLL_INIT;
        struct pollfd *grown =
            (struct pollfd *)realloc(sp_net_poll_set, (size_t)cap * sizeof *grown);
        if (!grown) {
            fprintf(stderr, "sp_net_poll_add: out of memory growing the poll set "
                            "to %d entries; fd %d is not being watched\n", cap, fd);
            return -1;
        }
        sp_net_poll_set = grown;
        sp_net_poll_cap = cap;
    }
    short ev = 0;
    if (mode_bits & 1) ev |= POLLIN;
    if (mode_bits & 2) ev |= POLLOUT;
    sp_net_poll_set[sp_net_poll_n].fd      = fd;
    sp_net_poll_set[sp_net_poll_n].events  = ev;
    sp_net_poll_set[sp_net_poll_n].revents = 0;
    return sp_net_poll_n++;
}

int sp_net_poll_run(int timeout_ms) {
    int r;
    for (;;) {
        /* Don't retry into a pending shutdown -- surface it as -1 so a
         * blocked scheduler tick can break and run shutdown hooks. */
        if (sp_net_term_flag) return -1;
        r = poll(sp_net_poll_set, sp_net_poll_n, timeout_ms);
        if (r >= 0) return r;
        if (errno == EINTR) {
            if (sp_net_term_flag) return -1;
            continue;
        }
        return -1;
    }
}

int sp_net_poll_ready(int slot) {
    if (slot < 0 || slot >= sp_net_poll_n) return 0;
    short rev = sp_net_poll_set[slot].revents;
    int out = 0;
    if (rev & (POLLIN | POLLHUP | POLLERR)) out |= 1;
    if (rev & POLLOUT)                      out |= 2;
    return out;
}

/* ---------- process (prefork) ---------- */

int sp_net_fork(void) {
    return (int)fork();
}

int sp_net_exit(int status) {
    _exit(status);
    return 0;   /* unreachable */
}

int sp_net_getpid(void) {
    return (int)getpid();
}

int sp_net_wait_any(void) {
    int status = 0;
    pid_t p = wait(&status);
    return (int)p;
}

/* ---------- shell ---------- */

static SP_TLS char *sp_net_shell_buf;      /* per-worker, see sp_net_recv_buf */
const char *sp_net_shell_capture(const char *cmd, int max_bytes) {
    if (!sp_net_shell_buf && !(sp_net_shell_buf = (char *)malloc(SP_NET_BUFSIZE)))
        return "";
    if (max_bytes <= 0 || max_bytes >= SP_NET_BUFSIZE) max_bytes = SP_NET_BUFSIZE - 1;
    sp_net_shell_buf[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp) return sp_net_shell_buf;
    size_t total = 0;
    while (total < (size_t)max_bytes) {
        size_t n = fread(sp_net_shell_buf + total, 1, (size_t)max_bytes - total, fp);
        if (n == 0) break;
        total += n;
    }
    sp_ffi_bin_len = (int)total;        /* exact byte count for the :binstr path */
    sp_net_shell_buf[total] = '\0';
    pclose(fp);
    return sp_net_shell_buf;
}
