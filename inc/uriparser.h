#include "commondefs.h"
#include <stdbool.h>

enum re_err {
    RE_OK = 0,
    RE_WRONG_ARGS = -1,
    RE_WRONG_PATTERN = -2,
    RE_RESOURCE_ERROR = -3,
    RE_NOMATCH = -4
};

bool uri_parse(const char *uristring, struct socket_uri *resuri);
