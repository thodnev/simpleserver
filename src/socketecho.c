/**
 * - We use URI notation to be able to specify different communication types.
 *   (https://en.wikipedia.org/wiki/List_of_URI_schemes)
 *   For example:
 *       tcp://192.168.0.1:8000 or tcp://localhost:1234  -- for TCP
 *       udp://192.168.0.1:1234  -- for UDP
 *       unix:///tmp/my.sock  -- for UNIX sockets (/tmp/my.sock here)
 *   We will not be implementing the IPv6 here (only IPv4) not to overcomplicate the example.
 * - And how do we parse it? These URIs are simple and we could parse them by hand.
 *   But for the demonstrational purposes, we will utilize regular expressions (regexps).
 *   There are at least two options:
 *     - Use glibc regular expressions. The glib is surely available on most systems and
 *       this approach will allow us to reduce the code size (because of dynamic linkage to glibc)
 *       (https://www.gnu.org/software/libc/manual/html_node/Regular-Expressions.html)
 *     - Use more feature-rich pcre library (Perl-Compatible Regular Expressions). This will either
 *       require having pcre in system with dynamic linkage, or lead to bigger code size if
 *       the static linkage is used.
 *   We will stick to the latter approach as we're not living in the world of 90s with slow
 *   dial-up connections. And we won't parse by hand because *code reuse matters*.
 * - What our regexp will look like? See uriparser.c for details
 * - Handling Unicode and all possible encodings is far beyond the scope of the example,
 *   so a lot of stuff here is done assuming the UTF-8 encoding is used
 *
 * - We do a simple echo for now, but you can already see that the code gets messy really quickly.
 *   Since we don't want spaghetti code, it needs to be decoupled into separate routines
 *   to maximize cohesion and code reuse.
 * - How to test it? First, you need openbsd netcat (`sudo pacman -S openbsd-netcat`)
 *   For TCP:
 *      echo -n teststring | nc -v 127.0.0.1 8000
 *   For UDP:
 *      echo -n teststring | nc -u 127.0.0.1 8000       (don't use -v here)
 *   For UNIX:
 *      echo -n teststring | nc -U /tmp/my.socket
 *   And remember we don't free up socket resources here, thus you may want to remove socket file
 *   or reclaim the port used.
 *   Proper cleanups on exit will need having SIGINT signal handler provided.
 */
#define _GNU_SOURCE
#include "uriparser.h"
#include "logging.h"
#include "macroutils.h"
#include <netdb.h>              /* getaddrinfo() */
#include <arpa/inet.h>          /* inet_addr */
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <argp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


// Here we rely on some heavy typecasting. That's ok and is exactly how
// the underlying structures were designed
bool host_resolve(enum socket_type kind, const char *host, struct in_addr *res)
{
    if (STYPE_UNIX == kind)
        return false;
    struct addrinfo hint = {
        .ai_family = AF_INET
    };
    struct addrinfo *req;
    int err = getaddrinfo(host, NULL, &hint, &req);
    if (err) {
        log_crit("getaddrinfo for host '%s' returned %d ('%s')", host, err, gai_strerror(err));
        return false;
    }
    struct sockaddr_in *tmp = (struct sockaddr_in *)(req->ai_addr);
    *res = tmp->sin_addr;
    freeaddrinfo(req);
    return true;
}


static bool err_handle(const char *errmsg, ...)
{
    va_list va;
    va_start(va, errmsg);

    if (NULL != errmsg) {
        // such successive print calls are not thread-safe and
        // may result in output being messed
        fprintf(stderr, "Error: ");
        vfprintf(stderr, errmsg, va);
        fprintf(stderr, "\n");
    }
    va_end(va);
    exit(EXIT_FAILURE);
}


// Beware: the code below is really really dirty and obviously needs a serious retouch
int main(int argc, char *argv[])
{
    const int CONN_POOL_SIZE = 100;
    const long RECV_BUFFER_SIZE = 1024;
    log_dbg("Size of struct socket_uri %lu", (unsigned long)sizeof(struct socket_uri));

    if (argc != 2)
        err_handle("\rUsage: %s URI", argv[0]);

    char *uristring = argv[1];

    struct socket_uri uri = {0};
    if (!uri_parse(uristring, &uri))
        err_handle("Uri parsing failed");

    // convert host to ip
    if (STYPE_UNIX != uri.type) {
        if (!host_resolve(uri.type, uri.host, &uri.ip))
            err_handle("Could not resolve host %s", uri.host);

        log_warn("Resolved host '%s' to addr '%s'", uri.host, inet_ntoa(uri.ip));
        free(uri.host);
        uri.host = NULL;
    }

    struct sockaddr *sockaddr = NULL;   // abstract socket addr
    if (STYPE_UNIX == uri.type) {
        struct sockaddr_un sau = { .sun_family = AF_UNIX };
        strncpy(sau.sun_path, uri.path, sizeof(sau.sun_path) -1);
        // reinterpret assignment
        if((sockaddr = calloc(1, sizeof(sau))) == NULL)
            err_handle("Memory allocation failed");
        memcpy(sockaddr, &sau, sizeof(sau));
    } else {
        struct sockaddr_in sai = {
            .sin_family = AF_INET,
            .sin_port = uri.port,
            .sin_addr = uri.ip
        };
        log_dbg("Port %d (0x%x) in network order is %d (0x%x)",
                (int)htons(sai.sin_port), (int)htons(sai.sin_port),
                (int)sai.sin_port, (int)sai.sin_port);
        if ((sockaddr = calloc(1, sizeof(sai))) == NULL)
            err_handle("Memory allocation failed");
        memcpy(sockaddr, &sai, sizeof(sai));
    }

    // save exactly what we need and free the rest
    typeof(uri.type) type = uri.type;
    free(uri.host);

    int sock = socket(STYPE_UNIX == type ? AF_UNIX : AF_INET,
                      STYPE_UDP == type ? SOCK_DGRAM : SOCK_STREAM,
                      0);
    if (-1 == sock) {
        free(sockaddr);
        err_handle("Socket creation failed (%s)", strerror(errno));
    }

    log_dbg("Created %s socket with fd=%d",
            (type == STYPE_UDP ? "UDP" : type == STYPE_TCP ? "TCP" :
             type == STYPE_UNIX ? "UNIX" : 0),
            sock);

    int err = bind(sock, sockaddr, (STYPE_UNIX == type) ? sizeof(struct sockaddr_un)
                                                        : sizeof(struct sockaddr_in));
    free(sockaddr);  // no longer needed since socket was created
    if (-1 == err)
        err_handle("Socket bind failed (%s)", strerror(errno));

    log_dbg("Socket bound");

    if (type != STYPE_UDP) {
        err = listen(sock, CONN_POOL_SIZE);
        if (-1 == err)
            err_handle("Socket listen failed (%s)", strerror(errno));
        log_dbg("Listening with pool size %ld", (long)CONN_POOL_SIZE);
    }

    // Accept api works the following way:
    // We pass sockaddr which gets filled with connection details (remote ip, port)
    // And pass pointer to sockaddr len, which is initially length of sockaddr struct
    // but is being set to the actual filled length by accept.
    // So here we store the original length for consequetive calls
    const socklen_t cdata_len_st = (STYPE_UNIX == type ? sizeof(struct sockaddr_un)
                                                       : sizeof(struct sockaddr_in));
    struct sockaddr *cdata = calloc(1, cdata_len_st);
    if (NULL == cdata) {
        close(sock);
        err_handle("Memory allocation failed");
    }
    printf("Waiting for incoming connections\n");

    while (1) {
        int conn = -1;
        char buf[RECV_BUFFER_SIZE];
        socklen_t cdata_len = cdata_len_st;

        if (STYPE_UDP == type) {
            conn = sock;
        } else {
            conn = accept4(sock, cdata, &cdata_len, 0);
            if (-1 == conn) {
                if (ECONNABORTED == errno || EPROTO == errno) {
                    // Connection aborted or protocol error caught
                    log_err("Connection error, continuing...");
                    continue;
                }
                free(cdata);
                err_handle("Connection accept retured %d (%s)", errno, strerror(errno));
            }
        }


        // Receive. As UDP is conectionless, we get the remote addr here
        // For TCP we get addr when the connection is initiated (accept)
        // For UNIX we don't need any addr
        int cnt = recvfrom(conn, buf, sizeof(buf), 0,
                           STYPE_UDP == type ? cdata : NULL,
                           STYPE_UDP == type ? &cdata_len : NULL);
        if (-1 == cnt) {
            fprintf(stderr, "Receive error (%s)", strerror(errno));
            if (type != STYPE_UDP)
                close(conn);
            continue;
        }

        if (type != STYPE_UNIX && cdata_len != cdata_len_st) {
            printf("[UNDEFINED (%d)] \n", cnt);
            continue;
        }
        if (type != STYPE_UNIX && cdata_len == cdata_len_st)
            printf("[%s (%d)] ", inet_ntoa(((struct sockaddr_in *)cdata)->sin_addr), cnt);
        if (type == STYPE_UNIX)
            printf("[UNIX] ");

        buf[cnt] = '\0';    // fix string end
        char str[sizeof(buf) + 1];
        snprintf(str, sizeof(str), "%s\n", buf);
        printf("%s", str);
        fflush(stdout);
        snprintf(str, sizeof(str), "Echo: \"%s\"\n", buf);

        unsigned long len = strlen(str);
        cnt = sendto(conn, str, len, 0,
                     STYPE_UDP == type ? cdata : NULL,
                     STYPE_UDP == type ? cdata_len : NULL);
        if (cnt != len)
            fprintf(stderr, "[sz err %ld < %ld (%s)]\n", (long)cnt, (long)len, strerror(errno));

        if (STYPE_UDP != type)
            close(conn);
    }

    return 0;
}
