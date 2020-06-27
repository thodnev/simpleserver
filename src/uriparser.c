/**
 * - What our regexp will look like. From higher perspective we have two options
 *   combined together:
 *     - ["tcp" or "udp"] + "://" + [ipv4 or host] + ":" + [port]
 *     - ["unix"] + "://" + path
 *   So the regexp will look like (with 'extended' flag to ignore whitespaces):
 *       ^ (?P<proto> tcp|udp|unix) : \/\/ (?:
 *         (?:
 *           (?P<ip>
 *             \d{1,3}  (?: \.\d{1,3}) {3}
 *           ) | (?P<domain>
 *             [a-zA-Z0-9] *   (?: \.? [a-zA-Z0-9\-] ) *
 *           )
 *           : (?P<port> \d{1,6})
 *         ) | (?P<path>
 *           [^[:cntrl:]] +
 *         )
 *       )$
 *   Here the <ip> and <domain> parts were designed far from optimal to keep them simple.
 *   See https://stackoverflow.com/a/106223/5750172 for RFC-compliant hostname regexps.
 *
 *   Sometimes "debugging" of regexps could be tricky, especially for the long ones.
 *   I recommend using services like: regex101.com, debuggex.com or regexr.com for that.
 *
 *   When working with large regexps, use the 'extended' flag and write your regexps in
 *   a multiline-way. One may also wish to use the (?# comment) or # comment syntax.
 *
 *   **Improtant** : above is presented the pure RE. If we denote it in C, care should
 *   be taken to properly escape all special chars. I.e. pure '\' becomes '\\' in c string,
 *   pure '\/\/' becomes '\\/\\/' and '\d' becomes '\\d'.
 *
 *   PCRE supports different char formats and sizes. See https://pcre.org/current/doc/html
 */
#include "uriparser.h"
#include "logging.h"
#include "macroutils.h"
#include "regexp.h"
#include <arpa/inet.h>          /* For inet_addr conversion */
#include <sys/un.h>
//#include <limits.h>
//#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static const char * const uri_re = (
    " ^ (?: "
    "     (?P<proto> tcp|udp) : \\/\\/ (?: "
    "       (?P<host> "
    "         (?: [a-zA-Z0-9] | [a-zA-Z0-9][a-zA-Z0-9\\-]{0,61} [a-zA-Z0-9] ) "
    "         (?: \\. (?: [a-zA-Z0-9] | [a-zA-Z0-9][a-zA-Z0-9\\-]{0,61} [a-zA-Z0-9] ) ) * "
    "       ) : (?P<port> \\d{1,6}) "
    "   ) | (?: "
    "     (?P<proto> unix) : \\/\\/ (?P<path> [^[:cntrl:]] + ) "
    "   ) "
    " )$ "
);
static const char *uri_groupnames[] = {"proto", "host", "port", "path", NULL};

// On Linux paths for UNIX sockets could be up to 108 symbols (including '\0')
// Here we portably calculate that
static const long UNIX_SOCKET_PATH_MAXLEN = sizeof(((struct sockaddr_un *)0)->sun_path) - 1;


bool uri_parse(const char *uristring, struct socket_uri *resuri)
{
    log_dbg("UNIX socket path maxlen: %ld", UNIX_SOCKET_PATH_MAXLEN);

    bool ret = false;
    if (NULL == resuri || NULL == uristring)
        return ret;   // fail fast

    char *groupvals[arr_len(uri_groupnames) - 1];
    reobj *re = re_init(uri_re, (RE_EXTENDED | RE_UTF | RE_DUPNAMES), NULL);
    if (NULL == re) {
        log_crit("Regexp compilation error");
        return ret;
    }
    long found = re_collect_named(re, uristring, groupvals, uri_groupnames, 0);
    if (found < 1)
        return ret;

    const char *proto = groupvals[0],
               *host  = groupvals[1],
               *port  = groupvals[2],
               *path  = groupvals[3];

    log_dbg("PROTO: %s HOST: %s PORT: %s PATH: %s", proto, host, port, path);
    struct socket_uri res = {
        .type = (!strcmp(proto, "tcp") ? STYPE_TCP :
                 !strcmp(proto, "udp") ? STYPE_UDP :
                 STYPE_UNIX),

    };

    if (STYPE_UNIX != res.type) {
        long long p;
        if (NULL == port || sscanf(port, "%lld", &p) < 1 || p < 1 || p > 65535) {
            log_err("port conversion failed");
            goto dealloc;
        }
        res.host = strdup(host);
        if (NULL == res.host) {
            log_err("host conversion failed");
            goto dealloc;
        }
        // man says ports need to be in network order
        res.port = htons(p);    // machine order to network order
    } else {
        res.path = strdup(path);
        if (NULL == res.path || strlen(res.path) > UNIX_SOCKET_PATH_MAXLEN) {
            log_err("path conversion failed");
            goto dealloc;
        }
    }

    ret = true;
    memcpy(resuri, &res, sizeof(res));

dealloc:
    re_free(re);
    log_info("Freeing intermediate groupvals");
    arr_foreach(v, groupvals) {
        log_dbg("  %s", v);
        free(v);
    }

    return ret;
}
