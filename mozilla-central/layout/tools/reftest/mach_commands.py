# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import unicode_literals

import os
import re

from mozbuild.base import (
    MachCommandBase,
    MozbuildObject,
)

from moztesting.util import parse_test_path

from mach.decorators import (
    CommandArgument,
    CommandProvider,
    Command,
)


DEBUGGER_HELP = 'Debugger binary to run test in. Program name or path.'


class ReftestRunner(MozbuildObject):
    """Easily run reftests.

    This currently contains just the basics for running reftests. We may want
    to hook up result parsing, etc.
    """

    def _manifest_file(self, suite):
        """Returns the manifest file used for a given test suite."""
        files = {
          'reftest': 'reftest.list',
          'crashtest': 'crashtests.list'
        }
        assert suite in files
        return files[suite]

    def _find_manifest(self, suite, test_file):
        assert test_file
        parsed = parse_test_path(test_file, self.topsrcdir)
        if parsed['is_dir']:
            return os.path.join(parsed['normalized'], self._manifest_file(suite))

        if parsed['normalized'].endswith('.list'):
            return parsed['normalized']

        raise Exception('Running a single test is not currently supported')

    def _make_shell_string(self, s):
        return "'%s'" % re.sub("'", r"'\''", s)

    def run_reftest_test(self, test_file=None, filter=None, suite=None,
            debugger=None):
        """Runs a reftest.

        test_file is a path to a test file. It can be a relative path from the
        top source directory, an absolute filename, or a directory containing
        test files.

        filter is a regular expression (in JS syntax, as could be passed to the
        RegExp constructor) to select which reftests to run from the manifest.

        suite is the type of reftest to run. It can be one of ('reftest',
        'crashtest').

        debugger is the program name (in $PATH) or the full path of the
        debugger to run.
        """

        if suite not in ('reftest', 'crashtest'):
            raise Exception('None or unrecognized reftest suite type.')

        env = {}
        extra_args = []

        if test_file:
            path = self._find_manifest(suite, test_file)
            if not os.path.exists(path):
                raise Exception('No manifest file was found at %s.' % path)
            env[b'TEST_PATH'] = path
        if filter:
            extra_args.extend(['--filter', self._make_shell_string(filter)])

        pass_thru = False

        if debugger:
            extra_args.append('--debugger=%s' % debugger)
            pass_thru = True

        if extra_args:
            args = [os.environ.get(b'EXTRA_TEST_ARGS', '')]
            args.extend(extra_args)
            env[b'EXTRA_TEST_ARGS'] = ' '.join(args)

        # TODO hook up harness via native Python
        return self._run_make(directory='.', target=suite, append_env=env,
            pass_thru=pass_thru, ensure_exit_code=False)


@CommandProvider
class MachCommands(MachCommandBase):
    @Command('reftest', help='Run a reftest.')
    @CommandArgument('test_file', default=None, nargs='?', metavar='MANIFEST',
        help='Reftest manifest file, or a directory in which to select '
             'reftest.list. If omitted, the entire test suite is executed.')
    @CommandArgument('--filter', default=None, metavar='REGEX',
        help='A JS regular expression to match test URLs against, to select '
             'a subset of tests to run.')
    @CommandArgument('--debugger', metavar='DEBUGGER', help=DEBUGGER_HELP)
    def run_reftest(self, test_file, filter, debugger=None):
        return self._run_reftest(test_file, filter=filter, suite='reftest',
            debugger=debugger)

    @Command('crashtest', help='Run a crashtest.')
    @CommandArgument('test_file', default=None, nargs='?', metavar='MANIFEST',
        help='Crashtest manifest file, or a direction in which to select '
             'crashtests.list.')
    @CommandArgument('--debugger', metavar='DEBUGGER', help=DEBUGGER_HELP)
    def run_crashtest(self, test_file, debugger=None):
        return self._run_reftest(test_file, suite='crashtest',
            debugger=debugger)

    def _run_reftest(self, test_file=None, filter=None, suite=None,
            debugger=None):
        reftest = self._spawn(ReftestRunner)
        return reftest.run_reftest_test(test_file, filter=filter, suite=suite,
            debugger=debugger)


