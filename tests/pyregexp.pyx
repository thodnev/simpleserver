"""
Python interface to regexp.h
This file should be build with Cython

To build without hassle:
        pcrecflags=`pkg-config --cflags libpcre2-8`
        pcreldflags=`pkg-config --libs libpcre2-8`
        pyflags=`pkg-config --cflags --libs python3`
    will collect required flags

        gcc -O2 -I../inc ${pcrecflags} -fPIC ../src/regexp.c -c
    will compile .o of the used functions

        cython -3 -I../inc pyregexp.pyx
    will produce .c representation

        gcc -O2 ${pyflags} -I../inc -fPIC pyregexp.c -c
    will compile .o of cythonized extension

        gcc -O2 -shared -fPIC ${pyflags} ${pcreldflags} \
        pyregexp.o regexp.o -o pyregexp.so
    will build Python C extension

    Note: using -DDEBUG=5 when compiling original code will show you all the power
          of logging.
    One of interesting features is how the reobj gets deallocated:
        - As soon as Python don't need an object (in python terms) it can be garbage collected
        - GC removes all internals of object (inner Python objects) one by one
        - We have our reobj wrapped in Python object capsule, which calls corresponding
          deallocate function when object is reclaimed
        - And we mapped this capsule dealloc function to our re_free
        - profit!

Then you can open Python and import the extension like:
    import pyregexp

    dir(pyregexp)       # see what's inside
    help(pyregexp)      # see some help

    # create object
    r = pyregexp.RegExp(b"hello (?P<t>\\w*)", pyregexp.RE_UTF)

    # expected error
    r = pyregexp.RegExp(b"hello (?", pyregexp.RE_UTF)
"""

from libc.stddef cimport size_t
from libc.stdlib cimport malloc, free
from cpython.pycapsule cimport *


# definition of the stuff

cdef extern from "regexp.h":
    ctypedef struct reobj:
        void *re
        const char *restr
        bint isjit

    cpdef enum re_err:
        RE_OK = 0
        RE_WRONG_ARGS = -1
        RE_WRONG_PATTERN = -2
        RE_RESOURCE_ERROR = -3
        RE_NOMATCH = -4

    cpdef enum re_flags:
        RE_ALLOW_EMPTY_CLASS   = 0x00000001
        RE_ALT_BSUX            = 0x00000002
        RE_AUTO_CALLOUT        = 0x00000004
        RE_CASELESS            = 0x00000008
        RE_DOLLAR_ENDONLY      = 0x00000010
        RE_DOTALL              = 0x00000020
        RE_DUPNAMES            = 0x00000040
        RE_EXTENDED            = 0x00000080
        RE_FIRSTLINE           = 0x00000100
        RE_MATCH_UNSET_BACKREF = 0x00000200
        RE_MULTILINE           = 0x00000400
        RE_NEVER_UCP           = 0x00000800
        RE_NEVER_UTF           = 0x00001000
        RE_NO_AUTO_CAPTURE     = 0x00002000
        RE_NO_AUTO_POSSESS     = 0x00004000
        RE_NO_DOTSTAR_ANCHOR   = 0x00008000
        RE_NO_START_OPTIMIZE   = 0x00010000
        RE_UCP                 = 0x00020000
        RE_UNGREEDY            = 0x00040000
        RE_UTF                 = 0x00080000
        RE_JIT_COMPLETE        = 0x20000000
        RE_JIT_PARTIAL_SOFT    = 0x40000000
        RE_JIT_PARTIAL_HARD    = 0x80000000

    cdef void re_free(reobj *obj)

    cdef reobj *re_init(const char *restr, unsigned long flags, re_err *error)

    cdef long re_collect_named(const reobj *obj, const char *string, char **collected,
                                const char **groupnames, size_t ngroups)


# c wrappers
cdef void py_re_free(object cap):
    cdef reobj *obj = <reobj *>PyCapsule_GetPointer(cap, PyCapsule_GetName(cap))
    re_free(obj)


# python world wrappers

class RegExpError(Exception):
    def __init__(self, re_err error, *args):
        err = re_err(error)             # convert C world -> Python world
        super().__init__(*args, err)


class RegExp:
    def __init__(self, const char *restr, unsigned long flags):
        cdef re_err error = re_err.RE_OK
        cdef reobj *obj = re_init(restr, flags, &error)
        if error != re_err.RE_OK:
            raise RegExpError(error)
        self._obj = PyCapsule_New(<void *>obj, "reobj", &py_re_free)

    def collect_named(self, const char *string, *groupnames):
        groupnames = list(groupnames)
        assert all([isinstance(i, bytes) for i in groupnames])

        #allocate buffer for storing strings
        cdef char **buf = <char **>malloc(len(groupnames) * sizeof(char *))
        if not buf:
            raise MemoryError('malloc')
        cdef char **ret = <char **>malloc(len(groupnames) * sizeof(char *))
        if not ret:
            free(buf)
            raise MemoryError('malloc')

        cdef reobj *obj = <reobj *>PyCapsule_GetPointer(self._obj, PyCapsule_GetName(self._obj))
        cdef long err
        try:
            for ix, bt in enumerate(groupnames):
                buf[ix] = bt        # cython implicitly converts strings 

            err = re_collect_named(obj, string, ret, buf, len(groupnames))
            if err < 0:
                raise RegExpError(<re_err>err)

            arr = [<bytes>ret[i] if ret[i] != NULL else None for i in range(len(groupnames))]
            for i in range(len(groupnames)):
                free(ret[i])
                
            dct = dict(zip(groupnames, arr))
            return dct
        finally:
            free(ret)
            free(buf)
