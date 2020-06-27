/**
 *  Implementation of simple HTTP server on top of sockets
 *
 *  This is a partial implementation and returns error on unsupported features
 */
//


// HTTP message start line (per sec. 3.1. RFC7230)
static const char * const _re_http_startline = (
    /* Sec. 4.1. RFC 7231 */
    "(?P<method> GET|HEAD|POST|PUT|DELETE|CONNECT|OPTIONS|TRACE )"
    "\\s"
    /* not 100% correct domain re, but stupid browser sends */
    /* requests with domains                                */
    "(?: http:\\/\\/ (<?P<domain>"
    "  [a-zA-Z0-9]+ (?: \\. [a-zA-Z0-9]+) *"
    "  ) )?"
    "(?P<target>"
      /* only partial support for origin-form */
    "  \\/ [a-zA-Z0-9\\-\\._~]+ (?: \\/ [a-zA-Z0-9\\-\\._~]+) *"
    ")"
    "\\s"
    /* version */
    "  HTTP \\/ (?P<vermajor> \\d ) \\. (?P<verminor> \\d )"
);
