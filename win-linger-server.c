/* To keep matters simple, we are releasing win-linger-server.c under the
 * same license as linger-server.c.
 */
/*************************************************************************\
*                  Copyright (C) Nybek Limited, 2015.                     *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU Affero General Public License as published   *
* by the Free Software Foundation, either version 3 or (at your option)   *
* any later version. This program is distributed without any warranty.    *
* See the file COPYING.agpl-v3 for details.                               *
\************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#ifdef TRUE
#undef TRUE
#endif

#ifdef FALSE
#undef FALSE
#endif

typedef enum { FALSE, TRUE } Boolean;

#define PORT_NUM 7777
#define SNDBUF_SIZE (50 * 1024)
#define PAYLOAD_SIZE (20 * 1024)
#define BACKLOG 128
#define TIME_MAX 86400

#define OPT_NOSOCK 0
#define OPT_LSOCK 1
#define OPT_CSOCK 2
#define OPT_CSOCK_LATE 3

typedef struct {
    int linger_sock;
    int linger_time;
    Boolean wait_on_exit;
    Boolean use_shutdown;
    Boolean nonblocking;
    int shutdown_time;
} Options;

static void fatal(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static void die(const char *where)
{
    perror(where);
    exit(EXIT_FAILURE);
}

static void wsa_die(const char *where)
{
    fprintf(stderr, "Error %s: %ld\n", where, WSAGetLastError());
    exit(EXIT_FAILURE);
}

static void usage_exit(const char *prog_name, const char *msg)
{
    if (msg != NULL)
        fprintf(stderr, "%s\n", msg);
    fprintf(stderr, "Usage: %s [-s lsock|csock|csock_late] [-t linger_secs] "
                    "[-w] [-N] [-S] [-T eof_wait_secs]\n", prog_name);
    fprintf(stderr,
            "     -h             Print usage and exit.\n"
            "     -s sock_type   The SO_LINGER option is applied to sock_type:\n"
            "                        lsock - The listening socket\n"
            "                        csock - The connected socket\n"
            "                        csock_late - The connected socket. This requires -S to\n"
            "                                be effective as it is applied after shutdown().\n"
            "     -t secs        The SO_LINGER timeout in seconds. This only\n"
            "                    has an effect when -s is set.\n"
            "     -w             Wait for user confirmation before exiting.\n"
            "     -N             Put connected socket in Non-Blocking mode.\n"
            "     -S             Use shutdown() and wait for EOF.\n"
            "     -T secs        Timeout waiting for EOF after shutdown().\n"
            "                    Must be > 0.\n");
    exit(EXIT_FAILURE);
}

static void get_payload(char *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        buf[i] = '.';
    }
}

static void set_socket_options(int fd)
{
    int r, val;

    val = 1;
    r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &val, sizeof(val));
    if (r == -1)
        wsa_die("setting SO_REUSEADDR");

    val = SNDBUF_SIZE,
    r = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &val, sizeof(val));
    if (r == -1)
        wsa_die("setting SO_SNDBUF");
}

static void set_linger(int fd, int linger_time)
{
    int r;
    struct linger ling;

    ling.l_onoff = 1;
    ling.l_linger = linger_time;
    r = setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
    if (r == -1)
        wsa_die("setting SO_LINGER");

    printf("Linger timeout (secs): %d\n", linger_time);
}

static void set_nonblocking(int fd)
{
    int r;
    unsigned long on = 1;

    r = ioctlsocket(fd, FIONBIO, &on);
    if (r == -1)
        wsa_die("ioctlsocket()");
}

static void parse_opts(char *argv[], Options *options)
{
    char *prog_name, *s, *param;
    int i;

    options->linger_sock = OPT_NOSOCK;
    options->linger_time = -1;
    options->wait_on_exit = FALSE;
    options->use_shutdown = FALSE;
    options->nonblocking = FALSE;
    options->shutdown_time = 0;
    prog_name = argv[0] ? argv[0] : "[prog_name]";

    i = 1;
    for (s = argv[i]; s != NULL; s = argv[++i]) {
        if (strcmp(s, "-h") == 0)
            usage_exit(prog_name, NULL);
        if (strcmp(s, "-s") == 0) {
            param = argv[++i];
            if (param == NULL)
                usage_exit(prog_name, "Missing argument for -s");
            if (strcmp(param, "lsock") == 0)
                options->linger_sock = OPT_LSOCK;
            else if (strcmp(param, "csock") == 0)
                options->linger_sock = OPT_CSOCK;
            else if (strcmp(param, "csock_late") == 0)
                options->linger_sock = OPT_CSOCK_LATE;
            else
                usage_exit(prog_name, "Bad socket option");
        } else if (strcmp(s, "-t") == 0) {
            param = argv[++i];
            if (param == NULL)
                usage_exit(prog_name, "Missing argument for -t");
            if (sscanf(param, "%d", &options->linger_time) != 1)
                usage_exit(prog_name, "Integer argument expected for -t");
            if (options->linger_time > TIME_MAX)
                usage_exit(prog_name, "Linger timeout must be <= 86400");
        } else if (strcmp(s, "-w") == 0) {
            options->wait_on_exit = TRUE;
        } else if (strcmp(s, "-N") == 0) {
            options->nonblocking = TRUE;
        } else if (strcmp(s, "-S") == 0) {
            options->use_shutdown = TRUE;
        } else if (strcmp(s, "-T") == 0) {
            param = argv[++i];
            if (param == NULL)
                usage_exit(prog_name, "Missing argument for -T");
            if (sscanf(param, "%d", &options->shutdown_time) != 1)
                usage_exit(prog_name, "Integer argument expected for -T");
            if (options->shutdown_time <= 0 ||
                options->shutdown_time > TIME_MAX)
                usage_exit(prog_name,
                           "Shutdown timeout must be > 0 and <= 86400");
        } else {
            usage_exit(prog_name, "Unrecognised option");
        }
    }
}

static double time_diff(DWORD before, DWORD after)
{
    return (double) (after - before) / 1000;
}

static void shutdown_wait_eof(int connfd, const Options *options)
{
    DWORD tstamp_before;
    double interval;
    int r;
    long n;
    char c;

    puts("-- calling shutdown() on connected socket");
    tstamp_before = GetTickCount();

    r = shutdown(connfd, SD_SEND);
    if (r == -1) {
        if (errno == EWOULDBLOCK)
            puts("EWOULDBLOCK on shutdown()");
        else
            wsa_die("shutdown()");
    }
    interval = time_diff(tstamp_before, GetTickCount());
    printf("Time to shutdown(): %.3f secs\n", interval);

    if (options->linger_sock == OPT_CSOCK_LATE) {
        puts("Late Linger: on (connected socket)");
        set_linger(connfd, options->linger_time);
    }

    puts("-- waiting for EOF");
    tstamp_before = GetTickCount();

    if (options->shutdown_time > 0) {
        WSAPOLLFD pfds[1];

        pfds[0].fd = connfd;
        pfds[0].events = POLLIN;
        r = WSAPoll(pfds, 1, (1000 * options->shutdown_time));
        if (r == -1)
            wsa_die("WSAPoll()");
        if (r == 0) {
            interval = time_diff(tstamp_before, GetTickCount());
            puts("Timeout reached waiting for EOF after shutdown()");
            printf("Timeout expected: %d secs (actual: %.3f secs)\n",
                   options->shutdown_time, interval);
            return;
        }
    }

    n = recv(connfd, &c, 1, 0);
    if (n == -1) {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            puts("WSAEWOULDBLOCK on read() after shutdown()");
        else
            wsa_die("recv() after shutdown()");
    } else if (n == 1) {
        fprintf(stderr, "recv() after shutdown(): "
                "Illegal data from peer; EOF expected\n");
        exit(EXIT_FAILURE);
    }
    interval = time_diff(tstamp_before, GetTickCount());
    printf("Time till EOF: %.3f secs\n", interval);
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, r;
    Options sopts, *options = &sopts;
    long n;
    struct sockaddr_in servaddr;
    char buf[PAYLOAD_SIZE];
    DWORD tstamp_before;
    double interval;
    WSADATA wsa_data;

    parse_opts(argv, options);

    memset(buf, 0, sizeof(buf));
    get_payload(buf, sizeof(buf));

    r = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (r != 0)
    	wsa_die("at WSAStartup()");

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
        wsa_die("socket()");

    set_socket_options(listenfd);
    if (options->linger_sock == OPT_LSOCK) {
        puts("Linger: on (listening socket)");
        set_linger(listenfd, options->linger_time);
    } else if (options->linger_sock == OPT_NOSOCK) {
        puts("Linger: off");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT_NUM);

    r = bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
    if (r == -1)
        wsa_die("bind()");

    r = listen(listenfd, BACKLOG);
    if (r == -1)
        wsa_die("listen()");

    puts("-- waiting for client connection");
    connfd = accept(listenfd, (struct sockaddr *) NULL, NULL);
    if (connfd == -1)
        wsa_die("accept()");
    puts("-- client connected");

    if (options->nonblocking) {
        set_nonblocking(connfd);
        puts("Non-Blocking Socket");
    }

    /* Close the listening socket - we don't need it anymore */
    puts("-- closing listening socket");
    r = closesocket(listenfd);
    if (r == -1)
        wsa_die("closing listenfd");

    if (options->linger_sock == OPT_CSOCK) {
        puts("Linger: on (connected socket)");
        set_linger(connfd, options->linger_time);
    }
    Sleep(1000);

    puts("-- writing payload");
    n = send(connfd, buf, sizeof(buf), 0);
    if (n == -1)
        wsa_die("write()");
    else if (n != sizeof(buf))
        fatal("full buffer not written");

    if (options->use_shutdown)
        shutdown_wait_eof(connfd, options);

    puts("-- closing connected socket");
    tstamp_before = GetTickCount();
    r = closesocket(connfd);
    if (r == -1)
	wsa_die("closing connfd");

    interval = time_diff(tstamp_before, GetTickCount());
    printf("Time to close(): %.3f secs\n", interval);

    if (options->wait_on_exit) {
        printf("Press RETURN to exit: ");
        getchar();
    }
    WSACleanup();
    exit(EXIT_SUCCESS);
}
