#!/usr/bin/python

# Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
# for details. All rights reserved. Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

"""
Pub buildbot steps.

Runs tests for pub and the pub packages that are hosted in the main Dart repo.
"""

import re
import sys

import bot

PUB_BUILDER = r'pub-(linux|mac|win)(-(russian))?(-(debug))?'

def PubConfig(name, is_buildbot):
  """Returns info for the current buildbot based on the name of the builder.

  Currently, this is just:
  - mode: "debug", "release"
  - system: "linux", "mac", or "win"
  """
  pub_pattern = re.match(PUB_BUILDER, name)
  if not pub_pattern:
    return None

  system = pub_pattern.group(1)
  locale = pub_pattern.group(3)
  mode = pub_pattern.group(5) or 'release'
  if system == 'win': system = 'windows'

  return bot.BuildInfo('none', 'vm', mode, system, checked=True,
                       builder_tag=locale)

def PubSteps(build_info):
  with bot.BuildStep('Build package-root'):
    args = [sys.executable, './tools/build.py', '--mode=' + build_info.mode,
            'packages']
    print 'Building package-root: %s' % (' '.join(args))
    bot.RunProcess(args)

  common_args = ['--write-test-outcome-log']
  if build_info.builder_tag:
    common_args.append('--builder-tag=%s' % build_info.builder_tag)


  testing_options = ['', '--vm-options=--optimization-counter-threshold=5']
  for options in testing_options:
    if build_info.mode == 'release':
      bot.RunTest('pub and pkg %s' % options, build_info,
                  common_args + [options, 'pub', 'pkg', 'docs'])
    else:
      # Pub tests currently have a lot of timeouts when run in debug mode.
      # See issue 18479
      bot.RunTest('pub and pkg %s' % options, build_info,
                  common_args + [options, 'pkg', 'docs'])

  if build_info.mode == 'release':
    pkgbuild_build_info = bot.BuildInfo('none', 'vm', build_info.mode,
                                        build_info.system, checked=False)
    bot.RunTest('pkgbuild_repo_pkgs', pkgbuild_build_info,
        common_args + ['--append_logs', '--use-repository-packages',
                       'pkgbuild'])

    # We are seeing issues with pub get calls on the windows bots.
    # Experiment with not running concurrent calls.
    public_args = (common_args +
                   ['--append_logs', '--use-public-packages', 'pkgbuild'])
    if build_info.system == 'windows':
      public_args.append('-j1')
    bot.RunTest('pkgbuild_public_pkgs', pkgbuild_build_info, public_args)

if __name__ == '__main__':
  bot.RunBot(PubConfig, PubSteps)
