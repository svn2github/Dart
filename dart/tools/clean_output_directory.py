#!/usr/bin/env python
#
# Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
# for details. All rights reserved. Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.
#

import optparse
import shutil
import sys
import utils

HOST_OS = utils.GuessOS()

def BuildOptions():
  result = optparse.OptionParser()
  result.add_option("-m", "--mode",
      help='Build variants (comma-separated).',
      metavar='[all,debug,release]',
      default='all')
  result.add_option("--arch",
      help='Target architectures (comma-separated).',
      metavar='[all,ia32,x64,simarm,arm]',
      default='all')
  result.add_option("--os",
    help='Target OSs (comma-separated).',
    metavar='[all,host,android]',
    default='all')
  return result


def ProcessOsOption(os):
  if os == 'host':
    return HOST_OS
  return os


def ProcessOptions(options):
  if options.arch == 'all':
    options.arch = 'ia32,x64'
  if options.mode == 'all':
    options.mode = 'release,debug'
  if options.os == 'all':
    options.os = 'host,android'
  options.mode = options.mode.split(',')
  options.arch = options.arch.split(',')
  options.os = options.os.split(',')
  for mode in options.mode:
    if not mode in ['debug', 'release']:
      print "Unknown mode %s" % mode
      return False
  for arch in options.arch:
    if not arch in ['ia32', 'x64', 'simarm', 'arm']:
      print "Unknown arch %s" % arch
      return False
  options.os = [ProcessOsOption(os) for os in options.os]
  for os in options.os:
    if not os in ['android', 'freebsd', 'linux', 'macos', 'win32']:
      print "Unknown os %s" % os
      return False
  return True

def Main():
  parser = BuildOptions()
  (options, args) = parser.parse_args()
  if not ProcessOptions(options):
    parser.print_help()
    return 1

  build_root = utils.GetBuildDir(HOST_OS, None)
  shutil.rmtree(build_root, ignore_errors=True)
  return 0

if __name__ == '__main__':
  sys.exit(Main())
