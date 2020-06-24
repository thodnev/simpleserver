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
#include <arpa/inet.h>          /* For inet_addr conversion */
#include <sys/un.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
// Uncomment this if statically linking against pcre
//#define PCRE2_STATIC
// pcre2.h is common interface. We need to provide lib with system-wide char size
#define PCRE2_CODE_UNIT_WIDTH CHAR_BIT
#include <pcre2.h>      // Requires buildflags as `pkg-config --cflags --libs libpcre2-8` shows

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

// There is always a tradeoff between simplicity and feature-richness
// PCRE is powerful, but not simple. This could be solved by
// simply providing a higher level of abstraction for your own (broad) needs
// Here we encapsulate the named group extraction into one function,
// which may prove useful in other contexts in future.
static long re_collect_named(const char *regexp, const char *string,
                             const char **groupnames, char **collected)
{
    if (NULL == regexp || NULL == string || NULL == groupnames || NULL == collected)
        return RE_WRONG_ARGS;   // fail fast
    // count groups from NULL-terminated array
    long ngroups = 0;
    while (NULL != groupnames[ngroups])
        ngroups++;
    log_info("Detected %ld groups", ngroups);

    int re_err;                 // stores PCRE error code
    PCRE2_SIZE _re_erroffset;   // Not used. If we know we had an error, we don't care where
    // Use default compile context with following option flags:
    //    PCRE2_EXTENDED - Ignore white space and # comments
    //    PCRE2_UTF - Treat pattern and subjects as UTF strings
    //    PCRE2_DUPNAMES - Allow duplicate names for subpatterns
    pcre2_code *re = pcre2_compile(
        (PCRE2_SPTR)regexp,                /* A string containing expression to be compiled */
        PCRE2_ZERO_TERMINATED,             /* The length of the string or PCRE2_ZERO_TERMINATED */
        PCRE2_EXTENDED | PCRE2_UTF | PCRE2_DUPNAMES,        /* Option bits */
        &re_err,                           /* Where to put an error code */
        &_re_erroffset,                    /* Where to put an error offset */
        NULL);                             /* Pointer to a compile context or NULL */
    if (NULL == re) {
        char buffer[100];
        pcre2_get_error_message(re_err, (unsigned char *)buffer, arr_len(buffer));
        log_crit("Regexp compilation error %d @ %llu : %s",
                 re_err, (long long unsigned)_re_erroffset, buffer);
        return RE_WRONG_PATTERN;
    }
    log_info("Regexp compiled");

    int ret = 0;
    // Create match block with number of entries calculated from our re
    // It will store matched results
    pcre2_match_data *match = pcre2_match_data_create_from_pattern(re, NULL);
    if (NULL == match) {
        log_crit("Match block could not be obtained");
        ret = RE_RESOURCE_ERROR;
        goto code_free;
    }


    long mcnt = pcre2_match(
        re,                                 /* Points to the compiled pattern */
        (PCRE2_SPTR)string,                 /* Points to the subject string */
        (PCRE2_SIZE)strlen(string),         /* Length of the subject string */
        (PCRE2_SIZE)0,                      /* Offset in the subject at which to start matching */
        0,                                  /* Option bits */
        match,                              /* Points to a match data block, for results */
        NULL);                              /* Points to a match context, or is NULL */

    if (mcnt < 1) {
        log_err("No matches. Match count is %ld", mcnt);
        ret = RE_NOMATCH;
        goto match_free;
    }
    log_dbg("Match count %ld", mcnt);

    if (DEBUG >= LOG_DEBUG) {
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match);
        for (long i = 0; i <  mcnt; i ++) {
            PCRE2_SPTR substring_start = string + ovector[2*i];
            PCRE2_SIZE substring_length = ovector[2*i+1] - ovector[2*i];
            log_dbg("  Item %2ld: %.*s", i, (int)substring_length, (char *)substring_start);
        }
    }

    // Now extract values of named groups
    log_info("Trying to extract %ld fields", (long)ngroups);
    long numfound = 0;
    for (long i = 0; i < ngroups; i++) {
        char *val = NULL;
        PCRE2_SIZE len = 0;
        long err = pcre2_substring_get_byname(
            match,                              /* The match data for the match */
            (PCRE2_SPTR)(groupnames[i]),      /* Name of the required substring */
            (PCRE2_UCHAR **)&val,               /* Where to put the string pointer */
            &len);                              /* Where to put the string length */
        switch (err) {
        case 0:
            break;
        case PCRE2_ERROR_UNSET:
            log_warn("PCRE group %ld (\"%s\") value not found", (long)i, groupnames[i]);
            collected[i] = NULL;
            continue;
        default:
            log_warn("PCRE group get for field %s returned %ld", groupnames[i], err);
            collected[i] = NULL;
            continue;
        }
        log_dbg("  Found %s=%s", groupnames[i], val);

        numfound++;
        // duplicate string allocating new memory as PCRE does allocation on its own
        collected[i] = strdup(val);
        if (NULL == collected[i]) {
            log_crit("Could not allocate memory for storing result %ld", (long)i);
            ret = RE_RESOURCE_ERROR;
            for (long k = i - 1; k >= 0; k--) {
                free(collected[k]);
                log_warn("Freeing %ld '%s'", (long)k, collected[k]);
            }
            goto match_free;
        }
    }

    log_info("Filled %ld matched groups", numfound);
    ret = numfound;

match_free:
    log_dbg("Deallocating re match data");
    pcre2_match_data_free(match);
code_free:
    log_dbg("Deallocating re compiled code");
    pcre2_code_free(re);
    return ret;
}


bool uri_parse(const char *uristring, struct socket_uri *resuri)
{
    log_dbg("UNIX socket path maxlen: %ld", UNIX_SOCKET_PATH_MAXLEN);

    bool ret = false;
    if (NULL == resuri || NULL == uristring)
        return ret;   // fail fast

    char *groupvals[arr_len(uri_groupnames) - 1];
    long found = re_collect_named(uri_re, uristring, uri_groupnames, groupvals);
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
    log_info("Freeing intermediate groupvals");
    arr_foreach(v, groupvals) {
        log_dbg("  %s", v);
        free(v);
    }

    return ret;
}
