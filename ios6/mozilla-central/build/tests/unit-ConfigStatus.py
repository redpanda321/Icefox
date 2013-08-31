from __future__ import with_statement
import os, posixpath
from StringIO import StringIO
import unittest
from mozunit import main, MockedOpen
import ConfigStatus
from ConfigStatus import FileAvoidWrite

class ConfigEnvironment(ConfigStatus.ConfigEnvironment):
    def __init__(self, **args):
        ConfigStatus.ConfigEnvironment.__init__(self, **args)
        # Be helpful to unit tests
        if not 'top_srcdir' in self.substs:
            if os.path.isabs(self.topsrcdir):
                self.substs['top_srcdir'] = self.topsrcdir.replace(os.sep, '/')
            else:
                self.substs['top_srcdir'] = ConfigStatus.relpath(self.topsrcdir, self.topobjdir).replace(os.sep, '/')

class TestFileAvoidWrite(unittest.TestCase):
    def test_file_avoid_write(self):
        '''Test the FileAvoidWrite class
        '''
        with MockedOpen({'file': 'content'}):
            # Overwriting an existing file replaces its content
            with FileAvoidWrite('file') as file:
                file.write('bazqux')
            self.assertEqual(open('file', 'r').read(), 'bazqux')

            # Creating a new file (obviously) stores its content
            with FileAvoidWrite('file2') as file:
                file.write('content')
            self.assertEqual(open('file2').read(), 'content')

        class MyMockedOpen(MockedOpen):
            '''MockedOpen extension to raise an exception if something
            attempts to write in an opened file.
            '''
            def __call__(self, name, mode):
                if 'w' in mode:
                    raise Exception, 'Unexpected open with write mode'
                return MockedOpen.__call__(self, name, mode)

        with MyMockedOpen({'file': 'content'}):
            # Validate that MyMockedOpen works as intended
            file = FileAvoidWrite('file')
            file.write('foobar')
            self.assertRaises(Exception, file.close)

            # Check that no write actually happens when writing the
            # same content as what already is in the file
            with FileAvoidWrite('file') as file:
                file.write('content')


class TestEnvironment(unittest.TestCase):
    def test_auto_substs(self):
        '''Test the automatically set values of ACDEFINES, ALLDEFINES
        and ALLSUBSTS.
        '''
        env = ConfigEnvironment(
                  defines = [ ('foo', 'bar'), ('baz', 'qux 42'),
                              ('abc', 'def'), ('extra', 'foobar') ],
                  non_global_defines = ['extra', 'ignore'],
                  substs = [ ('FOO', 'bar'), ('ABC', 'def'),
                             ('bar', 'baz qux'), ('zzz', '"abc def"') ])
        # non_global_defines should be filtered out in ACDEFINES and
        # ALLDEFINES.
        # Original order of the defines need to be respected in ACDEFINES
        self.assertEqual(env.substs['ACDEFINES'], '''-Dfoo=bar -Dbaz=qux\ 42 -Dabc=def''')
        # ALLDEFINES, on the other hand, needs to be sorted
        self.assertEqual(env.substs['ALLDEFINES'], '''#define abc def
#define baz qux 42
#define foo bar''')
        # Likewise for ALLSUBSTS, which also mustn't contain ALLDEFINES
        # but contain ACDEFINES
        self.assertEqual(env.substs['ALLSUBSTS'], '''ABC = def
ACDEFINES = -Dfoo=bar -Dbaz=qux\ 42 -Dabc=def
FOO = bar
bar = baz qux
zzz = "abc def"''')

    def test_config_file(self):
        '''Test the creation of config files.
        '''
        with MockedOpen({'file.in': '''#ifdef foo
@foo@
@bar@
'''}):
            env = ConfigEnvironment(substs = [ ('foo', 'bar baz') ])
            env.create_config_file('file')
            self.assertEqual(open('file', 'r').read(), '''#ifdef foo
bar baz
@bar@
''')

    def test_config_header(self):
        '''Test the creation of config headers.
        '''
        with MockedOpen({'file.in': '''
/* Comment */
#define foo
#define foo 42
#undef foo
#define bar
#define bar 42
#undef bar

# undef baz

#ifdef foo
#   undef   foo
#  define foo    42
  #     define   foo   42   
#endif
'''}):
            env = ConfigEnvironment(defines = [ ('foo', 'baz qux'), ('baz', 1) ])
            env.create_config_header('file')
            self.assertEqual(open('file','r').read(), '''
/* Comment */
#define foo
#define foo baz qux
#define foo baz qux
#define bar
#define bar 42
/* #undef bar */

# define baz 1

#ifdef foo
#   define   foo baz qux
#  define foo    baz qux
  #     define   foo   baz qux   
#endif
''')

# Tests for get_relative_srcdir, get_depth, get_input and get_file_srcdir,
# depending on various cases of top source directory and top build
# directory location.
class TestPaths(unittest.TestCase):
    def setUp(self):
        self.dir = os.path.basename(os.path.abspath(os.curdir))
        self.absolute = os.path.normpath('/absolute')

class TestPathsLocalBuildDir(TestPaths):
    def get_env(self, topsrcdir):
        env = ConfigEnvironment(topsrcdir = topsrcdir, topobjdir = '.')
        self.assertEqual(env.get_relative_srcdir('file'), '.')
        self.assertEqual(env.get_relative_srcdir('dir/file'), 'dir')
        self.assertEqual(env.get_relative_srcdir('deeply/nested/path/to/file'), 'deeply/nested/path/to')
        self.assertEqual(env.get_depth('file'), '.')
        self.assertEqual(env.get_depth('dir/file'), '..')
        self.assertEqual(env.get_depth('deeply/nested/path/to/file'), '../../../..')
        return env

    def test_paths_local_build_local_src(self):
        # topsrcdir = . ; topobjdir = .
        env = self.get_env('.')
        self.assertEqual(env.get_input('file'), 'file.in')
        self.assertEqual(env.get_input('dir/file'), os.path.join('dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('file'), '.')
        self.assertEqual(env.get_top_srcdir('dir/file'), '..')
        self.assertEqual(env.get_file_srcdir('file'), '.')
        self.assertEqual(env.get_file_srcdir('dir/file'), '../dir')

    def test_paths_local_build_parent_src(self):
        # topsrcdir = .. ; topobjdir = .
        env = self.get_env('..')
        self.assertEqual(env.get_input('file'), os.path.join('..', 'file.in'))
        self.assertEqual(env.get_input('dir/file'), os.path.join('..', 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('file'), '..')
        self.assertEqual(env.get_top_srcdir('dir/file'), '../..')
        self.assertEqual(env.get_file_srcdir('file'), '..')
        self.assertEqual(env.get_file_srcdir('dir/file'), '../../dir')

    def test_paths_local_build_absolute_src(self):
        # topsrcdir = /absolute ; topobjdir = /absolute
        env = self.get_env(self.absolute)
        self.assertEqual(env.get_input('file'), os.path.join(self.absolute, 'file.in'))
        self.assertEqual(env.get_input('dir/file'), os.path.join(self.absolute, 'dir', 'file.in'))
        self.assertEqual(env.get_input('%s/file' % self.dir), os.path.join(self.absolute, self.dir, 'file.in'))
        self.assertEqual(env.get_top_srcdir('file'), '/absolute')
        self.assertEqual(env.get_top_srcdir('dir/file'), '/absolute')
        self.assertEqual(env.get_top_srcdir('%s/file' % dir), '/absolute')
        self.assertEqual(env.get_file_srcdir('file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('dir/file'), '/absolute/dir')
        self.assertEqual(env.get_file_srcdir('%s/file' % dir), '/absolute/%s' % dir)

class TestPathsParentBuildDir(TestPaths):
    def get_env(self, topsrcdir):
        env = ConfigEnvironment(topsrcdir = topsrcdir, topobjdir = '..')
        self.assertEqual(env.get_relative_srcdir('..'), '.')
        self.assertEqual(env.get_relative_srcdir('file'), self.dir)
        self.assertEqual(env.get_relative_srcdir('dir/file'), '%s/dir' % self.dir)
        self.assertEqual(env.get_relative_srcdir('deeply/nested/path/to/file'), '%s/deeply/nested/path/to' % self.dir)
        self.assertEqual(env.get_depth('../file'), '.')
        self.assertEqual(env.get_depth('file'), '..')
        self.assertEqual(env.get_depth('dir/file'), '../..')
        self.assertEqual(env.get_depth('deeply/nested/path/to/file'), '../../../../..')
        return env

    def test_paths_parent_build_parent_src(self):
        # topsrcdir = .. ; topobjdir = ..
        env = self.get_env('..')
        self.assertEqual(env.get_input('../file'), os.path.join('..', 'file.in'))
        self.assertEqual(env.get_input('file'), os.path.join('..', self.dir, 'file.in'))
        self.assertEqual(env.get_input('dir/file'), os.path.join('..', self.dir, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('../file'), '.')
        self.assertEqual(env.get_top_srcdir('file'), '..')
        self.assertEqual(env.get_top_srcdir('dir/file'), '../..')
        self.assertEqual(env.get_file_srcdir('../file'), '.')
        self.assertEqual(env.get_file_srcdir('file'), '../%s' % self.dir)
        self.assertEqual(env.get_file_srcdir('dir/file'), '../../%s/dir' % self.dir)

    def test_paths_parent_build_ancestor_src(self):
        # topsrcdir = ../.. ; topobjdir = ..
        env = self.get_env('../..')
        self.assertEqual(env.get_input('../file'), os.path.join('..', '..', 'file.in'))
        self.assertEqual(env.get_input('file'), os.path.join('..', '..', self.dir, 'file.in'))
        self.assertEqual(env.get_input('dir/file'), os.path.join('..', '..', self.dir, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('../file'), '..')
        self.assertEqual(env.get_top_srcdir('file'), '../..')
        self.assertEqual(env.get_top_srcdir('dir/file'), '../../..')
        self.assertEqual(env.get_file_srcdir('../file'), '..')
        self.assertEqual(env.get_file_srcdir('file'), '../../%s' % self.dir)
        self.assertEqual(env.get_file_srcdir('dir/file'), '../../../%s/dir' % self.dir)

    def test_paths_parent_build_absolute_src(self):
        # topsrcdir = /absolute ; topobjdir = ..
        env = self.get_env(self.absolute)
        self.assertEqual(env.get_input('../file'), os.path.join(self.absolute, 'file.in'))
        self.assertEqual(env.get_input('file'), os.path.join(self.absolute, self.dir, 'file.in'))
        self.assertEqual(env.get_input('dir/file'), os.path.join(self.absolute, self.dir, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('../file'), '/absolute')
        self.assertEqual(env.get_top_srcdir('file'), '/absolute')
        self.assertEqual(env.get_top_srcdir('dir/file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('../file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('file'), '/absolute/%s' % self.dir)
        self.assertEqual(env.get_file_srcdir('dir/file'), '/absolute/%s/dir' % self.dir)

class TestPathsRelativeBuild(TestPaths):
    def get_env(self, topsrcdir):
        env = ConfigEnvironment(topsrcdir = topsrcdir, topobjdir = 'relative')
        self.assertEqual(env.get_relative_srcdir('relative/file'), '.')
        self.assertEqual(env.get_relative_srcdir('relative/dir/file'), 'dir')
        self.assertEqual(env.get_relative_srcdir('relative/deeply/nested/path/to/file'), 'deeply/nested/path/to')
        self.assertEqual(env.get_depth('relative/file'), '.')
        self.assertEqual(env.get_depth('relative/dir/file'), '..')
        self.assertEqual(env.get_depth('relative/deeply/nested/path/to/file'), '../../../..')
        return env

    def test_paths_relative_build_relative_src(self):
        # topsrcdir = relative ; topobjdir = relative
        env = self.get_env('relative')
        self.assertEqual(env.get_input('relative/file'), os.path.join('relative', 'file.in'))
        self.assertEqual(env.get_input('relative/dir/file'), os.path.join('relative', 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('relative/file'), '.')
        self.assertEqual(env.get_top_srcdir('relative/dir/file'), '..')
        self.assertEqual(env.get_file_srcdir('relative/file'), '.')
        self.assertEqual(env.get_file_srcdir('relative/dir/file'), '../dir')

    def test_paths_relative_build_local_src(self):
        # topsrcdir = . ; topobjdir = relative
        env = self.get_env('.')
        self.assertEqual(env.get_input('relative/file'), 'file.in')
        self.assertEqual(env.get_input('relative/dir/file'), os.path.join('dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('relative/file'), '..')
        self.assertEqual(env.get_top_srcdir('relative/dir/file'), '../..')
        self.assertEqual(env.get_file_srcdir('relative/file'), '..')
        self.assertEqual(env.get_file_srcdir('relative/dir/file'), '../../dir')

    def test_paths_relative_build_parent_src(self):
        # topsrcdir = .. ; topobjdir = relative
        env = self.get_env('..')
        self.assertEqual(env.get_input('relative/file'), os.path.join('..', 'file.in'))
        self.assertEqual(env.get_input('relative/dir/file'), os.path.join('..', 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('relative/file'), '../..')
        self.assertEqual(env.get_top_srcdir('relative/dir/file'), '../../..')
        self.assertEqual(env.get_file_srcdir('relative/file'), '../..')
        self.assertEqual(env.get_file_srcdir('relative/dir/file'), '../../../dir')

    def test_paths_relative_build_absolute_src(self):
        # topsrcdir = /absolute ; topobjdir = relative
        env = self.get_env(self.absolute)
        self.assertEqual(env.get_input('relative/file'), os.path.join(self.absolute, 'file.in'))
        self.assertEqual(env.get_input('relative/dir/file'), os.path.join(self.absolute, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('relative/file'), '/absolute')
        self.assertEqual(env.get_top_srcdir('relative/dir/file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('relative/file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('relative/dir/file'), '/absolute/dir')

class TestPathsAbsoluteBuild(unittest.TestCase):
    def setUp(self):
        self.absolute_build = os.path.normpath('/absolute/build')

    def get_env(self, topsrcdir):
        env = ConfigEnvironment(topsrcdir = topsrcdir, topobjdir = self.absolute_build)
        self.assertEqual(env.get_relative_srcdir('/absolute/build/file'), '.')
        self.assertEqual(env.get_relative_srcdir('/absolute/build/dir/file'), 'dir')
        self.assertEqual(env.get_relative_srcdir('/absolute/build/deeply/nested/path/to/file'), 'deeply/nested/path/to')
        self.assertEqual(env.get_depth('/absolute/build/file'), '.')
        self.assertEqual(env.get_depth('/absolute/build/dir/file'), '..')
        self.assertEqual(env.get_depth('/absolute/build/deeply/nested/path/to/file'), '../../../..')
        return env

    def test_paths_absolute_build_same_src(self):
        # topsrcdir = /absolute/build ; topobjdir = /absolute/build
        env = self.get_env(self.absolute_build)
        self.assertEqual(env.get_input('/absolute/build/file'), os.path.join(self.absolute_build, 'file.in'))
        self.assertEqual(env.get_input('/absolute/build/dir/file'), os.path.join(self.absolute_build, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('/absolute/build/file'), '/absolute/build')
        self.assertEqual(env.get_top_srcdir('/absolute/build/dir/file'), '/absolute/build')
        self.assertEqual(env.get_file_srcdir('/absolute/build/file'), '/absolute/build')
        self.assertEqual(env.get_file_srcdir('/absolute/build/dir/file'), '/absolute/build/dir')

    def test_paths_absolute_build_ancestor_src(self):
        # topsrcdir = /absolute ; topobjdir = /absolute/build
        absolute = os.path.dirname(self.absolute_build)
        env = self.get_env(absolute)
        self.assertEqual(env.get_input('/absolute/build/file'), os.path.join(absolute, 'file.in'))
        self.assertEqual(env.get_input('/absolute/build/dir/file'), os.path.join(absolute, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('/absolute/build/file'), '/absolute')
        self.assertEqual(env.get_top_srcdir('/absolute/build/dir/file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('/absolute/build/file'), '/absolute')
        self.assertEqual(env.get_file_srcdir('/absolute/build/dir/file'), '/absolute/dir')

    def test_paths_absolute_build_different_src(self):
        # topsrcdir = /some/path ; topobjdir = /absolute/build
        absolute = os.path.normpath('/some/path')
        env = self.get_env(absolute)
        self.assertEqual(env.get_input('/absolute/build/file'), os.path.join(absolute, 'file.in'))
        self.assertEqual(env.get_input('/absolute/build/dir/file'), os.path.join(absolute, 'dir', 'file.in'))
        self.assertEqual(env.get_top_srcdir('/absolute/build/file'), '/some/path')
        self.assertEqual(env.get_top_srcdir('/absolute/build/dir/file'), '/some/path')
        self.assertEqual(env.get_file_srcdir('/absolute/build/file'), '/some/path')
        self.assertEqual(env.get_file_srcdir('/absolute/build/dir/file'), '/some/path/dir')

if __name__ == "__main__":
    main()
