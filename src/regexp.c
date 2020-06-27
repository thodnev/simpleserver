#include "regexp.h"
#include "macroutils.h"
#include "logging.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if !defined(PCRE2_CODE_UNIT_WIDTH)
#define PCRE2_CODE_UNIT_WIDTH CHAR_BIT
#endif
#include <pcre2.h>      // Requires buildflags as `pkg-config --cflags --libs libpcre2-8` shows


static unsigned long map_compile_flags(unsigned long f)
{
    unsigned long ret = 0;
    if (f & RE_ALLOW_EMPTY_CLASS)   ret |= PCRE2_ALLOW_EMPTY_CLASS;
    if (f & RE_ALT_BSUX)            ret |= PCRE2_ALT_BSUX;
    if (f & RE_AUTO_CALLOUT)        ret |= PCRE2_AUTO_CALLOUT;
    if (f & RE_CASELESS)            ret |= PCRE2_CASELESS;
    if (f & RE_DOLLAR_ENDONLY)      ret |= PCRE2_DOLLAR_ENDONLY;
    if (f & RE_DOTALL)              ret |= PCRE2_DOTALL;
    if (f & RE_DUPNAMES)            ret |= PCRE2_DUPNAMES;
    if (f & RE_EXTENDED)            ret |= PCRE2_EXTENDED;
    if (f & RE_FIRSTLINE)           ret |= PCRE2_FIRSTLINE;
    if (f & RE_MATCH_UNSET_BACKREF) ret |= PCRE2_MATCH_UNSET_BACKREF;
    if (f & RE_MULTILINE)           ret |= PCRE2_MULTILINE;
    if (f & RE_NEVER_UCP)           ret |= PCRE2_NEVER_UCP;
    if (f & RE_NEVER_UTF)           ret |= PCRE2_NEVER_UTF;
    if (f & RE_NO_AUTO_CAPTURE)     ret |= PCRE2_NO_AUTO_CAPTURE;
    if (f & RE_NO_AUTO_POSSESS)     ret |= PCRE2_NO_AUTO_POSSESS;
    if (f & RE_NO_DOTSTAR_ANCHOR)   ret |= PCRE2_NO_DOTSTAR_ANCHOR;
    if (f & RE_NO_START_OPTIMIZE)   ret |= PCRE2_NO_START_OPTIMIZE;
    if (f & RE_UCP)                 ret |= PCRE2_UCP;
    if (f & RE_UNGREEDY)            ret |= PCRE2_UNGREEDY;
    if (f & RE_UTF)                 ret |= PCRE2_UTF;
    return ret;
}


static unsigned long map_flags_jit(unsigned long f)
{
    unsigned long ret = 0;
    if (f & RE_JIT_COMPLETE)        ret |= PCRE2_JIT_COMPLETE;
    if (f & RE_JIT_PARTIAL_SOFT)    ret |= PCRE2_JIT_PARTIAL_SOFT;
    if (f & RE_JIT_PARTIAL_HARD)    ret |= PCRE2_JIT_PARTIAL_HARD;
    return ret;
}


/**
 *  reobj_isvalid() -- sanity check whether passed reobj could be handled
 */
static inline bool reobj_isvalid(const reobj *obj)
{
    return (NULL != obj) && (NULL != obj->re);
}


void re_free(reobj *obj)
{
    log_dbg("re_free wrapper called");
    if (NULL == obj)
        return;

    if (NULL != obj->re) {
        log_dbg("re_free: Deallocating re compiled code");
        pcre2_code_free(obj->re);
    }

    free(obj);
}


reobj *re_init(const char *restr, unsigned long flags, enum re_err *error)
{
    reobj *obj = NULL;
    enum re_err err = RE_OK;
    if (NULL == restr) {
        log_err("Could not initalize re with NULL args given");
        err = RE_WRONG_ARGS;
        goto init_hdlr;
    }

    unsigned long pcre_cflags = map_compile_flags(flags);

    int pcre_err;                 // stores PCRE error code
    PCRE2_SIZE _pcre_erroffset;   // Not used. If we know we had an error, we don't care where
    // Use default compile context
    pcre2_code *re = pcre2_compile(
        (PCRE2_SPTR)restr,                 /* A string containing expression to be compiled */
        PCRE2_ZERO_TERMINATED,             /* The length of the string or PCRE2_ZERO_TERMINATED */
        pcre_cflags,                       /* Option bits */
        &pcre_err,                         /* Where to put an error code */
        &_pcre_erroffset,                  /* Where to put an error offset */
        NULL);                             /* Pointer to a compile context or NULL */
    if (NULL == re) {
        char buffer[100];
        pcre2_get_error_message(pcre_err, (unsigned char *)buffer, arr_len(buffer));
        log_err("Regexp compilation error %d @ %llu : %s",
                 pcre_err, (long long unsigned)_pcre_erroffset, buffer);
        err = RE_WRONG_PATTERN;
        goto init_hdlr;
    }
    log_dbg("Regexp compiled for %p", restr);

    unsigned long pcre_jitflags = map_flags_jit(flags);
    bool isjit = (0 != pcre_jitflags);

    if (isjit) {
        int err = pcre2_jit_compile(re, pcre_jitflags);
        if (err) {
            isjit = false;
            log_crit("Regexp JIT comilation failed with %d", err);
        } else {
            log_dbg("Regexp compiled for JIT");
        }
    }

    obj = malloc(sizeof *obj);
    if (NULL == obj) {
        log_crit("Could not allocate space for re object");
        err = RE_RESOURCE_ERROR;
        goto init_hdlr;
    }

    // Second variant. The first is using memcpy as shown earlier
    *obj = (reobj) {
        .re = re,
        .restr = restr,
        .isjit = isjit
    };

init_hdlr:
    if (NULL != error)
        *error = err;
    return obj;
}


// It proved useful numerous times. So our decicion to make it a separate function was right
// Here we furter refactor it for even better reusability
long re_collect_named(const reobj *obj, const char *string, char **collected,
                      const char **groupnames, size_t ngroups)
{
    if (!reobj_isvalid(obj) || NULL == string || NULL == collected || NULL == groupnames)
        return RE_WRONG_ARGS;   // fail fast

    if (0 == ngroups) {
        // count groups from NULL-terminated array
        while (NULL != groupnames[ngroups])
            ngroups++;
        log_info("Detected %ld groups", ngroups);
    }

    int ret = 0;
    // Create match block with number of entries calculated from our re
    // It will store matched results
    pcre2_match_data *match = pcre2_match_data_create_from_pattern(obj->re, NULL);
    if (NULL == match) {
        log_crit("Match block could not be obtained");
        return RE_RESOURCE_ERROR;
    }

    // Create a pointer to function. Use typeof to avoid specifying prototype manually
    // assign function to pointer depending on obj.isjit value
    typeof(pcre2_jit_match) *mfuncptr = obj->isjit ? &pcre2_jit_match : &pcre2_match;
    // We could always call pcre2_match too. But pcre2_jit_match bypasses some checks
    // and allows for faster matching
    long mcnt = (*mfuncptr)(
        obj->re,                             /* Points to the compiled pattern */
        (PCRE2_SPTR)string,                 /* Points to the subject string */
        (PCRE2_SIZE)strlen(string),         /* Length of the subject string */
        (PCRE2_SIZE)0,                      /* Offset in the subject at which to start matching */
        0,                                  /* Option bits */
        match,                              /* Points to a match data block, for results */
        NULL);                              /* Points to a match context, or is NULL */

    if (mcnt < 1) {
        log_err("No matches. Match count is %ld", mcnt);
        return RE_NOMATCH;
    }
    log_dbg("Match count %ld", mcnt);

#if defined(DEBUG) && (DEBUG >= LOG_DEBUG)
    {
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match);
        for (long i = 0; i <  mcnt; i ++) {
            PCRE2_SPTR substring_start = string + ovector[2*i];
            PCRE2_SIZE substring_length = ovector[2*i+1] - ovector[2*i];
            log_dbg("  Item %2ld: %.*s", i, (int)substring_length, (char *)substring_start);
        }
    }
#endif

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
    return ret;
}
