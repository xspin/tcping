#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <memory.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include "getopt.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#pragma comment(lib, "ws2_32.lib") // MSVC

#define close(fd) closesocket(fd)

typedef int socklen_t;

#ifndef _SA_FAMILY_T_DEFINED
typedef unsigned short family_t;
#endif

#ifndef _IN_PORT_T_DEFINED
typedef uint16_t port_t;
#define _IN_PORT_T_DEFINED
#endif

#ifndef _IN_ADDR_T_DEFINED
typedef uint32_t addr_t;
#define _IN_ADDR_T_DEFINED
#endif

static inline int set_nonblocking(int fd, int enable) {
    u_long mode = enable ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode);
}

#define usleep(x) Sleep((x) / 1000)

#else

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef sa_family_t family_t;
typedef in_port_t port_t;

static inline int set_nonblocking(int fd, int enable) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    if (enable)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}
#endif

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

#ifndef GIT_REV
#define GIT_REV "unknown"
#endif

static char errmsg[1024];

static volatile sig_atomic_t stop = 0;

static void handle_sigint(int sig) {
    (void)sig;
    stop = 1;
}

static inline const char *error() {
#ifdef _WIN32
    static char buf[256];
    int err = WSAGetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, buf, sizeof(buf), NULL);
    return buf;
#else
    return strerror(errno);
#endif
}

static const struct sockaddr *get_addr(const char *domain, port_t port,
                                       family_t family) {
    static struct sockaddr_storage addr;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM; // TCP

    char buf[10];
    sprintf(buf, "%d", port);
    int ret = getaddrinfo(domain, buf, &hints, &res);
    if (ret != 0) {
        snprintf(errmsg, sizeof(errmsg), "getaddrinfo: %s", gai_strerror(ret));
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    memcpy(&addr, res->ai_addr, res->ai_addrlen);

    freeaddrinfo(res);

    return (struct sockaddr *)&addr;
}

/*
static const struct sockaddr *pton(family_t family, const char *ip, int port) {
    static struct sockaddr_storage addr;

    memset(&addr, 0, sizeof(struct sockaddr_storage));

    if (family == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
        addr4->sin_family = family;
        addr4->sin_port = htons(port);
        inet_pton(family, ip, &addr4->sin_addr);
    } else if (family == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
        addr6->sin6_family = family;
        addr6->sin6_port = htons(port);
        inet_pton(family, ip, &addr6->sin6_addr);
    }

    return (struct sockaddr *)&addr;
}
*/

static const char *ntop(const struct sockaddr *addr, port_t *port) {
    static char ip[INET6_ADDRSTRLEN];
    const char *ret = NULL;

    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
        ret = inet_ntop(AF_INET, &addr4->sin_addr, ip, sizeof(ip));
        if (port)
            *port = ntohs(addr4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
        ret = inet_ntop(AF_INET6, &addr6->sin6_addr, ip, sizeof(ip));
        if (port)
            *port = ntohs(addr6->sin6_port);
    }

    if (ret == NULL) {
        snprintf(errmsg, sizeof(errmsg), "inet_ntop: %s", error());
        return NULL;
    }

    return ip;
}

static int connect_with_timeout(int fd, const struct sockaddr *addr,
                                socklen_t addrlen, int timeout_sec) {
    if (set_nonblocking(fd, 1) < 0) {
        return -1;
    }

    int ret = connect(fd, addr, addrlen);
    if (ret == 0) {
        set_nonblocking(fd, 0);
        return 0;
    }
#ifdef _WIN32
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        goto failed;
    }
#else
    if (errno != EINPROGRESS) {
        goto failed;
    }
#endif

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    ret = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (ret == 0) {
#ifdef _WIN32
        WSASetLastError(WSAETIMEDOUT);
#else
        errno = ETIMEDOUT;
#endif
        goto failed;
    }
    if (ret < 0) {
        goto failed;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) < 0) {
        goto failed;
    }
    if (err != 0) {
        errno = err;
        goto failed;
    }

    set_nonblocking(fd, 0);
    return 0;

failed:
    set_nonblocking(fd, 0);
    return -1;
}

static int tcp_connect(const struct sockaddr *addr, int timeout) {
    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(errmsg, sizeof(errmsg), "socket: %s", error());
        return -1;
    }

    socklen_t len = addr->sa_family == AF_INET ? sizeof(struct sockaddr_in)
                                               : sizeof(struct sockaddr_in6);

    // if (connect(fd, addr, len) < 0) {
    if (connect_with_timeout(fd, addr, len, timeout)) {
        snprintf(errmsg, sizeof(errmsg), "connect: %s", error());
        close(fd);
        return -1;
    }

    return fd;
}

static double ping_once(const struct sockaddr *addr, int timeout) {
    if (!addr) {
        assert(addr);
        return -1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int fd = tcp_connect(addr, timeout);
    if (fd < 0) {
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) * 1e3 +
                     (end.tv_nsec - start.tv_nsec) / 1e6; // milliseconds

    close(fd);

    return elapsed;
}

static void usage() {
    const char *prog = "tcping";
    fprintf(
        stderr,
        "Usage: %s [-46hv] [-t timeout] host [port]\n"
        "Version %s (rev %s)\n"
        "Positional arguments:\n"
        "  host             IP address or hostname\n"
        "  port             TCP port (default 80)\n"
        "Options:\n"
        "  -6               force using IPv6\n"
        "  -4               force using IPv4\n"
        "  -c count         stop after 'count' pings\n"
        "  -t timeout       specify timeout in seconds (default 3 seconds)\n"
        "  -w waittime      time in milliseconds to wait for each reply "
        "(default 1 second)\n"
        "  -v               show version info\n"
        "  -h               show this helpful usage\n",
        prog, APP_VERSION, GIT_REV);
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    signal(SIGINT, handle_sigint);

    family_t family = AF_UNSPEC;
    int show_help = 0;
    int show_version = 0;
    int timeout = 3;     // seconds
    int waittime = 1000; // milliseconds
    int count = INT_MAX;
    int opt;

    sprintf(errmsg, "unknown error");

    while ((opt = getopt(argc, argv, "46hvt:w:c:")) != -1) {
        switch (opt) {
        case '6':
            family = AF_INET6;
            break;
        case '4':
            family = AF_INET;
            break;
        case 'h':
            show_help = 1;
            break;
        case 'v':
            show_version = 1;
            break;
        case 't':
            timeout = atoi(optarg);
            break;
        case 'w':
            waittime = atoi(optarg);
            break;
        case 'c':
            count = atoi(optarg);
            break;
        default: // '?'
            usage();
            return 1;
        }
    }

    if (show_help) {
        usage();
        return 0;
    }
    if (show_version) {
        printf("%s (rev %s)\n", APP_VERSION, GIT_REV);
        return 0;
    }

    int remaining = argc - optind;
    if (remaining < 1) {
        printf("host is not specified\n");
        usage();
        return -1;
    }
    if (remaining > 2) {
        printf("unknown arg: %s\n", argv[optind + 2]);
        usage();
        return -1;
    }

#ifdef _WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", err);
        return 1;
    }
#endif

    const char *host = argv[optind];
    const char *port_str = (remaining == 2) ? argv[optind + 1] : "80";
    port_t port = atoi(port_str);

    int seq = 0;
    double min = 1e9;
    double max = 0;
    double mean = 0;
    double m2 = 0;
    int succ = 0;

    const struct sockaddr *addr = get_addr(host, port, family);
    if (!addr) {
        fprintf(stderr, "cannot resolve %s: %s\n", host, errmsg);
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    const char *ip = ntop(addr, NULL);

    if (strcmp(host, ip)) {
        printf("TCPING %s (%s) %d\n", host, ip, port);
    } else {
        printf("TCPING %s %d\n", ip, port);
    }

    const char *proto = addr->sa_family == AF_INET ? "TCP" : "TCP6";

    while (!stop) {
        double elapsed = ping_once(addr, timeout);
        seq++;
        if (elapsed >= 0) {
            succ++;
            printf("%s %s %d: seq=%d time=%.3f ms\n", proto, ip, port, seq,
                   elapsed);
            min = (elapsed < min) ? elapsed : min;
            max = (elapsed > max) ? elapsed : max;
            double delta = elapsed - mean;
            mean += delta / succ;
            double delta2 = elapsed - mean;
            m2 += delta * delta2;
        } else {
            printf("%s %s %d: seq=%d %s\n", proto, ip, port, seq, errmsg);
        }
        if (seq >= count)
            break;
        usleep(1000 * waittime);
    }

    double sd = succ > 1 ? m2 / (succ - 1) : 0;

    printf("\n--- %s %d %s statistics ---\n", host, port, proto);
    printf("%d packets transmitted, %d packets received, %.2f%% packet loss\n",
           seq, succ, 100.0 * (seq - succ) / seq);
    if (succ > 0) {
        printf("round-trip min/avg/max/stddev: %.3f/%.3f/%.3f/%.3f\n", min,
               mean, max, sd);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
