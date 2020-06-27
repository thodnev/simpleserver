# encoding=utf8

"""
This is a demo of simple unit test for C using Python
"""


import pyregexp as pr
import unittest as ut


class SimpleRegexpTest(ut.TestCase):
    def test_compile(self):
        r = pr.RegExp(rb'some (?P<key>.*)', pr.RE_UTF)

    def test_exc(self):
        with self.assertRaises(pr.RegExpError):
            # unknown key used here
            r = pr.RegExp(rb'some (?D<key>.*)', pr.RE_UTF)

    def test_collect_exists(self):
        r = pr.RegExp(rb'some (?P<key>.*)', pr.RE_UTF)
        ret = r.collect_named(b'some word', b'key')
        self.assertEqual(ret, {b'key': b'word'})
