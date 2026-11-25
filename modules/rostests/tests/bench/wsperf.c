/*
 * PROJECT:     LiberNT rostests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Socket throughput, connect-rate and stress measurement
 */

/* wsperf: minimal winsock throughput / connect-rate tool
 *   wsperf t <host> <port> <mb> [e]    TCP send <mb> MiB; 'e' = also read echo back
 *   wsperf c <host> <port> <count>     <count> sequential connects
 *   wsperf r <host> <port> <count>     <count> connects each sending 64 KiB then close
 *   wsperf d <host> <port>             receive until close; reports RX rate
 *   wsperf u <host> <port> <count>     <count> UDP datagrams, echo each back; reports loss
 *   wsperf m <host> <port> <n> <mb>    <n> concurrent threads each sending <mb> MiB
 *   wsperf b <host> <port> <count>     <count> connects closed abortively (RST)
 *   wsperf s <host> <port> <count>     <count> connects, 4 KiB select() round-trip each
 *   wsperf p <host> <port> <count>     <count> connects, MSG_PEEK verify each
 *   wsperf f <host> <port> <count>     <count> connects to a closed port; wants WSAECONNREFUSED
 *   wsperf x <host> <port>             UDP send to a closed port, recv wants WSAECONNRESET
 *   wsperf l <port>                    sink: accept and discard (server mode)
 *   wsperf q <port>                    echo: accept and echo back (server mode)
 *   wsperf k <port>                    kernel keepalive loop test (server mode)
 * prints single-line CSV-ish results via printf (CRT stdout - survives pipes)
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <windows.h>
#include <stdarg.h>

static void dbg(const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(b, sizeof(b) - 1, fmt, ap);
    va_end(ap);
    b[sizeof(b) - 1] = 0;
    OutputDebugStringA(b);
}

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static void set_touts(SOCKET s, DWORD ms)
{
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&ms, sizeof(ms));
}

static SOCKET conn(const char *host, int port)
{
    struct sockaddr_in a;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr(host);
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR)
    {
        int e = WSAGetLastError();
        closesocket(s);
        printf("connect_err=%d\n", e);
        return INVALID_SOCKET;
    }
    return s;
}

typedef struct { SOCKET s; volatile long long recvd; volatile int done; } dr_arg;

static void __cdecl drainer(void *p)
{
    dr_arg *a = p;
    char buf[65536];
    int n;
    dbg("drainer start\n");
    while ((n = recv(a->s, buf, sizeof(buf), 0)) > 0)
        a->recvd += n;
    a->done = 1;
    dbg("drainer exit n=%d err=%d recvd=%lld\n", n, WSAGetLastError(), a->recvd);
}

static int tp(const char *host, int port, long mb, int echo)
{
    SOCKET s = conn(host, port);
    static char buf[65536];
    long long total = (long long)mb << 20, sent = 0;
    double t0, t1;
    dr_arg da;
    HANDLE th = NULL;
    if (s == INVALID_SOCKET) return 1;
    set_touts(s, 30000);
    da.s = s; da.recvd = 0; da.done = 0;
    if (echo)
        th = (HANDLE)_beginthread(drainer, 0, &da);
    t0 = now_ms();
    while (sent < total)
    {
        int n = send(s, buf, (int)((total - sent) > (long long)sizeof(buf) ? (long long)sizeof(buf) : (total - sent)), 0);
        if (n <= 0) { printf("send_err=%d at %lld\n", WSAGetLastError(), sent); break; }
        sent += n;
        if ((sent & 0xFFFFF) == 0) dbg("tp sent=%lldM drained=%lld done=%d\n", sent >> 20, da.recvd, da.done);
    }
    t1 = now_ms();
    dbg("tp sendloop out sent=%lld drained=%lld done=%d\n", sent, da.recvd, da.done);
    printf("tx %lld bytes %.0f ms %.2f MB/s\n", sent, t1 - t0, sent / 1048576.0 / ((t1 - t0) / 1000.0));
    if (echo)
    {
        shutdown(s, SD_SEND);
        dbg("tp post-shutdown recvd=%lld\n", da.recvd);
        WaitForSingleObject(th, 40000);
        t1 = now_ms();
        printf("rx %lld bytes done=%d %.0f ms\n", da.recvd, da.done, t1 - t0);
        CloseHandle(th);
    }
    closesocket(s);
    return 0;
}

static int cr(const char *host, int port, int count)
{
    int i, ok = 0, fail = 0;
    double t0 = now_ms(), t1;
    for (i = 0; i < count; i++)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in a;
        if (s == INVALID_SOCKET) { fail++; continue; }
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)port);
        a.sin_addr.s_addr = inet_addr(host);
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR)
            fail++;
        else
            ok++;
        closesocket(s);
    }
    t1 = now_ms();
    printf("connects ok=%d fail=%d %.0f ms %.1f conn/s\n", ok, fail, t1 - t0,
           ok / ((t1 - t0) / 1000.0));
    return fail ? 1 : 0;
}

static int dl(const char *host, int port)
{
    SOCKET s = conn(host, port);
    static char buf[65536];
    long long recvd = 0;
    double t0, t1;
    if (s == INVALID_SOCKET) return 1;
    set_touts(s, 30000);
    t0 = now_ms();
    for (;;)
    {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        recvd += n;
    }
    t1 = now_ms();
    printf("rx %lld bytes %.0f ms %.2f MB/s\n", recvd, t1 - t0,
           recvd / 1048576.0 / ((t1 - t0) / 1000.0));
    closesocket(s);
    return 0;
}

static int srv(int port)
{
    struct sockaddr_in a;
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), c;
    static char buf[65536];
    long long recvd = 0;
    if (l == INVALID_SOCKET) { printf("socket_err\n"); return 1; }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) || listen(l, 4))
    {
        printf("bind/listen_err=%d\n", WSAGetLastError());
        return 1;
    }
    printf("listening %d\n", port);
    for (;;)
    {
        c = accept(l, NULL, NULL);
        if (c == INVALID_SOCKET) { printf("accept_err=%d\n", WSAGetLastError()); return 1; }
        for (;;)
        {
            int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            recvd += n;
        }
        closesocket(c);
        printf("sunk %lld\n", recvd);
    }
    return 0;
}

/* q: echo listener — accept connections, echo back everything received */
static int esrv(int port)
{
    struct sockaddr_in a;
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), c;
    static char buf[65536];
    if (l == INVALID_SOCKET) { printf("socket_err\n"); return 1; }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) || listen(l, 4))
    {
        printf("bind/listen_err=%d\n", WSAGetLastError());
        return 1;
    }
    printf("echo listening %d\n", port);
    for (;;)
    {
        c = accept(l, NULL, NULL);
        if (c == INVALID_SOCKET) { printf("accept_err=%d\n", WSAGetLastError()); return 1; }
        for (;;)
        {
            int n = recv(c, buf, sizeof(buf), 0), off = 0;
            if (n <= 0) break;
            while (off < n)
            {
                int m = send(c, buf + off, n - off, 0);
                if (m <= 0) break;
                off += m;
            }
            if (off < n) break;
        }
        closesocket(c);
    }
    return 0;
}

/* u: UDP echo — N datagrams to host:port, recv each reply; reports loss rate */
static int ue(const char *host, int port, int count)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a;
    int i, sent = 0, got = 0, lost = 0, err = 0;
    char buf[1400], rbuf[1500];
    double t0, t1;
    DWORD tv = 3000;
    if (s == INVALID_SOCKET) { printf("socket_err\n"); return 1; }
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr(host);
    memset(buf, 0x5a, sizeof(buf));
    t0 = now_ms();
    for (i = 0; i < count; i++)
    {
        int n = sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&a, sizeof(a));
        if (n != sizeof(buf)) { err++; continue; }
        sent++;
        n = recv(s, rbuf, sizeof(rbuf), 0);
        if (n > 0) got++;
        else lost++;
    }
    t1 = now_ms();
    printf("udp sent=%d echo=%d lost=%d err=%d %.0f ms %.1f dgram/s\n",
           sent, got, lost, err, t1 - t0, sent / ((t1 - t0) / 1000.0));
    closesocket(s);
    return (err || lost) ? 1 : 0;
}

/* f: refused connects — N connects to a closed port; expect WSAECONNREFUSED */
static int refused(const char *host, int port, int count)
{
    int i, refused_ok = 0, other = 0;
    double t0 = now_ms(), t1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr(host);
    for (i = 0; i < count; i++)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) { other++; continue; }
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAECONNREFUSED) refused_ok++;
            else { printf("unexpected_err=%d\n", WSAGetLastError()); other++; }
        }
        else other++;
        closesocket(s);
    }
    t1 = now_ms();
    printf("refused ok=%d other=%d %.0f ms %.1f/s\n", refused_ok, other, t1 - t0,
           refused_ok / ((t1 - t0) / 1000.0));
    return other ? 1 : 0;
}

/* x: error paths — UDP to closed port then recv (want WSAECONNRESET);
   then send on a shut socket (want WSAESHUTDOWN) */
static int errpaths(const char *host, int port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a;
    int ok = 0;
    char buf[64] = {0};
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr(host);
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR)
    {
        printf("udp_connect_err=%d\n", WSAGetLastError());
        return 1;
    }
    send(s, buf, sizeof(buf), 0);
    Sleep(500);
    send(s, buf, sizeof(buf), 0);
    {
        int n = recv(s, buf, sizeof(buf), 0);
        int e = n < 0 ? WSAGetLastError() : 0;
        printf("udp_reset recv=%d err=%d %s\n", n, e, e == WSAECONNRESET ? "ok" : "?");
        if (e == WSAECONNRESET) ok++;
    }
    closesocket(s);
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET)
    {
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) != SOCKET_ERROR)
        {
            shutdown(s, SD_SEND);
            if (send(s, buf, sizeof(buf), 0) == SOCKET_ERROR &&
                WSAGetLastError() == WSAESHUTDOWN)
            {
                printf("send_shutdown ok\n");
                ok++;
            }
            else
                printf("send_shutdown unexpected=%d\n", WSAGetLastError());
        }
        else
            printf("tcp_connect_for_shutdown=%d\n", WSAGetLastError());
        closesocket(s);
    }
    return ok == 2 ? 0 : 1;
}

/* k: loopback self-test — listen on 127.0.0.1:0, connect, accept, echo both ways */
static int kloop(int count)
{
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a;
    int len = sizeof(a), ok = 0, fail = 0, i;
    char buf[256];
    double t0, t1;
    if (l == INVALID_SOCKET) { printf("socket_err\n"); return 1; }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = 0;
    a.sin_addr.s_addr = htonl(0x7f000001);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) || listen(l, 8) ||
        getsockname(l, (struct sockaddr *)&a, &len))
    {
        printf("loopback_listen_err=%d\n", WSAGetLastError());
        return 1;
    }
    t0 = now_ms();
    for (i = 0; i < count; i++)
    {
        SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), s;
        if (c == INVALID_SOCKET) { fail++; continue; }
        if (connect(c, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR) { fail++; closesocket(c); continue; }
        s = accept(l, NULL, NULL);
        if (s == INVALID_SOCKET) { fail++; closesocket(c); continue; }
        memset(buf, i & 0xff, sizeof(buf));
        if (send(c, buf, sizeof(buf), 0) != sizeof(buf) || recv(s, buf, sizeof(buf), 0) != sizeof(buf))
            fail++;
        else
            ok++;
        closesocket(s);
        closesocket(c);
    }
    t1 = now_ms();
    printf("loopback ok=%d fail=%d %.0f ms %.1f conn/s\n", ok, fail, t1 - t0,
           ok / ((t1 - t0) / 1000.0));
    closesocket(l);
    return fail ? 1 : 0;
}

static int rw(const char *host, int port, int count)
{
    static char buf[65536];
    int i, ok = 0, fail = 0;
    double t0 = now_ms(), t1;
    for (i = 0; i < count; i++)
    {
        SOCKET s = conn(host, port);
        int n;
        if (s == INVALID_SOCKET) { fail++; continue; }
        set_touts(s, 20000);
        n = send(s, buf, sizeof(buf), 0);
        if (n != sizeof(buf)) { fail++; closesocket(s); continue; }
        ok++;
        shutdown(s, SD_SEND);
        while (recv(s, buf, sizeof(buf), 0) > 0) {}
        closesocket(s);
    }
    t1 = now_ms();
    printf("req64k ok=%d fail=%d %.0f ms %.1f req/s\n", ok, fail, t1 - t0,
           ok / ((t1 - t0) / 1000.0));
    return fail ? 1 : 0;
}

/* s: select round-trips — connect to echo, send 4K, select-read, recv, repeat */
static int sel(const char *host, int port, int count)
{
    static char buf[4096];
    char rbuf[4096];
    int i, ok = 0, fail = 0;
    double t0 = now_ms(), t1;
    for (i = 0; i < count; i++)
    {
        SOCKET s = conn(host, port);
        fd_set r;
        struct timeval tv;
        int n;
        if (s == INVALID_SOCKET) { fail++; continue; }
        set_touts(s, 10000);
        if (send(s, buf, sizeof(buf), 0) != sizeof(buf)) { fail++; closesocket(s); continue; }
        for (n = 0; n < (int)sizeof(buf);)
        {
            int m;
            FD_ZERO(&r); FD_SET(s, &r);
            tv.tv_sec = 5; tv.tv_usec = 0;
            if (select(0, &r, NULL, NULL, &tv) != 1) { printf("s%d select_timeout\n", i); break; }
            m = recv(s, rbuf + n, sizeof(rbuf) - n, 0);
            if (m <= 0) break;
            n += m;
        }
        if (n == sizeof(buf)) ok++; else fail++;
        closesocket(s);
    }
    t1 = now_ms();
    printf("select_rt ok=%d fail=%d %.0f ms %.1f/s\n", ok, fail, t1 - t0, ok / ((t1 - t0) / 1000.0));
    return fail ? 1 : 0;
}

/* p: MSG_PEEK — send 8K, peek 1K, recv 4K, verify peeked == head of stream */
static int pk(const char *host, int port, int count)
{
    static char buf[8192];
    char pkbuf[1024], rbuf[4096];
    int i, ok = 0, fail = 0;
    memset(buf, 0x5A, sizeof(buf));
    for (i = 0; i < count; i++)
    {
        SOCKET s = conn(host, port);
        int n, m;
        if (s == INVALID_SOCKET) { fail++; continue; }
        set_touts(s, 10000);
        if (send(s, buf, sizeof(buf), 0) != sizeof(buf)) { fail++; closesocket(s); continue; }
        n = recv(s, pkbuf, sizeof(pkbuf), MSG_PEEK);
        m = n > 0 ? recv(s, rbuf, sizeof(rbuf), 0) : -1;
        if (n == sizeof(pkbuf) && m >= n && !memcmp(pkbuf, buf, n)) ok++;
        else { printf("p%d peek=%d recv=%d\n", i, n, m); fail++; }
        closesocket(s);
    }
    printf("peek ok=%d fail=%d\n", ok, fail);
    return fail ? 1 : 0;
}

/* m: concurrent streams — n threads each open a connection and send mb MiB */
typedef struct { const char *host; int port; long mb; long long sent; int err; } mpar;

static void __cdecl mwrk(void *p)
{
    mpar *a = p;
    SOCKET s = conn(a->host, a->port);
    char buf[16384];
    long long total = (long long)a->mb << 20;
    if (s == INVALID_SOCKET) { a->err = 1; return; }
    set_touts(s, 60000);
    memset(buf, 0x33, sizeof(buf));
    while (a->sent < total)
    {
        int n = send(s, buf, (int)((total - a->sent) > (long long)sizeof(buf) ? (long long)sizeof(buf) : (total - a->sent)), 0);
        if (n <= 0) { a->err = WSAGetLastError(); break; }
        a->sent += n;
    }
    closesocket(s);
}

static int mp(const char *host, int port, int conns, long mb)
{
    mpar *a;
    HANDLE *th;
    int i, err = 0;
    long long total = 0;
    double t0, t1;
    a = malloc(sizeof(mpar) * conns);
    th = malloc(sizeof(HANDLE) * conns);
    if (!a || !th) { printf("oom\n"); return 1; }
    t0 = now_ms();
    for (i = 0; i < conns; i++)
    {
        a[i].host = host; a[i].port = port; a[i].mb = mb; a[i].sent = 0; a[i].err = 0;
        th[i] = (HANDLE)_beginthread(mwrk, 0, &a[i]);
        if (th[i] == (HANDLE)-1) { a[i].err = -2; th[i] = NULL; }
    }
    WaitForMultipleObjects(conns, th, TRUE, 300000);
    t1 = now_ms();
    for (i = 0; i < conns; i++)
    {
        total += a[i].sent;
        if (a[i].err) { err++; printf("conn%d err=%d sent=%lld\n", i, a[i].err, a[i].sent); }
        if (th[i]) CloseHandle(th[i]);
    }
    printf("parallel conns=%d each=%ldMB err=%d total=%lld %.0f ms %.2f MB/s\n",
           conns, mb, err, total, t1 - t0, total / 1048576.0 / ((t1 - t0) / 1000.0));
    free(a);
    free(th);
    return err ? 1 : 0;
}

/* b: abortive closes — connect, send a little, closesocket with hard RST */
static int ab(const char *host, int port, int count)
{
    int i, ok = 0, fail = 0;
    double t0 = now_ms(), t1;
    struct sockaddr_in a;
    char buf[256];
    struct linger lg = { 1, 0 };
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr(host);
    memset(buf, 0x41, sizeof(buf));
    for (i = 0; i < count; i++)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        u_long nb = 1;
        fd_set w;
        struct timeval tv;
        int e;
        if (s == INVALID_SOCKET) { printf("b%d socket_err=%d\n", i, WSAGetLastError()); fail++; continue; }
        ioctlsocket(s, FIONBIO, &nb);
        e = connect(s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR ? WSAGetLastError() : 0;
        if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS)
        {
            int soerr = 0, sl = sizeof(soerr);
            FD_ZERO(&w); FD_SET(s, &w);
            tv.tv_sec = 10; tv.tv_usec = 0;
            if (select(0, NULL, &w, NULL, &tv) <= 0) { printf("b%d connect_timeout\n", i); fail++; closesocket(s); continue; }
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl);
            e = soerr;
        }
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
        if (e) { printf("b%d connect_err=%d\n", i, e); fail++; closesocket(s); continue; }
        send(s, buf, sizeof(buf), 0);
        setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lg, sizeof(lg));
        if ((i & 31) == 0) { printf("b%d closed\n", i); fflush(stdout); }
        closesocket(s);
        ok++;
    }
    t1 = now_ms();
    printf("abortive ok=%d fail=%d %.0f ms %.1f/s\n", ok, fail, t1 - t0,
           ok / ((t1 - t0) / 1000.0));
    return fail ? 1 : 0;
}

int main(int argc, char **argv)
{
    WSADATA w;
    int r = 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { printf("usage: wsperf t|c|r host port arg [e]\n"); return 2; }
    if (WSAStartup(MAKEWORD(2, 2), &w)) { printf("WSAStartup fail\n"); return 2; }
    if (argv[1][0] == 't' && argc >= 5)
        r = tp(argv[2], atoi(argv[3]), atol(argv[4]), argc > 5);
    else if (argv[1][0] == 'c' && argc >= 5)
        r = cr(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 'r' && argc >= 5)
        r = rw(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 'd' && argc >= 4)
        r = dl(argv[2], atoi(argv[3]));
    else if (argv[1][0] == 'l' && argc >= 3)
        r = srv(atoi(argv[2]));
    else if (argv[1][0] == 'u' && argc >= 5)
        r = ue(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 'f' && argc >= 5)
        r = refused(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 'x' && argc >= 4)
        r = errpaths(argv[2], atoi(argv[3]));
    else if (argv[1][0] == 'k' && argc >= 3)
        r = kloop(atoi(argv[2]));
    else if (argv[1][0] == 'm' && argc >= 6)
        r = mp(argv[2], atoi(argv[3]), atoi(argv[4]), atol(argv[5]));
    else if (argv[1][0] == 'b' && argc >= 5)
        r = ab(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 's' && argc >= 5)
        r = sel(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 'p' && argc >= 5)
        r = pk(argv[2], atoi(argv[3]), atoi(argv[4]));
    else if (argv[1][0] == 'q' && argc >= 3)
        r = esrv(atoi(argv[2]));
    else
        printf("bad args\n");
    WSACleanup();
    return r;
}
