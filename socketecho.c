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
 */
#include "uriparser.h"
#include "logging.h"
#include "macroutils.h"
#include <netdb.h>              /* getaddrinfo() */
#include <arpa/inet.h>          /* inet_addr */
#include <sys/socket.h>
#include <argp.h>
#include <stdio.h>
#include <stdlib.h>


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


int main(int argc, char *argv[])
{
    log_info("Size of struct socket_uri %lu", (unsigned long)sizeof(struct socket_uri));
    struct socket_uri uri = {0};
    if (!uri_parse("tcp://127.0.0.1:1234", &uri)) {
        log_err("Wrong arguments");
        exit(EXIT_FAILURE);
    }

    // convert host to ip
    if (STYPE_UNIX != uri.type && NULL != uri.host) {
        if (!host_resolve(uri.type, uri.host, &uri.ip)) {
            log_crit("Could not get addr for host %s", uri.host);
            exit(EXIT_FAILURE);
        }
        log_warn("Converted host '%s' to addr '%s'", uri.host, inet_ntoa(uri.ip));
        free(uri.host);
        uri.host = NULL;
    }

    int sock = socket(STYPE_UNIX == uri.type ? AF_UNIX : AF_INET,
                      STYPE_UDP == uri.type ? SOCK_DGRAM : SOCK_STREAM,
                      0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = uri.port,
        .sin_addr = uri.ip
    };
    return 0;
}
