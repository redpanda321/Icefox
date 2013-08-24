# Copyright (c) 2010 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'library%': 'shared_library',
  },
  'target_defaults': {
    'default_configuration': 'Debug',
    'configurations': {
      'Common': {
        'abstract': 1,
        'msvs_configuration_attributes': {
          'OutputDirectory': '$(SolutionDir)$(ConfigurationName)',
          'IntermediateDirectory': '$(OutDir)\\obj\\$(ProjectName)',
          'CharacterSet': '1',  # UNICODE
        },
        'msvs_configuration_platform': 'Win32',
        'msvs_settings': {
          'VCCLCompilerTool': {
            'BufferSecurityCheck': 'true',
            'DebugInformationFormat': '3',
            # TODO(alokp): Disable exceptions before integrating with chromium.
            #'ExceptionHandling': '0',
            'EnableFunctionLevelLinking': 'true',
            'MinimalRebuild': 'false',
            'PreprocessorDefinitions': [
              '_CRT_SECURE_NO_DEPRECATE',
              '_HAS_EXCEPTIONS=0',
              '_HAS_TR1=0',
              '_WIN32_WINNT=0x0600',
              '_WINDOWS',
              'NOMINMAX',
              'WIN32',
              'WIN32_LEAN_AND_MEAN',
              'WINVER=0x0600',
            ],
            'RuntimeTypeInfo': 'false',
            'WarningLevel': '3',
          },
          'VCLinkerTool': {
            'FixedBaseAddress': '1',
            'GenerateDebugInformation': 'true',
            'ImportLibrary': '$(OutDir)\\lib\\$(TargetName).lib',
            'MapFileName': '$(OutDir)\\$(TargetName).map',
            # Most of the executables we'll ever create are tests
            # and utilities with console output.
            'SubSystem': '1',  # /SUBSYSTEM:CONSOLE
          },
          'VCResourceCompilerTool': {
            'Culture': '1033',
          },
        },
      },  # Common
      'Debug': {
        'inherit_from': ['Common'],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '0',  # /Od
            'PreprocessorDefinitions': ['_DEBUG'],
            'BasicRuntimeChecks': '3',
            'RuntimeLibrary': '1',  # /MTd (debug static)
          },
          'VCLinkerTool': {
            'LinkIncremental': '2',
          },
        },
      },  # Debug
      'Release': {
        'inherit_from': ['Common'],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '2',  # /Os
            'PreprocessorDefinitions': ['NDEBUG'],
            'RuntimeLibrary': '0',  # /MT (static)
          },
          'VCLinkerTool': {
            'LinkIncremental': '1',
          },
        },
      },  # Release
    },  # configurations
  },  # target_defaults
  'conditions': [
    ['OS=="win"', {
      'target_defaults': {
        'msvs_cygwin_dirs': ['../third_party/cygwin'],
      },
    }]
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
