/* Some of the code dealing with parsing command line options was taken from
 * Michael Kerrisk's book, The Linux Programming Interface [2010]. The code
 * in question was released under the LGPLv3, so that is the license that we
 * are releasing linger-server.c under.
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
/*************************************************************************\
*                  Copyright (C) Michael Kerrisk, 2010.                   *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU Affero General Public License as published   *
* by the Free Software Foundation, either version 3 or (at your option)   *
* any later version. This program is distributed without any warranty.    *
* See the file COPYING.agpl-v3 for details.                               *
\*************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

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

#define printable(ch) (isprint((unsigned char) ch) ? ch : '#')

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

static void usage_exit(const char *prog_name, const char *msg, int opt)
{
    if (msg != NULL && opt != 0)
        fprintf(stderr, "%s (-%c)\n", msg, printable(opt));
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
    r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (r == -1)
        die("setting SO_REUSEADDR");

    val = SNDBUF_SIZE,
    r = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
    if (r == -1)
        die("setting SO_SNDBUF");
}

static void set_linger(int fd, int linger_time)
{
    int r;
    struct linger ling;

    ling.l_onoff = 1;
    ling.l_linger = linger_time;
    r = setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    if (r == -1)
        die("setting SO_LINGER");

    printf("Linger timeout (secs): %d\n", linger_time);
}

static void set_nonblocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        die("fcntl F_GETFL");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        die("fcntl F_SETFL");
}

static void parse_opts(int argc, char *argv[], Options *options)
{
    int opt;
    char *prog_name;

    options->linger_sock = OPT_NOSOCK;
    options->linger_time = -1;
    options->wait_on_exit = FALSE;
    options->use_shutdown = FALSE;
    options->nonblocking = FALSE;
    options->shutdown_time = 0;
    prog_name = argv[0] ? argv[0] : "[prog_name]";

    while ((opt = getopt(argc, argv, ":hs:t:wNST:")) != -1) {
        switch (opt) {
        case 'h':
            usage_exit(prog_name, NULL, opt);
        case 's':
            if (strcmp("lsock", optarg) == 0)
                options->linger_sock = OPT_LSOCK;
            else if (strcmp("csock", optarg) == 0)
                options->linger_sock = OPT_CSOCK;
            else if (strcmp("csock_late", optarg) == 0)
                options->linger_sock = OPT_CSOCK_LATE;
            else
                usage_exit(prog_name, "Bad socket option", opt);
            break;
        case 't':
            if (sscanf(optarg, "%d", &options->linger_time) != 1)
                usage_exit(prog_name, "Integer argument expected", opt);
            if (options->linger_time > TIME_MAX)
                usage_exit(prog_name,
                            "Linger timeout must be <= 86400", opt);
            break;
        case 'w':
            options->wait_on_exit = TRUE;
            break;
        case 'N':
            options->nonblocking = TRUE;
            break;
        case 'S':
            options->use_shutdown = TRUE;
            break;
        case 'T':
            if (sscanf(optarg, "%d", &options->shutdown_time) != 1)
                usage_exit(prog_name, "Integer argument expected", opt);
            if (options->shutdown_time <= 0 ||
                options->shutdown_time > TIME_MAX)
                usage_exit(prog_name,
                            "Shutdown timeout must be > 0 and <= 86400",
                            opt);
            break;
        case ':':
            usage_exit(prog_name, "Missing argument", optopt);
        case '?':
            usage_exit(prog_name, "Unrecognised option", optopt);
        default:
            fatal("Unexpected case in switch()");
        }
    }
}

static void timestamp(struct timeval *tp)
{
    if (gettimeofday(tp, NULL) == -1)
        die("gettimeofday() failure");
}

static double time_diff(const struct timeval *before,
                        const struct timeval *after)
{
    double x, y;

    x = (double) before->tv_sec + (double) before->tv_usec / 1000000;
    y = (double) after->tv_sec + (double) after->tv_usec / 1000000;

    return y - x;
}

static void shutdown_wait_eof(int connfd, const Options *options)
{
    struct timeval tv1, *tp_before = &tv1;
    struct timeval tv2, *tp_after = &tv2;
    int r;
    ssize_t n;
    char c;

    puts("-- calling shutdown() on connected socket");
    timestamp(tp_before);

    r = shutdown(connfd, SHUT_WR);
    if (r == -1) {
        if (errno == EWOULDBLOCK)
            puts("EWOULDBLOCK on shutdown()");
        else
            die("shutdown connfd");
    }
    timestamp(tp_after);
    printf("Time to shutdown(): %.3f secs\n", time_diff(tp_before, tp_after));

    if (options->linger_sock == OPT_CSOCK_LATE) {
        puts("Late Linger: on (connected socket)");
        set_linger(connfd, options->linger_time);
    }

    puts("-- waiting for EOF");
    timestamp(tp_before);

    if (options->shutdown_time > 0) {
        struct pollfd pfds[1];

        pfds[0].fd = connfd;
        pfds[0].events = POLLIN;
        r = poll(pfds, 1, (1000 * options->shutdown_time));
        if (r == -1)
            die("poll()");
        if (r == 0) {
            timestamp(tp_after);
            puts("Timeout reached waiting for EOF after shutdown()");
            printf("Timeout expected: %d secs (actual: %.3f secs)\n",
                   options->shutdown_time, time_diff(tp_before, tp_after));
            return;
        }
    }

    n = read(connfd, &c, 1);
    if (n == -1) {
        if (errno == EWOULDBLOCK)
            puts("EWOULDBLOCK on read() after shutdown()");
        else
            die("read() after shutdown()");
    } else if (n == 1) {
        fprintf(stderr, "read() after shutdown(): "
                "Illegal data from peer; EOF expected\n");
        exit(EXIT_FAILURE);
    }
    timestamp(tp_after);
    printf("Time till EOF: %.3f secs\n", time_diff(tp_before, tp_after));
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, r;
    Options sopts, *options = &sopts;
    ssize_t n;
    struct sockaddr_in servaddr;
    char buf[PAYLOAD_SIZE];
    struct timeval tv1, *tp_before = &tv1;
    struct timeval tv2, *tp_after = &tv2;

    parse_opts(argc, argv, options);

    memset(buf, 0, sizeof(buf));
    get_payload(buf, sizeof(buf));

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
        die("socket");

    set_socket_options(listenfd);
    if (options->linger_sock == OPT_LSOCK) {
        set_linger(listenfd, options->linger_time);
        puts("Linger: on (listening socket)");
    } else if (options->linger_sock == OPT_NOSOCK) {
        puts("Linger: off");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT_NUM);

    r = bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
    if (r == -1)
        die("bind()");

    r = listen(listenfd, BACKLOG);
    if (r == -1)
        die("listen()");

    puts("-- waiting for client connection");
    connfd = accept(listenfd, (struct sockaddr *) NULL, NULL);
    if (connfd == -1)
        die("accept()");
    puts("-- client connected");

    if (options->nonblocking) {
        set_nonblocking(connfd);
        puts("Non-Blocking Socket");
    }

    /* Close the listening socket - we don't need it anymore */
    puts("-- closing listening socket");
    r = close(listenfd);
    if (r == -1)
        die("closing listenfd");

    if (options->linger_sock == OPT_CSOCK) {
        set_linger(connfd, options->linger_time);
        puts("Linger: on (connected socket)");
    }
    sleep(1);

    puts("-- writing payload");
    n = write(connfd, buf, sizeof(buf));
    if (n == -1)
        die("write");
    else if (n != sizeof(buf))
        fatal("full buffer not written");

    if (options->use_shutdown)
        shutdown_wait_eof(connfd, options);

    puts("-- closing connected socket");
    timestamp(tp_before);
    r = close(connfd);
    if (r == -1) {
        if (errno == EWOULDBLOCK)
            puts("EWOULDBLOCK on close()");
        else
            die("closing connfd");
    }
    timestamp(tp_after);
    printf("Time to close(): %.3f secs\n", time_diff(tp_before, tp_after));

    if (options->wait_on_exit) {
        printf("Press RETURN to exit: ");
        getchar();
    }
    exit(EXIT_SUCCESS);
}
