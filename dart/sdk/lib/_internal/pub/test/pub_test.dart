// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub_tests;

import 'package:scheduled_test/scheduled_test.dart';

import 'test_pub.dart';

final USAGE_STRING = """
    Pub is a package manager for Dart.

    Usage: pub command [arguments]

    Global options:
    -h, --help            Print this usage information.
        --version         Print pub version.
        --[no-]trace      Print debugging information when an error occurs.
        --verbosity       Control output verbosity.

              [all]       Show all output including internal tracing messages.
              [io]        Also show IO operations.
              [normal]    Show errors, warnings, and user messages.
              [solver]    Show steps during version resolution.

    -v, --verbose         Shortcut for "--verbosity=all".

    Available commands:
      build      Copy and compile all Dart entrypoints in the 'web' directory.
      get        Get the current package's dependencies.
      help       Display help information for Pub.
      publish    Publish the current package to pub.dartlang.org.
      serve      Run a local web development server.
      upgrade    Upgrade the current package's dependencies to latest versions.
      uploader   Manage uploaders for a package on pub.dartlang.org.
      version    Print pub version.

    Use "pub help [command]" for more information about a command.
    """;

final VERSION_STRING = '''
    Pub 0.1.2+3
    ''';

main() {
  initConfig();

  integration('running pub with no command displays usage', () {
    schedulePub(args: [], output: USAGE_STRING);
  });

  integration('running pub with just --help displays usage', () {
    schedulePub(args: ['--help'], output: USAGE_STRING);
  });

  integration('running pub with just -h displays usage', () {
    schedulePub(args: ['-h'], output: USAGE_STRING);
  });

  integration('running pub with --help after command shows command usage', () {
    schedulePub(args: ['get', '--help'],
        output: '''
          Get the current package's dependencies.

          Usage: pub get
          -h, --help            Print usage information for this command.
              --[no-]offline    Use cached packages instead of accessing the network.
    ''');
  });

  integration('running pub with -h after command shows command usage', () {
    schedulePub(args: ['get', '-h'],
        output: '''
          Get the current package's dependencies.

          Usage: pub get
          -h, --help            Print usage information for this command.
              --[no-]offline    Use cached packages instead of accessing the network.
    ''');
  });

  integration('running pub with just --version displays version', () {
    schedulePub(args: ['--version'], output: VERSION_STRING);
  });

  integration('an unknown command displays an error message', () {
    schedulePub(args: ['quylthulg'],
        error: '''
        Could not find a command named "quylthulg".
        Run "pub help" to see available commands.
        ''',
        exitCode: 64);
  });

  integration('an unknown option displays an error message', () {
    schedulePub(args: ['--blorf'],
        error: '''
        Could not find an option named "blorf".
        Run "pub help" to see available options.
        ''',
        exitCode: 64);
  });

  integration('an unknown command option displays an error message', () {
    // TODO(rnystrom): When pub has command-specific options, a more precise
    // error message would be good here.
    schedulePub(args: ['version', '--blorf'],
        error: '''
        Could not find an option named "blorf".
        Run "pub help" to see available options.
        ''',
        exitCode: 64);
  });

  integration('an unexpected argument displays an error message', () {
    schedulePub(args: ['version', 'unexpected'],
        output: '''
        Print pub version.

        Usage: pub version
         -h, --help    Print usage information for this command.
        ''',
        error: '''
        Command "version" does not take any arguments.
        ''',
        exitCode: 64);
  });

  group('help', () {
    integration('shows help for a command', () {
      schedulePub(args: ['help', 'get'],
          output: '''
            Get the current package's dependencies.

            Usage: pub get
            -h, --help            Print usage information for this command.
                --[no-]offline    Use cached packages instead of accessing the network.
            ''');
    });

    integration('shows help for a command', () {
      schedulePub(args: ['help', 'publish'],
          output: '''
            Publish the current package to pub.dartlang.org.

            Usage: pub publish [options]
            -h, --help       Print usage information for this command.
            -n, --dry-run    Validate but do not publish the package.
            -f, --force      Publish without confirmation if there are no errors.
                --server     The package server to which to upload this package.
                             (defaults to "https://pub.dartlang.org")
            ''');
    });

    integration('an unknown help command displays an error message', () {
      schedulePub(args: ['help', 'quylthulg'],
          error: '''
            Could not find a command named "quylthulg".
            Run "pub help" to see available commands.
            ''',
            exitCode: 64);
    });

  });

  group('version', () {
    integration('displays the current version', () {
      schedulePub(args: ['version'], output: VERSION_STRING);
    });
  });
}
