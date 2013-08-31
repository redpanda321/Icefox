#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os, unittest, subprocess, tempfile, shutil, urlparse, zipfile, StringIO
import mozcrash, mozlog, mozhttpd

# Make logs go away
log = mozlog.getLogger("mozcrash", os.devnull)

def popen_factory(stdouts):
    """
    Generate a class that can mock subprocess.Popen. |stdouts| is an iterable that
    should return an iterable for the stdout of each process in turn.
    """
    class mock_popen(object):
        def __init__(self, args, *args_rest, **kwargs):
            self.stdout = stdouts.next()
            self.returncode = 0

        def wait(self):
            return 0

        def communicate(self):
            return (self.stdout.next(), "")

    return mock_popen

class TestCrash(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.mkdtemp()
        # a fake file to use as a stackwalk binary
        self.stackwalk = os.path.join(self.tempdir, "stackwalk")
        open(self.stackwalk, "w").write("fake binary")
        self._subprocess_popen = subprocess.Popen
        subprocess.Popen = popen_factory(self.next_mock_stdout())
        self.stdouts = []

    def tearDown(self):
        subprocess.Popen = self._subprocess_popen
        shutil.rmtree(self.tempdir)

    def next_mock_stdout(self):
        if not self.stdouts:
            yield iter([])
        for s in self.stdouts:
            yield iter(s)

    def test_nodumps(self):
        """
        Test that check_for_crashes returns False if no dumps are present.
        """
        self.stdouts.append(["this is some output"])
        self.assertFalse(mozcrash.check_for_crashes(self.tempdir,
                                                    'symbols_path',
                                                    stackwalk_binary=self.stackwalk))

    def test_simple(self):
        """
        Test that check_for_crashes returns True if a dump is present.
        """
        open(os.path.join(self.tempdir, "test.dmp"), "w").write("foo")
        self.stdouts.append(["this is some output"])
        self.assert_(mozcrash.check_for_crashes(self.tempdir,
                                                'symbols_path',
                                                stackwalk_binary=self.stackwalk))

    def test_stackwalk_envvar(self):
        """
        Test that check_for_crashes uses the MINIDUMP_STACKWALK environment var.
        """
        open(os.path.join(self.tempdir, "test.dmp"), "w").write("foo")
        self.stdouts.append(["this is some output"])
        os.environ['MINIDUMP_STACKWALK'] = self.stackwalk
        self.assert_(mozcrash.check_for_crashes(self.tempdir,
                                                'symbols_path'))
        del os.environ['MINIDUMP_STACKWALK']

    def test_save_path(self):
        """
        Test that dump_save_path works.
        """
        open(os.path.join(self.tempdir, "test.dmp"), "w").write("foo")
        save_path = os.path.join(self.tempdir, "saved")
        os.mkdir(save_path)
        self.stdouts.append(["this is some output"])
        self.assert_(mozcrash.check_for_crashes(self.tempdir,
                                                'symbols_path',
                                                stackwalk_binary=self.stackwalk,
                                                dump_save_path=save_path))
        self.assert_(os.path.isfile(os.path.join(save_path, "test.dmp")))

    def test_save_path_envvar(self):
        """
        Test that the MINDUMP_SAVE_PATH environment variable works.
        """
        open(os.path.join(self.tempdir, "test.dmp"), "w").write("foo")
        save_path = os.path.join(self.tempdir, "saved")
        os.mkdir(save_path)
        self.stdouts.append(["this is some output"])
        os.environ['MINIDUMP_SAVE_PATH'] = save_path
        self.assert_(mozcrash.check_for_crashes(self.tempdir,
                                                'symbols_path',
                                                stackwalk_binary=self.stackwalk))
        del os.environ['MINIDUMP_SAVE_PATH']
        self.assert_(os.path.isfile(os.path.join(save_path, "test.dmp")))

    def test_symbol_path_url(self):
        """
        Test that passing a URL as symbols_path correctly fetches the URL.
        """
        open(os.path.join(self.tempdir, "test.dmp"), "w").write("foo")
        self.stdouts.append(["this is some output"])

        def make_zipfile():
            data = StringIO.StringIO()
            z = zipfile.ZipFile(data, 'w')
            z.writestr("symbols.txt", "abc/xyz")
            z.close()
            return data.getvalue()
        def get_symbols(req):
            headers = {}
            return (200, headers, make_zipfile())
        httpd = mozhttpd.MozHttpd(port=0,
                                  urlhandlers=[{'method':'GET', 'path':'/symbols', 'function':get_symbols}])
        httpd.start()
        symbol_url = urlparse.urlunsplit(('http', '%s:%d' % httpd.httpd.server_address,
                                        '/symbols','',''))
        self.assert_(mozcrash.check_for_crashes(self.tempdir,
                                                symbol_url,
                                                stackwalk_binary=self.stackwalk))

if __name__ == '__main__':
    unittest.main()
