# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

'''A generic script to verify all test files are in the 
corresponding .ini file.

Usage: xpccheck.py <directory> [<directory> ...]
'''

import sys
import os
from glob import glob
import manifestparser

class srcManifestParser(manifestparser.ManifestParser):
  def __init__(self, manifests=(), defaults=None, strict=True, testroot=None):
    self.testroot = testroot
    manifestparser.ManifestParser.__init__(self, manifests, defaults, strict)

  def getRelativeRoot(self, here):
    if self.testroot is None:
        return manifestparser.ManifestParser.getRelativeRoot(self, self.rootdir)
    return self.testroot


def getIniTests(testdir):
  mp = manifestparser.ManifestParser(strict=False)
  mp.read(os.path.join(testdir, 'xpcshell.ini'))
  return mp.tests

def verifyDirectory(initests, directory):
  files = glob(os.path.join(os.path.abspath(directory), "test_*"))
  for f in files:
    if (not os.path.isfile(f)):
      continue

    name = os.path.basename(f)
    if name.endswith('.in'):
      name = name[:-3]

    if not name.endswith('.js'):
      continue

    found = False
    for test in initests:
      if os.path.join(os.path.abspath(directory), name) == test['path']:
        found = True
        break
   
    if not found:
      print >>sys.stderr, "TEST-UNEXPECTED-FAIL | xpccheck | test %s is missing from test manifest %s!" % (name, os.path.join(directory, 'xpcshell.ini'))
      sys.exit(1)

def verifyIniFile(initests, directory):
  files = glob(os.path.join(os.path.abspath(directory), "test_*"))
  for test in initests:
    name = test['path'].split('/')[-1]

    found = False
    for f in files:

      fname = f.split('/')[-1]
      if fname.endswith('.in'):
        fname = '.in'.join(fname.split('.in')[:-1])

      if os.path.join(os.path.abspath(directory), fname) == test['path']:
        found = True
        break

    if not found:
      print >>sys.stderr, "TEST-UNEXPECTED-FAIL | xpccheck | found %s in xpcshell.ini and not in directory '%s'" % (name, directory)
      sys.exit(1)

def verifyMasterIni(mastername, topsrcdir, directory):
  mp = srcManifestParser(strict=False, testroot=topsrcdir)
  mp.read(mastername)
  tests = mp.tests

  found = False
  for test in tests:
    if test['manifest'] == os.path.abspath(os.path.join(directory, 'xpcshell.ini')):
      found = True
      break

  if not found:
    print >>sys.stderr, "TEST-UNEXPECTED-FAIL | xpccheck | directory %s is missing from master xpcshell.ini file %s" % (directory, mastername)
    sys.exit(1)


if __name__ == '__main__':
  if len(sys.argv) < 4:
    print >>sys.stderr, "Usage: xpccheck.py <topsrcdir> <path/master.ini> <directory> [<directory> ...]"
    sys.exit(1)

  topsrcdir = sys.argv[1]
  for d in sys.argv[3:]:
    # xpcshell-unpack is a copy of xpcshell sibling directory and in the Makefile
    # we copy all files (including xpcshell.ini from the sibling directory.
    if d.endswith('toolkit/mozapps/extensions/test/xpcshell-unpack'):
      continue

    initests = getIniTests(d)
    verifyDirectory(initests, d)
    verifyIniFile(initests, d)
    verifyMasterIni(sys.argv[2], topsrcdir, d)


