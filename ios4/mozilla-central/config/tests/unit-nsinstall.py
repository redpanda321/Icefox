import unittest

import os, sys, os.path, time
from tempfile import mkdtemp
from shutil import rmtree
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

from nsinstall import nsinstall

class TestNsinstall(unittest.TestCase):
    """
    Unit tests for nsinstall.py
    """
    def setUp(self):
        self.tmpdir = mkdtemp()

    def tearDown(self):
        rmtree(self.tmpdir)

    # utility methods for tests
    def touch(self, file, dir=None):
        if dir is None:
            dir = self.tmpdir
        f = os.path.join(dir, file)
        open(f, 'w').close()
        return f

    def mkdirs(self, dir):
        d = os.path.join(self.tmpdir, dir)
        os.makedirs(d)
        return d

    def test_nsinstall_D(self):
        "Test nsinstall -D <dir>"
        testdir = os.path.join(self.tmpdir, "test")
        self.assertEqual(nsinstall(["-D", testdir]), 0)
        self.assert_(os.path.isdir(testdir))

    def test_nsinstall_basic(self):
        "Test nsinstall <file> <dir>"
        testfile = self.touch("testfile")
        testdir = self.mkdirs("testdir")
        self.assertEqual(nsinstall([testfile, testdir]), 0)
        self.assert_(os.path.isfile(os.path.join(testdir, "testfile")))

    def test_nsinstall_basic_recursive(self):
        "Test nsinstall <dir> <dest dir>"
        sourcedir = self.mkdirs("sourcedir")
        self.touch("testfile", sourcedir)
        destdir = self.mkdirs("destdir")
        self.assertEqual(nsinstall([sourcedir, destdir]), 0)
        testdir = os.path.join(destdir, "sourcedir")
        self.assert_(os.path.isdir(testdir))
        self.assert_(os.path.isfile(os.path.join(testdir, "testfile")))

    def test_nsinstall_multiple(self):
        "Test nsinstall <three files> <dest dir>"
        testfiles = [self.touch("testfile1"),
                     self.touch("testfile2"),
                     self.touch("testfile3")]
        testdir = self.mkdirs("testdir")
        self.assertEqual(nsinstall(testfiles + [testdir]), 0)
        for f in testfiles:
            self.assert_(os.path.isfile(os.path.join(testdir,
                                                     os.path.basename(f))))

    def test_nsinstall_dir_exists(self):
        "Test nsinstall <dir> <dest dir>, where <dest dir>/<dir> already exists"
        srcdir = self.mkdirs("test")
        destdir = self.mkdirs("testdir/test")
        self.assertEqual(nsinstall([srcdir, os.path.dirname(destdir)]), 0)
        self.assert_(os.path.isdir(destdir))

    def test_nsinstall_t(self):
        "Test that nsinstall -t works (preserve timestamp)"
        testfile = self.touch("testfile")
        testdir = self.mkdirs("testdir")
        # set mtime to now - 30 seconds
        t = int(time.time()) - 30
        os.utime(testfile, (t, t))
        self.assertEqual(nsinstall(["-t", testfile, testdir]), 0)
        destfile = os.path.join(testdir, "testfile")
        self.assert_(os.path.isfile(destfile))
        self.assertEqual(os.stat(testfile).st_mtime,
                         os.stat(destfile).st_mtime)

    if sys.platform != "win32":
        # can't run this test on windows, don't have real file modes there
        def test_nsinstall_m(self):
            "Test that nsinstall -m works (set mode)"
            testfile = self.touch("testfile")
            mode = 0600
            os.chmod(testfile, mode)
            testdir = self.mkdirs("testdir")
            self.assertEqual(nsinstall(["-m", "%04o" % mode, testfile, testdir]), 0)
            destfile = os.path.join(testdir, "testfile")
            self.assert_(os.path.isfile(destfile))
            self.assertEqual(os.stat(testfile).st_mode,
                             os.stat(destfile).st_mode)

    def test_nsinstall_d(self):
        "Test that nsinstall -d works (create directories in target)"
        # -d makes no sense to me, but ok!
        testfile = self.touch("testfile")
        testdir = self.mkdirs("testdir")
        destdir = os.path.join(testdir, "subdir")
        self.assertEqual(nsinstall(["-d", testfile, destdir]), 0)
        self.assert_(os.path.isdir(os.path.join(destdir, "testfile")))

    #TODO: implement -R, -l, -L and test them!

if __name__ == '__main__':
  unittest.main()
