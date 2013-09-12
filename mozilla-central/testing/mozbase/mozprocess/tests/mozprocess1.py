#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
import unittest
from time import sleep

from mozprocess import processhandler

here = os.path.dirname(os.path.abspath(__file__))

def make_proclaunch(aDir):
    """
        Makes the proclaunch executable.
        Params:
            aDir - the directory in which to issue the make commands
        Returns:
            the path to the proclaunch executable that is generated
    """
    # Ideally make should take care of this, but since it doesn't - on windows,
    # anyway, let's just call out both targets explicitly.
    p = subprocess.call(["make", "-C", "iniparser"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=aDir)
    p = subprocess.call(["make"],stdout=subprocess.PIPE, stderr=subprocess.PIPE ,cwd=aDir)
    if sys.platform == "win32":
        exepath = os.path.join(aDir, "proclaunch.exe")
    else:
        exepath = os.path.join(aDir, "proclaunch")
    return exepath

def check_for_process(processName):
    """
        Use to determine if process of the given name is still running.

        Returns:
        detected -- True if process is detected to exist, False otherwise
        output -- if process exists, stdout of the process, '' otherwise
    """
    output = ''
    if sys.platform == "win32":
        # On windows we use tasklist
        p1 = subprocess.Popen(["tasklist"], stdout=subprocess.PIPE)
        output = p1.communicate()[0]
        detected = False
        for line in output.splitlines():
            if processName in line:
                detected = True
                break
    else:
        p1 = subprocess.Popen(["ps", "-ef"], stdout=subprocess.PIPE)
        p2 = subprocess.Popen(["grep", processName], stdin=p1.stdout, stdout=subprocess.PIPE)
        p1.stdout.close()
        output = p2.communicate()[0]
        detected = False
        for line in output.splitlines():
            if "grep %s" % processName in line:
                continue
            elif processName in line and not 'defunct' in line: 
                detected = True
                break

    return detected, output


class ProcTest1(unittest.TestCase):

    def __init__(self, *args, **kwargs):

        # Ideally, I'd use setUpClass but that only exists in 2.7.
        # So, we'll do this make step now.
        self.proclaunch = make_proclaunch(here)
        unittest.TestCase.__init__(self, *args, **kwargs)

    def test_process_normal_finish(self):
        """Process is started, runs to completion while we wait for it"""

        p = processhandler.ProcessHandler([self.proclaunch, "process_normal_finish.ini"],
                                          cwd=here)
        p.run()
        p.wait()

        detected, output = check_for_process(self.proclaunch)
        self.determine_status(detected,
                              output,
                              p.proc.returncode,
                              p.didTimeout)

    def test_process_waittimeout(self):
        """ Process is started, runs but we time out waiting on it
            to complete
        """
        p = processhandler.ProcessHandler([self.proclaunch, "process_waittimeout.ini"],
                                          cwd=here)
        p.run(timeout=10)
        p.wait()

        detected, output = check_for_process(self.proclaunch)
        self.determine_status(detected,
                              output,
                              p.proc.returncode,
                              p.didTimeout,
                              False,
                              ['returncode', 'didtimeout'])

    def test_process_kill(self):
        """ Process is started, we kill it
        """
        p = processhandler.ProcessHandler([self.proclaunch, "process_normal_finish.ini"],
                                          cwd=here)
        p.run()
        p.kill()

        detected, output = check_for_process(self.proclaunch)
        self.determine_status(detected,
                              output,
                              p.proc.returncode,
                              p.didTimeout)

    def determine_status(self,
                         detected=False,
                         output='',
                         returncode=0,
                         didtimeout=False,
                         isalive=False,
                         expectedfail=[]):
        """
        Use to determine if the situation has failed.
        Parameters:
            detected -- value from check_for_process to determine if the process is detected
            output -- string of data from detected process, can be ''
            returncode -- return code from process, defaults to 0
            didtimeout -- True if process timed out, defaults to False
            isalive -- Use True to indicate we pass if the process exists; however, by default
                       the test will pass if the process does not exist (isalive == False)
            expectedfail -- Defaults to [], used to indicate a list of fields that are expected to fail
        """
        if 'returncode' in expectedfail:
            self.assertTrue(returncode, "Detected an unexpected return code of: %s" % returncode)
        elif not isalive:
            self.assertTrue(returncode == 0, "Detected non-zero return code of: %d" % returncode)

        if 'didtimeout' in expectedfail:
            self.assertTrue(didtimeout, "Detected that process didn't time out")
        else:
            self.assertTrue(not didtimeout, "Detected that process timed out")

        if isalive:
            self.assertTrue(detected, "Detected process is not running, process output: %s" % output)
        else:
            self.assertTrue(not detected, "Detected process is still running, process output: %s" % output)

if __name__ == '__main__':
    unittest.main()
