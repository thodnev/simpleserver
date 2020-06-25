/**
 *  General-purpose macro utilities
 */

#define arr_len(array) (sizeof (array) / sizeof (*(array)))
#define arr_foreach(var, arr) \
    for (long keep = 1, cnt = 0, size = arr_len(arr); \
              keep && (cnt < size); keep = !keep, cnt++) \
        for (typeof(*(arr)) var = (arr)[cnt]; keep; keep = !keep)

#define except(condition, seterr, errptr, gotoptr) \
    {                                              \
        bool cond = (condition);                   \
        if (cond) {                                \
            errptr = (seterr);                     \
            goto gotoptr;                          \
        }                                          \
        cond;                                      \
    }
