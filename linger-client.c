/* Some of the code surrounding getaddrinfo() was taken from Michael Kerrisk's
 * book, The Linux Programming Interface [2010]. The code in question was
 * released under the LGPLv3, so that is the license that we are releasing
 * linger-client.c under.
 */
/*************************************************************************\
*                  Copyright (C) Nybek Limited, 2015.                     *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU Lesser General Public License as published   *
* by the Free Software Foundation, either version 3 or (at your option)   *
* any later version. This program is distributed without any warranty.    *
* See the files COPYING.lgpl-v3 and COPYING.gpl-v3 for details.           *
\*************************************************************************/
/*************************************************************************\
*                  Copyright (C) Michael Kerrisk, 2010.                   *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU Lesser General Public License as published   *
* by the Free Software Foundation, either version 3 or (at your option)   *
* any later version. This program is distributed without any warranty.    *
* See the files COPYING.lgpl-v3 and COPYING.gpl-v3 for details.           *
\*************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#ifdef TRUE
#undef TRUE
#endif

#ifdef FALSE
#undef FALSE
#endif

typedef enum { FALSE, TRUE } Boolean;

#define PORT "7777"
#define RCVBUF_SIZE 8192
#define WAIT_TIME 4
#define READ_SIZE 512

static void fatal(const char* where, const char *msg)
{
    if (where != NULL)
        fprintf(stderr, "%s: ", where);
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static void die(const char *where)
{
    perror(where);
    exit(EXIT_FAILURE);
}

static void usage_exit(const char *prog_name, const char *err_msg)
{
    if (err_msg != NULL)
        fprintf(stderr, "%s\n", err_msg);
    fprintf(stderr,
            "usage: %s [-i] hostname\n"
            "    -h      Print usage and exit.\n"
            "    -i      Interactive. Require user confirmation before each\n"
            "            read of the stream.\n",
            prog_name);
    exit(EXIT_FAILURE);
}

static void set_socket_options(int fd)
{
    int r, val;

    val = RCVBUF_SIZE,
    r = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
    if (r == -1)
        die("setting SO_RCVBUF");
}

static int connect_to(const char *host)
{
    int sockfd, n;
    struct addrinfo hints = {0};
    struct addrinfo *result, *rp;

    hints.ai_family = AF_INET;              /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;        /* TCP */

    n = getaddrinfo(host, PORT, &hints, &result);
    if (n != 0)
        fatal("getaddrinfo() failed", gai_strerror(n));

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            die("socket() failed");

        set_socket_options(sockfd);

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */
        perror("connect() failed");

        close(sockfd);
    }

    if (rp == NULL)
        fatal(NULL, "failed to connect socket");

    freeaddrinfo(result);

    return sockfd;
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

static void recv_all(int fd, Boolean opt_interactive)
{
    char buf[READ_SIZE];
    int n, total;

    total = 0;
    while ((n = read(fd, buf, READ_SIZE)) != 0) {
        if (n == -1)
            die("socket read()");
        total += n;
        printf("RECV: %d (%d)\n", n, total);

        if (opt_interactive) {
            printf("Press RETURN to read next %d bytes: ", READ_SIZE);
            while (getchar() != '\n')
                ;
        } else {
            sleep(WAIT_TIME);
        }
    }
    puts("Connection closed");
}

int main(int argc, char *argv[])
{
    int sockfd;
    struct timeval tv1, *tp_start = &tv1;
    struct timeval tv2, *tp_end = &tv2;
    char *hostname;
    Boolean opt_interactive = FALSE;

    if (argc == 2) {
        if (strcmp("-h", argv[1]) == 0)
            usage_exit(argv[0], NULL);
        else
            hostname = argv[1];
    } else if (argc == 3) {
        if (strcmp("-i", argv[1]) == 0)
            opt_interactive = TRUE;
        else
            usage_exit(argv[0], "invalid option");
        hostname = argv[2];
    } else {
        usage_exit(argv[0], NULL);
    }

    sockfd = connect_to(hostname);
    timestamp(tp_start);

    recv_all(sockfd, opt_interactive);

    close(sockfd);
    timestamp(tp_end);
    printf("Total recv time (secs): %.3f\n", time_diff(tp_start, tp_end));

    exit(EXIT_SUCCESS);
}
