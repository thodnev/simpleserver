#include <stdbool.h>
#include <stddef.h>

// it will be opaque object
typedef struct reobj {
    void *re;               /* Compiled regexp                     */
    const char *restr;      /* Re format (specfication) string     */
    bool isjit;             /* Flag indicating whether JIT is used */
} reobj;


enum re_err {
    RE_OK = 0,
    RE_WRONG_ARGS = -1,
    RE_WRONG_PATTERN = -2,
    RE_RESOURCE_ERROR = -3,
    RE_NOMATCH = -4
};


// regexp flags abstraction
enum re_flags {
    // Currently compilaton flags map 1:1 to PCRE2
    RE_ALLOW_EMPTY_CLASS   = 0x00000001UL,
    RE_ALT_BSUX            = 0x00000002UL,
    RE_AUTO_CALLOUT        = 0x00000004UL,
    RE_CASELESS            = 0x00000008UL,
    RE_DOLLAR_ENDONLY      = 0x00000010UL,
    RE_DOTALL              = 0x00000020UL,
    RE_DUPNAMES            = 0x00000040UL,
    RE_EXTENDED            = 0x00000080UL,
    RE_FIRSTLINE           = 0x00000100UL,
    RE_MATCH_UNSET_BACKREF = 0x00000200UL,
    RE_MULTILINE           = 0x00000400UL,
    RE_NEVER_UCP           = 0x00000800UL,
    RE_NEVER_UTF           = 0x00001000UL,
    RE_NO_AUTO_CAPTURE     = 0x00002000UL,
    RE_NO_AUTO_POSSESS     = 0x00004000UL,
    RE_NO_DOTSTAR_ANCHOR   = 0x00008000UL,
    RE_NO_START_OPTIMIZE   = 0x00010000UL,
    RE_UCP                 = 0x00020000UL,
    RE_UNGREEDY            = 0x00040000UL,
    RE_UTF                 = 0x00080000UL,
    // JIT flags also included here because we want to allow them combined
    // with other flags.
    // *NOTE*: JIT flag values DO NOT match PCRE ones
    RE_JIT_COMPLETE        = 0x20000000UL,
    RE_JIT_PARTIAL_SOFT    = 0x40000000UL,
    RE_JIT_PARTIAL_HARD    = 0x80000000UL
};


void re_free(reobj *obj);

reobj *re_init(const char *restr, unsigned long flags, enum re_err *error);

long re_collect_named(const reobj *obj, const char *string, char **collected,
                      const char **groupnames, size_t ngroups);
