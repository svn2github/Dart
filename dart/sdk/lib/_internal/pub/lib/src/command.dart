// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub.command;

import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:args/args.dart';
import 'package:path/path.dart' as path;
import 'package:stack_trace/stack_trace.dart';

import 'command/build.dart';
import 'command/cache.dart';
import 'command/get.dart';
import 'command/help.dart';
import 'command/lish.dart';
import 'command/list_package_dirs.dart';
import 'command/serve.dart';
import 'command/upgrade.dart';
import 'command/uploader.dart';
import 'command/version.dart';
import 'entrypoint.dart';
import 'exit_codes.dart' as exit_codes;
import 'http.dart';
import 'io.dart';
import 'log.dart' as log;
import 'system_cache.dart';
import 'utils.dart';

/// The base class for commands for the pub executable.
abstract class PubCommand {
  /// The commands that pub understands.
  static final Map<String, PubCommand> commands = _initCommands();

  /// The top-level [ArgParser] used to parse the pub command line.
  static final pubArgParser = _initArgParser();

  /// Displays usage information for the app.
  static void printGlobalUsage() {
    // Build up a buffer so it shows up as a single log entry.
    var buffer = new StringBuffer();
    buffer.writeln('Pub is a package manager for Dart.');
    buffer.writeln();
    buffer.writeln('Usage: pub command [arguments]');
    buffer.writeln();
    buffer.writeln('Global options:');
    buffer.writeln(pubArgParser.getUsage());
    buffer.writeln();

    // Show the public commands alphabetically.
    var names = ordered(commands.keys.where((name) =>
        !commands[name].aliases.contains(name) &&
        !commands[name].hidden));

    var length = names.map((name) => name.length).reduce(math.max);

    buffer.writeln('Available commands:');
    for (var name in names) {
      buffer.writeln('  ${padRight(name, length)}   '
          '${commands[name].description}');
    }

    buffer.writeln();
    buffer.write(
        'Use "pub help [command]" for more information about a command.');
    log.message(buffer.toString());
  }

  SystemCache cache;

  /// The parsed options for this command.
  ArgResults commandOptions;

  Entrypoint entrypoint;

  /// A one-line description of this command.
  String get description;

  /// If the command is undocumented and should not appear in command listings,
  /// this will be `true`.
  bool get hidden => false;

  /// How to invoke this command (e.g. `"pub get [package]"`).
  String get usage;

  /// Whether or not this command requires [entrypoint] to be defined. If false,
  /// pub won't look for a pubspec and [entrypoint] will be null when the
  /// command runs.
  bool get requiresEntrypoint => true;

  /// Whether or not this command takes arguments in addition to options. If
  /// false, pub will exit with an error if arguments are provided.
  bool get takesArguments => false;

  /// Alternate names for this command. These names won't be used in the
  /// documentation, but they will work when invoked on the command line.
  final aliases = const <String>[];

  /// The [ArgParser] for this command.
  final commandParser = new ArgParser();

  /// Override this to use offline-only sources instead of hitting the network.
  /// This will only be called before the [SystemCache] is created. After that,
  /// it has no effect.
  bool get isOffline => false;

  PubCommand() {
    // Allow "--help" after a command to get command help.
    commandParser.addFlag('help', abbr: 'h', negatable: false,
        help: 'Print usage information for this command.');
  }

  void run(String cacheDir, ArgResults options, List<String> arguments) {
    commandOptions = options.command;

    if (commandOptions['help']) {
      this.printUsage();
      return;
    }

    cache = new SystemCache.withSources(cacheDir, isOffline: isOffline);

    handleError(error, Chain chain) {
      // This is basically the top-level exception handler so that we don't
      // spew a stack trace on our users.
      var message;

      log.error(getErrorMessage(error));
      log.fine("Exception type: ${error.runtimeType}");

      if (options['trace'] || !isUserFacingException(error)) {
        log.error(chain.terse);
      } else {
        log.fine(chain.terse);
      }

      if (error is ApplicationException && error.innerError != null) {
        var message = "Wrapped exception: ${error.innerError}";
        if (error.innerTrace != null) message = "$message\n${error.innerTrace}";
        log.fine(message);
      }

      if (options['trace']) {
        log.dumpTranscript();
      } else if (!isUserFacingException(error)) {
        log.error("""
This is an unexpected error. Please run

    pub --trace ${arguments.map((arg) => "'$arg'").join(' ')}

and include the results in a bug report on http://dartbug.com/new.
""");
      }

      return flushThenExit(_chooseExitCode(error));
    }

    var captureStackChains =
        options['trace'] || options['verbose'] || options['verbosity'] == 'all';
    captureErrors(() {
      return syncFuture(() {
        // Make sure there aren't unexpected arguments.
        if (!takesArguments && commandOptions.rest.isNotEmpty) {
          log.error('Command "${commandOptions.name}" does not take any '
                    'arguments.');
          this.printUsage();
          return flushThenExit(exit_codes.USAGE);
        }

        if (requiresEntrypoint) {
          // TODO(rnystrom): Will eventually need better logic to walk up
          // subdirectories until we hit one that looks package-like. For now,
          // just assume the cwd is it.
          entrypoint = new Entrypoint(path.current, cache);
        }

        var commandFuture = onRun();
        if (commandFuture == null) return true;

        return commandFuture;
      }).whenComplete(() => cache.deleteTempDir());
    }, captureStackChains: captureStackChains).catchError(handleError)
        .then((_) {
      // Explicitly exit on success to ensure that any dangling dart:io handles
      // don't cause the process to never terminate.
      return flushThenExit(0);
    });
  }

  /// Override this to perform the specific command. Return a future that
  /// completes when the command is done or fails if the command fails. If the
  /// command is synchronous, it may return `null`.
  Future onRun();

  /// Displays usage information for this command.
  void printUsage([String description]) {
    if (description == null) description = this.description;

    var buffer = new StringBuffer();
    buffer.write('$description\n\nUsage: $usage');

    var commandUsage = commandParser.getUsage();
    if (!commandUsage.isEmpty) {
      buffer.write('\n');
      buffer.write(commandUsage);
    }

    log.message(buffer.toString());
  }

  /// Returns the appropriate exit code for [exception], falling back on 1 if no
  /// appropriate exit code could be found.
  int _chooseExitCode(exception) {
    if (exception is HttpException || exception is HttpException ||
        exception is SocketException || exception is PubHttpException) {
      return exit_codes.UNAVAILABLE;
    } else if (exception is FormatException) {
      return exit_codes.DATA;
    } else {
      return 1;
    }
  }
}

_initCommands() {
  var commands = {
    'build': new BuildCommand(),
    'cache': new CacheCommand(),
    'get': new GetCommand(),
    'help': new HelpCommand(),
    'list-package-dirs': new ListPackageDirsCommand(),
    'publish': new LishCommand(),
    'serve': new ServeCommand(),
    'upgrade': new UpgradeCommand(),
    'uploader': new UploaderCommand(),
    'version': new VersionCommand()
  };

  for (var command in commands.values.toList()) {
    for (var alias in command.aliases) {
      commands[alias] = command;
    }
  }

  return commands;
}

/// Creates the top-level [ArgParser] used to parse the pub command line.
ArgParser _initArgParser() {
  var argParser = new ArgParser();

  // Add the global options.
  argParser.addFlag('help', abbr: 'h', negatable: false,
      help: 'Print this usage information.');
  argParser.addFlag('version', negatable: false,
      help: 'Print pub version.');
  argParser.addFlag('trace',
       help: 'Print debugging information when an error occurs.');
  argParser.addOption('verbosity',
      help: 'Control output verbosity.',
      allowed: ['normal', 'io', 'solver', 'all'],
      allowedHelp: {
        'normal': 'Show errors, warnings, and user messages.',
        'io':     'Also show IO operations.',
        'solver': 'Show steps during version resolution.',
        'all':    'Show all output including internal tracing messages.'
      });
  argParser.addFlag('verbose', abbr: 'v', negatable: false,
      help: 'Shortcut for "--verbosity=all".');

  // Register the commands.
  PubCommand.commands.forEach((name, command) {
    argParser.addCommand(name, command.commandParser);
  });

  return argParser;
}
