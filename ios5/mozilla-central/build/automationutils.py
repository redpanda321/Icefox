#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import with_statement
import glob, logging, os, platform, shutil, subprocess, sys, tempfile, urllib2, zipfile
import re
from urlparse import urlparse
from operator import itemgetter

__all__ = [
  "ZipFileReader",
  "addCommonOptions",
  "checkForCrashes",
  "dumpLeakLog",
  "isURL",
  "processLeakLog",
  "getDebuggerInfo",
  "DEBUGGER_INFO",
  "replaceBackSlashes",
  "wrapCommand",
  "ShutdownLeakLogger"
  ]

# Map of debugging programs to information about them, like default arguments
# and whether or not they are interactive.
DEBUGGER_INFO = {
  # gdb requires that you supply the '--args' flag in order to pass arguments
  # after the executable name to the executable.
  "gdb": {
    "interactive": True,
    "args": "-q --args"
  },

  # valgrind doesn't explain much about leaks unless you set the
  # '--leak-check=full' flag.
  "valgrind": {
    "interactive": False,
    "args": "--leak-check=full"
  }
}

class ZipFileReader(object):
  """
  Class to read zip files in Python 2.5 and later. Limited to only what we
  actually use.
  """

  def __init__(self, filename):
    self._zipfile = zipfile.ZipFile(filename, "r")

  def __del__(self):
    self._zipfile.close()

  def _getnormalizedpath(self, path):
    """
    Gets a normalized path from 'path' (or the current working directory if
    'path' is None). Also asserts that the path exists.
    """
    if path is None:
      path = os.curdir
    path = os.path.normpath(os.path.expanduser(path))
    assert os.path.isdir(path)
    return path

  def _extractname(self, name, path):
    """
    Extracts a file with the given name from the zip file to the given path.
    Also creates any directories needed along the way.
    """
    filename = os.path.normpath(os.path.join(path, name))
    if name.endswith("/"):
      os.makedirs(filename)
    else:
      path = os.path.split(filename)[0]
      if not os.path.isdir(path):
        os.makedirs(path)
      with open(filename, "wb") as dest:
        dest.write(self._zipfile.read(name))

  def namelist(self):
    return self._zipfile.namelist()

  def read(self, name):
    return self._zipfile.read(name)

  def extract(self, name, path = None):
    if hasattr(self._zipfile, "extract"):
      return self._zipfile.extract(name, path)

    # This will throw if name is not part of the zip file.
    self._zipfile.getinfo(name)

    self._extractname(name, self._getnormalizedpath(path))

  def extractall(self, path = None):
    if hasattr(self._zipfile, "extractall"):
      return self._zipfile.extractall(path)

    path = self._getnormalizedpath(path)

    for name in self._zipfile.namelist():
      self._extractname(name, path)

log = logging.getLogger()

def isURL(thing):
  """Return True if |thing| looks like a URL."""
  # We want to download URLs like http://... but not Windows paths like c:\...
  return len(urlparse(thing).scheme) >= 2

def addCommonOptions(parser, defaults={}):
  parser.add_option("--xre-path",
                    action = "store", type = "string", dest = "xrePath",
                    # individual scripts will set a sane default
                    default = None,
                    help = "absolute path to directory containing XRE (probably xulrunner)")
  if 'SYMBOLS_PATH' not in defaults:
    defaults['SYMBOLS_PATH'] = None
  parser.add_option("--symbols-path",
                    action = "store", type = "string", dest = "symbolsPath",
                    default = defaults['SYMBOLS_PATH'],
                    help = "absolute path to directory containing breakpad symbols, or the URL of a zip file containing symbols")
  parser.add_option("--debugger",
                    action = "store", dest = "debugger",
                    help = "use the given debugger to launch the application")
  parser.add_option("--debugger-args",
                    action = "store", dest = "debuggerArgs",
                    help = "pass the given args to the debugger _before_ "
                           "the application on the command line")
  parser.add_option("--debugger-interactive",
                    action = "store_true", dest = "debuggerInteractive",
                    help = "prevents the test harness from redirecting "
                        "stdout and stderr for interactive debuggers")

def checkForCrashes(dumpDir, symbolsPath, testName=None):
  stackwalkPath = os.environ.get('MINIDUMP_STACKWALK', None)
  # try to get the caller's filename if no test name is given
  if testName is None:
    try:
      testName = os.path.basename(sys._getframe(1).f_code.co_filename)
    except:
      testName = "unknown"

  # Check preconditions
  dumps = glob.glob(os.path.join(dumpDir, '*.dmp'))
  if len(dumps) == 0:
    return False

  foundCrash = False
  removeSymbolsPath = False

  # If our symbols are at a remote URL, download them now
  if isURL(symbolsPath):
    print "Downloading symbols from: " + symbolsPath
    removeSymbolsPath = True
    # Get the symbols and write them to a temporary zipfile
    data = urllib2.urlopen(symbolsPath)
    symbolsFile = tempfile.TemporaryFile()
    symbolsFile.write(data.read())
    # extract symbols to a temporary directory (which we'll delete after
    # processing all crashes)
    symbolsPath = tempfile.mkdtemp()
    zfile = ZipFileReader(symbolsFile)
    zfile.extractall(symbolsPath)

  try:
    for d in dumps:
      log.info("PROCESS-CRASH | %s | application crashed (minidump found)", testName)
      print "Crash dump filename: " + d
      if symbolsPath and stackwalkPath and os.path.exists(stackwalkPath):
        # run minidump stackwalk
        p = subprocess.Popen([stackwalkPath, d, symbolsPath],
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        (out, err) = p.communicate()
        if len(out) > 3:
          # minidump_stackwalk is chatty, so ignore stderr when it succeeds.
          print out
        else:
          print "stderr from minidump_stackwalk:"
          print err
        if p.returncode != 0:
          print "minidump_stackwalk exited with return code %d" % p.returncode
      else:
        if not symbolsPath:
          print "No symbols path given, can't process dump."
        if not stackwalkPath:
          print "MINIDUMP_STACKWALK not set, can't process dump."
        elif stackwalkPath and not os.path.exists(stackwalkPath):
          print "MINIDUMP_STACKWALK binary not found: %s" % stackwalkPath
      dumpSavePath = os.environ.get('MINIDUMP_SAVE_PATH', None)
      if dumpSavePath:
        shutil.move(d, dumpSavePath)
        print "Saved dump as %s" % os.path.join(dumpSavePath,
                                                os.path.basename(d))
      else:
        os.remove(d)
      extra = os.path.splitext(d)[0] + ".extra"
      if os.path.exists(extra):
        os.remove(extra)
      foundCrash = True
  finally:
    if removeSymbolsPath:
      shutil.rmtree(symbolsPath)

  return foundCrash

def getFullPath(directory, path):
  "Get an absolute path relative to 'directory'."
  return os.path.normpath(os.path.join(directory, os.path.expanduser(path)))

def searchPath(directory, path):
  "Go one step beyond getFullPath and try the various folders in PATH"
  # Try looking in the current working directory first.
  newpath = getFullPath(directory, path)
  if os.path.isfile(newpath):
    return newpath

  # At this point we have to fail if a directory was given (to prevent cases
  # like './gdb' from matching '/usr/bin/./gdb').
  if not os.path.dirname(path):
    for dir in os.environ['PATH'].split(os.pathsep):
      newpath = os.path.join(dir, path)
      if os.path.isfile(newpath):
        return newpath
  return None

def getDebuggerInfo(directory, debugger, debuggerArgs, debuggerInteractive = False):

  debuggerInfo = None

  if debugger:
    debuggerPath = searchPath(directory, debugger)
    if not debuggerPath:
      print "Error: Path %s doesn't exist." % debugger
      sys.exit(1)

    debuggerName = os.path.basename(debuggerPath).lower()

    def getDebuggerInfo(type, default):
      if debuggerName in DEBUGGER_INFO and type in DEBUGGER_INFO[debuggerName]:
        return DEBUGGER_INFO[debuggerName][type]
      return default

    debuggerInfo = {
      "path": debuggerPath,
      "interactive" : getDebuggerInfo("interactive", False),
      "args": getDebuggerInfo("args", "").split()
    }

    if debuggerArgs:
      debuggerInfo["args"] = debuggerArgs.split()
    if debuggerInteractive:
      debuggerInfo["interactive"] = debuggerInteractive

  return debuggerInfo


def dumpLeakLog(leakLogFile, filter = False):
  """Process the leak log, without parsing it.

  Use this function if you want the raw log only.
  Use it preferably with the |XPCOM_MEM_LEAK_LOG| environment variable.
  """

  # Don't warn (nor "info") if the log file is not there.
  if not os.path.exists(leakLogFile):
    return

  leaks = open(leakLogFile, "r")
  leakReport = leaks.read()
  leaks.close()

  # Only |XPCOM_MEM_LEAK_LOG| reports can be actually filtered out.
  # Only check whether an actual leak was reported.
  if filter and not "0 TOTAL " in leakReport:
    return

  # Simply copy the log.
  log.info(leakReport.rstrip("\n"))

def processSingleLeakFile(leakLogFileName, PID, processType, leakThreshold):
  """Process a single leak log, corresponding to the specified
  process PID and type.
  """

  #                  Per-Inst  Leaked      Total  Rem ...
  #   0 TOTAL              17     192  419115886    2 ...
  # 833 nsTimerImpl        60     120      24726    2 ...
  lineRe = re.compile(r"^\s*\d+\s+(?P<name>\S+)\s+"
                      r"(?P<size>-?\d+)\s+(?P<bytesLeaked>-?\d+)\s+"
                      r"-?\d+\s+(?P<numLeaked>-?\d+)")

  processString = ""
  if PID and processType:
    processString = "| %s process %s " % (processType, PID)
  leaks = open(leakLogFileName, "r")
  for line in leaks:
    matches = lineRe.match(line)
    if (matches and
        int(matches.group("numLeaked")) == 0 and
        matches.group("name") != "TOTAL"):
      continue
    log.info(line.rstrip())
  leaks.close()

  leaks = open(leakLogFileName, "r")
  seenTotal = False
  crashedOnPurpose = False
  prefix = "TEST-PASS"
  numObjects = 0
  for line in leaks:
    if line.find("purposefully crash") > -1:
      crashedOnPurpose = True
    matches = lineRe.match(line)
    if not matches:
      continue
    name = matches.group("name")
    size = int(matches.group("size"))
    bytesLeaked = int(matches.group("bytesLeaked"))
    numLeaked = int(matches.group("numLeaked"))
    if size < 0 or bytesLeaked < 0 or numLeaked < 0:
      log.info("TEST-UNEXPECTED-FAIL %s| automationutils.processLeakLog() | negative leaks caught!" %
               processString)
      if name == "TOTAL":
        seenTotal = True
    elif name == "TOTAL":
      seenTotal = True
      # Check for leaks.
      if bytesLeaked < 0 or bytesLeaked > leakThreshold:
        prefix = "TEST-UNEXPECTED-FAIL"
        leakLog = "TEST-UNEXPECTED-FAIL %s| automationutils.processLeakLog() | leaked" \
                  " %d bytes during test execution" % (processString, bytesLeaked)
      elif bytesLeaked > 0:
        leakLog = "TEST-PASS %s| automationutils.processLeakLog() | WARNING leaked" \
                  " %d bytes during test execution" % (processString, bytesLeaked)
      else:
        leakLog = "TEST-PASS %s| automationutils.processLeakLog() | no leaks detected!" \
                  % processString
      # Remind the threshold if it is not 0, which is the default/goal.
      if leakThreshold != 0:
        leakLog += " (threshold set at %d bytes)" % leakThreshold
      # Log the information.
      log.info(leakLog)
    else:
      if numLeaked != 0:
        if numLeaked > 1:
          instance = "instances"
          rest = " each (%s bytes total)" % matches.group("bytesLeaked")
        else:
          instance = "instance"
          rest = ""
        numObjects += 1
        if numObjects > 5:
          # don't spam brief tinderbox logs with tons of leak output
          prefix = "TEST-INFO"
        log.info("%(prefix)s %(process)s| automationutils.processLeakLog() | leaked %(numLeaked)d %(instance)s of %(name)s "
                 "with size %(size)s bytes%(rest)s" %
                 { "prefix": prefix,
                   "process": processString,
                   "numLeaked": numLeaked,
                   "instance": instance,
                   "name": name,
                   "size": matches.group("size"),
                   "rest": rest })
  if not seenTotal:
    if crashedOnPurpose:
      log.info("INFO | automationutils.processLeakLog() | process %s was " \
               "deliberately crashed and thus has no leak log" % PID)
    else:
      log.info("TEST-UNEXPECTED-FAIL %s| automationutils.processLeakLog() | missing output line for total leaks!" %
             processString)
  leaks.close()


def processLeakLog(leakLogFile, leakThreshold = 0):
  """Process the leak log, including separate leak logs created
  by child processes.

  Use this function if you want an additional PASS/FAIL summary.
  It must be used with the |XPCOM_MEM_BLOAT_LOG| environment variable.
  """

  if not os.path.exists(leakLogFile):
    log.info("WARNING | automationutils.processLeakLog() | refcount logging is off, so leaks can't be detected!")
    return

  (leakLogFileDir, leakFileBase) = os.path.split(leakLogFile)
  pidRegExp = re.compile(r".*?_([a-z]*)_pid(\d*)$")
  if leakFileBase[-4:] == ".log":
    leakFileBase = leakFileBase[:-4]
    pidRegExp = re.compile(r".*?_([a-z]*)_pid(\d*).log$")

  for fileName in os.listdir(leakLogFileDir):
    if fileName.find(leakFileBase) != -1:
      thisFile = os.path.join(leakLogFileDir, fileName)
      processPID = 0
      processType = None
      m = pidRegExp.search(fileName)
      if m:
        processType = m.group(1)
        processPID = m.group(2)
      processSingleLeakFile(thisFile, processPID, processType, leakThreshold)

def replaceBackSlashes(input):
  return input.replace('\\', '/')

def wrapCommand(cmd):
  """
  If running on OS X 10.5 or older, wrap |cmd| so that it will
  be executed as an i386 binary, in case it's a 32-bit/64-bit universal
  binary.
  """
  if platform.system() == "Darwin" and \
     hasattr(platform, 'mac_ver') and \
     platform.mac_ver()[0][:4] < '10.6':
    return ["arch", "-arch", "i386"] + cmd
  # otherwise just execute the command normally
  return cmd

class ShutdownLeakLogger(object):
  """
  Parses the mochitest run log when running a debug build, assigns all leaked
  DOM windows (that are still around after test suite shutdown, despite running
  the GC) to the tests that created them and prints leak statistics.
  """
  MAX_LEAK_COUNT = 7

  def __init__(self, logger):
    self.logger = logger
    self.tests = []
    self.leakedWindows = {}
    self.leakedDocShells = set()
    self.currentTest = None
    self.seenShutdown = False

  def log(self, line):
    if line[2:11] == "DOMWINDOW":
      self._logWindow(line)
    elif line[2:10] == "DOCSHELL":
      self._logDocShell(line)
    elif line.startswith("TEST-START"):
      fileName = line.split(" ")[-1].strip().replace("chrome://mochitests/content/browser/", "")
      self.currentTest = {"fileName": fileName, "windows": set(), "docShells": set()}
    elif line.startswith("INFO TEST-END"):
      # don't track a test if no windows or docShells leaked
      if self.currentTest and (self.currentTest["windows"] or self.currentTest["docShells"]):
        self.tests.append(self.currentTest)
      self.currentTest = None
    elif line.startswith("INFO TEST-START | Shutdown"):
      self.seenShutdown = True

  def parse(self):
    leakingTests = self._parseLeakingTests()

    if leakingTests:
      totalWindows = sum(len(test["leakedWindows"]) for test in leakingTests)
      totalDocShells = sum(len(test["leakedDocShells"]) for test in leakingTests)
      msgType = "TEST-INFO" if totalWindows + totalDocShells <= self.MAX_LEAK_COUNT else "TEST-UNEXPECTED-FAIL"
      self.logger.info("%s | ShutdownLeaks | leaked %d DOMWindow(s) and %d DocShell(s) until shutdown", msgType, totalWindows, totalDocShells)

    for test in leakingTests:
      for url, count in self._zipLeakedWindows(test["leakedWindows"]):
        self.logger.info("%s | %s | leaked %d window(s) until shutdown [url = %s]", msgType, test["fileName"], count, url)

      if test["leakedDocShells"]:
        self.logger.info("%s | %s | leaked %d docShell(s) until shutdown", msgType, test["fileName"], len(test["leakedDocShells"]))

  def _logWindow(self, line):
    created = line[:2] == "++"
    id = self._parseValue(line, "serial")

    # log line has invalid format
    if not id:
      return

    if self.currentTest:
      windows = self.currentTest["windows"]
      if created:
        windows.add(id)
      else:
        windows.discard(id)
    elif self.seenShutdown and not created:
      self.leakedWindows[id] = self._parseValue(line, "url")

  def _logDocShell(self, line):
    created = line[:2] == "++"
    id = self._parseValue(line, "id")

    # log line has invalid format
    if not id:
      return

    if self.currentTest:
      docShells = self.currentTest["docShells"]
      if created:
        docShells.add(id)
      else:
        docShells.discard(id)
    elif self.seenShutdown and not created:
      self.leakedDocShells.add(id)

  def _parseValue(self, line, name):
    match = re.search("\[%s = (.+?)\]" % name, line)
    if match:
      return match.group(1)
    return None

  def _parseLeakingTests(self):
    leakingTests = []

    for test in self.tests:
      test["leakedWindows"] = [self.leakedWindows[id] for id in test["windows"] if id in self.leakedWindows]
      test["leakedDocShells"] = [id for id in test["docShells"] if id in self.leakedDocShells]
      test["leakCount"] = len(test["leakedWindows"]) + len(test["leakedDocShells"])

      if test["leakCount"]:
        leakingTests.append(test)

    return sorted(leakingTests, key=itemgetter("leakCount"), reverse=True)

  def _zipLeakedWindows(self, leakedWindows):
    counts = []
    counted = set()

    for url in leakedWindows:
      if not url in counted:
        counts.append((url, leakedWindows.count(url)))
        counted.add(url)

    return sorted(counts, key=itemgetter(1), reverse=True)
