// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

/**
 * Classes and methods for executing tests.
 *
 * This module includes:
 * - Managing parallel execution of tests, including timeout checks.
 * - Evaluating the output of each test as pass/fail/crash/timeout.
 */
library test_runner;

import "dart:io";
import "dart:isolate";
import "dart:uri";
import "status_file_parser.dart";
import "test_progress.dart";
import "test_suite.dart";

const int NO_TIMEOUT = 0;
const int SLOW_TIMEOUT_MULTIPLIER = 4;

typedef void TestCaseEvent(TestCase testCase);
typedef void ExitCodeEvent(int exitCode);
typedef void EnqueueMoreWork(ProcessQueue queue);

/** A command executed as a step in a test case. */
class Command {
  /** Path to the executable of this command. */
  String executable;

  /** Command line arguments to the executable. */
  List<String> arguments;

  /** The actual command line that will be executed. */
  String commandLine;

  Command(this.executable, this.arguments) {
    if (Platform.operatingSystem == 'windows') {
      // Windows can't handle the first command if it is a .bat file or the like
      // with the slashes going the other direction.
      // TODO(efortuna): Remove this when fixed (Issue 1306).
      executable = executable.replaceAll('/', '\\');
    }
    commandLine = "$executable ${Strings.join(arguments, ' ')}";
  }

  String toString() => commandLine;

  Future<bool> get outputIsUpToDate => new Future.immediate(false);
}

class Dart2JsCommand extends Command {
  String _jsOutputFile;
  bool _neverSkipCompilation;
  List<Uri> _bootstrapDependencies;

  Dart2JsCommand(this._jsOutputFile,
                 this._neverSkipCompilation,
                 this._bootstrapDependencies,
                 String executable,
                 List<String> arguments)
      : super(executable, arguments);

  Future<bool> get outputIsUpToDate {
    if (_neverSkipCompilation) return new Future.immediate(false);

    Future<List<Uri>> readDepsFile(String path) {
      var file = new File(new Path(path).toNativePath());
      if (!file.existsSync()) {
        return new Future.immediate(null);
      }
      return file.readAsLines().transform((List<String> lines) {
        var dependencies = new List<Uri>();
        for (var line in lines) {
          line = line.trim();
          if (line.length > 0) {
            dependencies.add(new Uri(line));
          }
        }
        return dependencies;
      });
    }

    return readDepsFile("$_jsOutputFile.deps").transform((dependencies) {
      if (dependencies != null) {
        dependencies.addAll(_bootstrapDependencies);
        var jsOutputLastModified = TestUtils.lastModifiedCache.getLastModified(
            new Uri.fromComponents(scheme: 'file', path: _jsOutputFile));
        if (jsOutputLastModified != null) {
          for (var dependency in dependencies) {
            var dependencyLastModified =
                TestUtils.lastModifiedCache.getLastModified(dependency);
            if (dependencyLastModified == null ||
                dependencyLastModified > jsOutputLastModified) {
              return false;
            }
          }
          return true;
        }
      }
      return false;
    });
  }
}

/**
 * TestCase contains all the information needed to run a test and evaluate
 * its output.  Running a test involves starting a separate process, with
 * the executable and arguments given by the TestCase, and recording its
 * stdout and stderr output streams, and its exit code.  TestCase only
 * contains static information about the test; actually running the test is
 * performed by [ProcessQueue] using a [RunningProcess] object.
 *
 * The output information is stored in a [CommandOutput] instance contained
 * in TestCase.commandOutputs. The last CommandOutput instance is responsible
 * for evaluating if the test has passed, failed, crashed, or timed out, and the
 * TestCase has information about what the expected result of the test should
 * be.
 *
 * The TestCase has a callback function, [completedHandler], that is run when
 * the test is completed.
 */
class TestCase {
  /**
   * A list of commands to execute. Most test cases have a single command. 
   * Dart2js tests have two commands, one to compile the source and another
   * to execute it. Some isolate tests might even have three, if they require
   * compiling multiple sources that are run in isolation.
   */
  List<Command> commands;
  Map<Command, CommandOutput> commandOutputs = new Map<Command,CommandOutput>();

  Map configuration;
  String displayName;
  bool isNegative;
  Set<String> expectedOutcomes;
  TestCaseEvent completedHandler;
  TestInformation info;

  TestCase(this.displayName,
           this.commands,
           this.configuration,
           this.completedHandler,
           this.expectedOutcomes,
           {this.isNegative: false,
            this.info: null}) {
    if (!isNegative) {
      this.isNegative = displayName.contains("negative_test");
    }

    // Special command handling. If a special command is specified
    // we have to completely rewrite the command that we are using.
    // We generate a new command-line that is the special command where we
    // replace '@' with the original command executable, and generate
    // a command formed like the following
    // Let PREFIX be what is before the @.
    // Let SUFFIX be what is after the @.
    // Let EXECUTABLE be the existing executable of the command.
    // Let ARGUMENTS be the existing arguments to the existing executable.
    // The new command will be:
    // PREFIX EXECUTABLE SUFFIX ARGUMENTS
    var specialCommand = configuration['special-command'];
    if (!specialCommand.isEmpty) {
      Expect.isTrue(specialCommand.contains('@'),
                    "special-command must contain a '@' char");
      var specialCommandSplit = specialCommand.split('@');
      var prefix = specialCommandSplit[0].trim();
      var suffix = specialCommandSplit[1].trim();
      List<Command> newCommands = [];
      for (Command c in commands) {
        // If we don't have a new prefix we will use the existing executable.
        var newExecutablePath = c.executable;;
        var newArguments = [];

        if (prefix.length > 0) {
          var prefixSplit = prefix.split(' ');
          newExecutablePath = prefixSplit[0];
          for (int i = 1; i < prefixSplit.length; i++) {
            var current = prefixSplit[i];
            if (!current.isEmpty) newArguments.add(current);
          }
          newArguments.add(c.executable);
        }

        // Add any suffixes to the arguments of the original executable.
        var suffixSplit = suffix.split(' ');
        suffixSplit.forEach((e) {
          if (!e.isEmpty) newArguments.add(e);
        });

        newArguments.addAll(c.arguments);
        final newCommand = new Command(newExecutablePath, newArguments);
        newCommands.add(newCommand);
        // If there are extra spaces inside the prefix or suffix, this fails.
        String expected =
            '$prefix ${c.executable} $suffix ${Strings.join(c.arguments, ' ')}';
        Expect.stringEquals(expected.trim(), newCommand.commandLine);
      }
      commands = newCommands;
    }
  }

  CommandOutput get lastCommandOutput {
    if (commandOutputs.length == 0) {
      throw new Exception("CommandOutputs is empty, maybe no command was run? ("
                          "displayName: '$displayName', "
                          "configurationString: '$configurationString')");
    }
    return commandOutputs[commands[commandOutputs.length - 1]];
  }

  int get timeout {
    if (expectedOutcomes.contains(SLOW)) {
      return configuration['timeout'] * SLOW_TIMEOUT_MULTIPLIER;
    } else {
      return configuration['timeout'];
    }
  }

  String get configurationString {
    final compiler = configuration['compiler'];
    final runtime = configuration['runtime'];
    final mode = configuration['mode'];
    final arch = configuration['arch'];
    final checked = configuration['checked'] ? '-checked' : '';
    return "$compiler-$runtime$checked ${mode}_$arch";
  }

  List<String> get batchRunnerArguments => ['-batch'];
  List<String> get batchTestArguments => commands.last.arguments;

  bool get usesWebDriver => TestUtils.usesWebDriver(configuration['runtime']);

  void completed() { completedHandler(this); }

  bool get isFlaky {
      if (expectedOutcomes.contains(SKIP)) {
        return false;
      }

      var flags = new Set.from(expectedOutcomes);
      flags..remove(TIMEOUT)
           ..remove(SLOW);
      return flags.contains(PASS) && flags.length > 1;
  }
}


/**
 * BrowserTestCase has an extra compilation command that is run in a separate
 * process, before the regular test is run as in the base class [TestCase].
 * If the compilation command fails, then the rest of the test is not run.
 */
class BrowserTestCase extends TestCase {
  /**
   * Indicates the number of potential retries remaining, to compensate for
   * flaky browser tests.
   */
  int numRetries;

  /**
   * True if this test is dependent on another test completing before it can
   * star (for example, we might need to depend on some other test completing
   * first).
   */
  bool waitingForOtherTest;
  
  /**
   * The set of test cases that wish to be notified when this test has
   * completed.
   */
  List<BrowserTestCase> observers;

  BrowserTestCase(displayName, commands, configuration, completedHandler,
      expectedOutcomes, info, isNegative, [this.waitingForOtherTest = false])
    : super(displayName, commands, configuration, completedHandler,
        expectedOutcomes, isNegative: isNegative, info: info) {
    numRetries = 2; // Allow two retries to compensate for flaky browser tests.
    observers = [];
  }

  List<String> get _lastArguments => commands.last.arguments;

  List<String> get batchRunnerArguments => [_lastArguments[0], '--batch'];

  List<String> get batchTestArguments =>
      _lastArguments.getRange(1, _lastArguments.length - 1);

  /** Add a test case to listen for when this current test has completed. */
  void addObserver(BrowserTestCase testCase) {
    observers.add(testCase);
  }

  /**
   * Notify all of the test cases that are dependent on this one that they can
   * proceed.
   */
  void notifyObservers() {
    for (BrowserTestCase testCase in observers) {
      testCase.waitingForOtherTest = false;
    }
  }
}


/**
 * CommandOutput records the output of a completed command: the process's exit 
 * code, the standard output and standard error, whether the process timed out,
 * and the time the process took to run.  It also contains a pointer to the
 * [TestCase] this is the output of.
 */
abstract class CommandOutput {
  factory CommandOutput.fromCase(TestCase testCase,
                                 Command command,
                                 int exitCode,
                                 bool incomplete,
                                 bool timedOut,
                                 List<String> stdout,
                                 List<String> stderr,
                                 Duration time,
                                 bool compilationSkipped) {
    return new CommandOutputImpl.fromCase(testCase,
                                          command,
                                          exitCode,
                                          incomplete,
                                          timedOut,
                                          stdout,
                                          stderr,
                                          time,
                                          compilationSkipped);
  }

  bool get incomplete;

  String get result;

  bool get unexpectedOutput;

  bool get hasCrashed;

  bool get hasTimedOut;

  bool get didFail;

  bool requestRetry;

  Duration get time;

  int get exitCode;

  List<String> get stdout;

  List<String> get stderr;

  List<String> get diagnostics;

  bool get compilationSkipped;
}

class CommandOutputImpl implements CommandOutput {
  TestCase testCase;
  int exitCode;

  /// Records if all commands were run, true if they weren't.
  final bool incomplete;

  bool timedOut;
  bool failed = false;
  List<String> stdout;
  List<String> stderr;
  Duration time;
  List<String> diagnostics;
  bool compilationSkipped;

  /**
   * A flag to indicate we have already printed a warning about ignoring the VM
   * crash, to limit the amount of output produced per test.
   */
  bool alreadyPrintedWarning = false;

  /**
   * Set to true if we encounter a condition in the output that indicates we
   * need to rerun this test.
   */
  bool requestRetry = false;

  // Don't call this constructor, call CommandOutput.fromCase() to
  // get a new TestOutput instance.
  CommandOutputImpl(TestCase this.testCase,
                    Command command,
                    int this.exitCode,
                    bool this.incomplete,
                    bool this.timedOut,
                    List<String> this.stdout,
                    List<String> this.stderr,
                    Duration this.time,
                    bool this.compilationSkipped) {
    testCase.commandOutputs[command] = this;
    diagnostics = [];
  }
  factory CommandOutputImpl.fromCase(TestCase testCase,
                                     Command command,
                                     int exitCode,
                                     bool incomplete,
                                     bool timedOut,
                                     List<String> stdout,
                                     List<String> stderr,
                                     Duration time,
                                     bool compilationSkipped) {
    if (testCase is BrowserTestCase) {
      return new BrowserCommandOutputImpl(testCase,
                                          command,
                                          exitCode,
                                          incomplete,
                                          timedOut,
                                          stdout,
                                          stderr,
                                          time,
                                          compilationSkipped);
    } else if (testCase.configuration['compiler'] == 'dartc') {
      return new AnalysisCommandOutputImpl(testCase,
                                           command,
                                           exitCode,
                                           timedOut,
                                           stdout,
                                           stderr,
                                           time,
                                           compilationSkipped);
    }
    return new CommandOutputImpl(testCase,
                                 command,
                                 exitCode,
                                 incomplete,
                                 timedOut,
                                 stdout,
                                 stderr,
                                 time,
                                 compilationSkipped);
  }

  String get result =>
      hasCrashed ? CRASH : (hasTimedOut ? TIMEOUT : (hasFailed ? FAIL : PASS));

  bool get unexpectedOutput => !testCase.expectedOutcomes.contains(result);

  bool get hasCrashed {
    // The Java dartc runner and dart2js exits with code 253 in case
    // of unhandled exceptions.
    if (exitCode == 253) return true;
    if (Platform.operatingSystem == 'windows') {
      // The VM uses std::abort to terminate on asserts.
      // std::abort terminates with exit code 3 on Windows.
      if (exitCode == 3) {
        return !timedOut;
      }
      return (!timedOut && (exitCode < 0) && ((0x3FFFFF00 & exitCode) == 0));
    }
    return !timedOut && ((exitCode < 0));
  }

  bool get hasTimedOut => timedOut;

  bool get didFail {
    return (exitCode != 0 && !hasCrashed);
  }

  // Reverse result of a negative test.
  bool get hasFailed {
    // Always fail if a runtime-error is expected and compilation failed.
    if (testCase.info != null && testCase.info.hasRuntimeError && incomplete) {
      return true;
    }
    return testCase.isNegative ? !didFail : didFail;
  }

}

class BrowserCommandOutputImpl extends CommandOutputImpl {
  BrowserCommandOutputImpl(
      testCase,
      command,
      exitCode,
      incomplete,
      timedOut,
      stdout,
      stderr,
      time,
      compilationSkipped) :
    super(testCase,
          command,
          exitCode,
          incomplete,
          timedOut,
          stdout,
          stderr,
          time,
          compilationSkipped);

  bool get didFail {
    // Browser case:
    // If the browser test failed, it may have been because DumpRenderTree
    // and the virtual framebuffer X server didn't hook up, or DRT crashed with
    // a core dump. Sometimes DRT crashes after it has set the stdout to PASS,
    // so we have to do this check first.
    for (String line in super.stderr) {
      if (line.contains('Gtk-WARNING **: cannot open display: :99') ||
        line.contains('Failed to run command. return code=1')) {
        // If we get the X server error, or DRT crashes with a core dump, retry
        // the test.
        if ((testCase as BrowserTestCase).numRetries > 0) {
          requestRetry = true;
        }
        return true;
      }
    }

    // Browser tests fail unless stdout contains
    // 'Content-Type: text/plain' followed by 'PASS'.
    bool has_content_type = false;
    for (String line in super.stdout) {
      switch (line) {
        case 'Content-Type: text/plain':
          has_content_type = true;
          break;

        case 'PASS':
          if (has_content_type) {
            return (exitCode != 0 && !hasCrashed);
          }
          break;
      }
    }
    return true;
  }
}

// The static analyzer does not actually execute code, so
// the criteria for success now depend on the text sent
// to stderr.
class AnalysisCommandOutputImpl extends CommandOutputImpl {
  // An error line has 8 fields that look like:
  // ERROR|COMPILER|MISSING_SOURCE|file:/tmp/t.dart|15|1|24|Missing source.
  final int ERROR_LEVEL = 0;
  final int ERROR_TYPE = 1;
  final int FORMATTED_ERROR = 7;

  bool alreadyComputed = false;
  bool failResult;

  AnalysisCommandOutputImpl(testCase,
                            command,
                            exitCode,
                            timedOut,
                            stdout,
                            stderr,
                            time,
                            compilationSkipped) :
    super(testCase,
          command,
          exitCode,
          false,
          timedOut,
          stdout,
          stderr,
          time,
          compilationSkipped);

  bool get didFail {
    if (!alreadyComputed) {
      failResult = _didFail();
      alreadyComputed = true;
    }
    return failResult;
  }

  bool _didFail() {
    if (hasCrashed) return false;

    List<String> errors = [];
    List<String> staticWarnings = [];

    // Read the returned list of errors and stuff them away.
    for (String line in super.stderr) {
      if (line.length == 0) continue;
      List<String> fields = splitMachineError(line);
      if (fields[ERROR_LEVEL] == 'ERROR') {
        errors.add(fields[FORMATTED_ERROR]);
      } else if (fields[ERROR_LEVEL] == 'WARNING') {
        // We only care about testing Static type warnings
        // ignore all others
        if (fields[ERROR_TYPE] == 'STATIC_TYPE') {
          staticWarnings.add(fields[FORMATTED_ERROR]);
        }
      }
      // OK to Skip error output that doesn't match the machine format
    }
    if (testCase.info != null
        && testCase.info.optionsFromFile['isMultitest']) {
      return _didMultitestFail(errors, staticWarnings);
    }
    return _didStandardTestFail(errors, staticWarnings);
  }

  bool _didMultitestFail(List errors, List staticWarnings) {
    Set<String> outcome = testCase.info.multitestOutcome;
    Expect.isNotNull(outcome);
    if (outcome.contains('compile-time error') && errors.length > 0) {
      return true;
    } else if (outcome.contains('static type warning')
        && staticWarnings.length > 0) {
      return true;
    } else if (outcome.isEmpty
        && (errors.length > 0 || staticWarnings.length > 0)) {
      return true;
    }
    return false;
  }

  bool _didStandardTestFail(List errors, List staticWarnings) {
    bool hasFatalTypeErrors = false;
    int numStaticTypeAnnotations = 0;
    int numCompileTimeAnnotations = 0;
    var isStaticClean = false;
    if (testCase.info != null) {
      var optionsFromFile = testCase.info.optionsFromFile;
      hasFatalTypeErrors = testCase.info.hasFatalTypeErrors;
      for (Command c in testCase.commands) {
        for (String arg in c.arguments) {
          if (arg == '--fatal-type-errors') {
            hasFatalTypeErrors = true;
            break;
          }
        }
      }
      numStaticTypeAnnotations = optionsFromFile['numStaticTypeAnnotations'];
      numCompileTimeAnnotations = optionsFromFile['numCompileTimeAnnotations'];
      isStaticClean = optionsFromFile['isStaticClean'];
    }

    if (errors.length == 0) {
      if (!hasFatalTypeErrors && exitCode != 0) {
        diagnostics.add("EXIT CODE MISMATCH: Expected error message:");
        diagnostics.add("  command[0]:${testCase.commands[0]}");
        diagnostics.add("  exitCode:${exitCode}");
        return true;
      }
    } else if (exitCode == 0) {
      diagnostics.add("EXIT CODE MISMATCH: Unexpected error message:");
      diagnostics.add("  errors[0]:${errors[0]}");
      diagnostics.add("  command[0]:${testCase.commands[0]}");
      diagnostics.add("  exitCode:${exitCode}");
      return true;
    }
    if (numStaticTypeAnnotations > 0 && isStaticClean) {
      diagnostics.add("Cannot have both @static-clean and /// static "
                      "type warning annotations.");
      return true;
    }

    if (isStaticClean && staticWarnings.length > 0) {
      diagnostics.add(
          "@static-clean annotation found but analyzer returned warnings.");
      return true;
    }

    if (numCompileTimeAnnotations > 0
        && numCompileTimeAnnotations < errors.length) {
      // Expected compile-time errors were not returned.
      // The test did not 'fail' in the way intended so don't return failed.
      diagnostics.add("Fewer compile time errors than annotated: "
          "$numCompileTimeAnnotations");
      return false;
    }

    if (numStaticTypeAnnotations > 0 || hasFatalTypeErrors) {
      // TODO(zundel): match up the annotation line numbers
      // with the reported error line numbers
      if (staticWarnings.length < numStaticTypeAnnotations) {
        diagnostics.add("Fewer static type warnings than annotated: "
            "$numStaticTypeAnnotations");
        return true;
      }
      return false;
    } else if (errors.length != 0) {
      return true;
    }
    return false;
  }

  // Parse a line delimited by the | character using \ as an escape charager
  // like:  FOO|BAR|FOO\|BAR|FOO\\BAZ as 4 fields: FOO BAR FOO|BAR FOO\BAZ
  List<String> splitMachineError(String line) {
    StringBuffer field = new StringBuffer();
    List<String> result = [];
    bool escaped = false;
    for (var i = 0 ; i < line.length; i++) {
      var c = line[i];
      if (!escaped && c == '\\') {
        escaped = true;
        continue;
      }
      escaped = false;
      if (c == '|') {
        result.add(field.toString());
        field.clear();
        continue;
      }
      field.add(c);
    }
    result.add(field.toString());
    return result;
  }
}

/**
 * A RunningProcess actually runs a test, getting the command lines from
 * its [TestCase], starting the test process (and first, a compilation
 * process if the TestCase is a [BrowserTestCase]), creating a timeout
 * timer, and recording the results in a new [CommandOutput] object, which it
 * attaches to the TestCase.  The lifetime of the RunningProcess is limited
 * to the time it takes to start the process, run the process, and record
 * the result; there are no pointers to it, so it should be available to
 * be garbage collected as soon as it is done.
 */
class RunningProcess {
  ProcessQueue processQueue;
  Process process;
  TestCase testCase;
  bool timedOut = false;
  Date startTime;
  Timer timeoutTimer;
  List<String> stdout;
  List<String> stderr;
  bool compilationSkipped;
  bool allowRetries;

  /** Which command of [testCase.commands] is currently being executed. */
  int currentStep;

  RunningProcess(TestCase this.testCase,
      [this.allowRetries = false, this.processQueue]);

  /**
   * Called when all commands are executed.
   */
  void testComplete(CommandOutput lastCommandOutput) {
    if (timeoutTimer != null) {
      timeoutTimer.cancel();
    }
    if (lastCommandOutput.unexpectedOutput
        && testCase.configuration['verbose'] != null
        && testCase.configuration['verbose']) {
      print(testCase.displayName);
      for (var line in lastCommandOutput.stderr) print(line);
      for (var line in lastCommandOutput.stdout) print(line);
    }
    if (allowRetries && testCase.usesWebDriver
        && lastCommandOutput.unexpectedOutput
        && (testCase as BrowserTestCase).numRetries > 0) {
      // Selenium tests can be flaky. Try rerunning.
      lastCommandOutput.requestRetry = true;
    }
    if (lastCommandOutput.requestRetry) {
      lastCommandOutput.requestRetry = false;
      this.timedOut = false;
      (testCase as BrowserTestCase).numRetries--;
      print("Potential flake. Re-running ${testCase.displayName} "
          "(${(testCase as BrowserTestCase).numRetries} attempt(s) remains)");
      // When retrying we need to reset the timeout as well.
      // Otherwise there will be no timeout handling for the retry.
      timeoutTimer = null;
      this.start();
    } else {
      testCase.completed();
    }
  }

  /**
   * Process exit handler called at the end of every command. It internally
   * treats all but the last command as compilation steps. The last command is
   * the actual test and its output is analyzed in [testComplete].
   */
  void commandComplete(Command command, int exitCode) {
    process = null;
    int totalSteps = testCase.commands.length;
    String suffix =' (step $currentStep of $totalSteps)';
    if (timedOut) {
      // Non-webdriver test timed out before it could complete. Webdriver tests
      // run their own timeouts by timing from the launch of the browser (which
      // could be delayed).
      testComplete(createCommandOutput(command, 0, true));
    } else if (currentStep == totalSteps) {
      // Done with all test commands.
      testComplete(createCommandOutput(command, exitCode, false));
    } else if (exitCode != 0) {
      // One of the steps failed.
      stderr.add('test.dart: Compilation failed$suffix, exit code $exitCode\n');
      testComplete(createCommandOutput(command, exitCode, true));
    } else {
      createCommandOutput(command, exitCode, true);
      // One compilation step successfully completed, move on to the
      // next step.
      stderr.add('test.dart: Compilation finished $suffix\n');
      stdout.add('test.dart: Compilation finished $suffix\n');
      if (currentStep == totalSteps - 1 && testCase.usesWebDriver &&
          !testCase.configuration['noBatch']) {
        // Note: processQueue will always be non-null for runtime == ie9, ie10,
        // ff, safari, chrome, opera. (It is only null for runtime == vm)
        // This RunningProcess object is done, and hands over control to
        // BatchRunner.startTest(), which handles reporting, etc.
        if (timeoutTimer != null) {
          timeoutTimer.cancel();
        }
        processQueue._getBatchRunner(testCase).startTest(testCase);
      } else {
        runCommand(testCase.commands[currentStep++], commandComplete);
      }
    }
  }

  /**
   * Called for all executed commands.
   */
  CommandOutput createCommandOutput(Command command,
                                    int exitCode,
                                    bool incomplete) {
    var commandOutput = new CommandOutput.fromCase(
        testCase,
        command,
        exitCode,
        incomplete,
        timedOut,
        stdout,
        stderr,
        new Date.now().difference(startTime),
        compilationSkipped);
    resetLocalOutputInformation();
    return commandOutput;
  }

  void resetLocalOutputInformation() {
    stdout = new List<String>();
    stderr = new List<String>();
    compilationSkipped = false;
  }

  VoidFunction makeReadHandler(StringInputStream source,
                               List<String> destination) {
    void handler () {
      if (source.closed) return;  // TODO(whesse): Remove when bug is fixed.
      var line = source.readLine();
      while (null != line) {
        destination.add(line);
        line = source.readLine();
      }
    }
    return handler;
  }

  void start() {
    Expect.isFalse(testCase.expectedOutcomes.contains(SKIP));
    resetLocalOutputInformation();
    currentStep = 0;
    startTime = new Date.now();
    runCommand(testCase.commands[currentStep++], commandComplete);
  }

  void runCommand(Command command, void commandCompleteHandler(Command, int)) {
    void processExitHandler(int returnCode) {
      commandCompleteHandler(command, returnCode);
    }

    command.outputIsUpToDate.then((bool isUpToDate) {
      if (isUpToDate) {
        stdout.add("Skipped compilation because the old output is "
                   "still up to date!");
        compilationSkipped = true;
        commandComplete(command, 0);
      } else {
        ProcessOptions options = new ProcessOptions();
        options.environment =
            new Map<String, String>.from(Platform.environment);
        options.environment['DART_CONFIGURATION'] =
            TestUtils.configurationDir(testCase.configuration);
        Future processFuture = Process.start(command.executable,
                                             command.arguments,
                                             options);
        processFuture.then((Process p) {
          process = p;
          process.onExit = processExitHandler;
          var stdoutStringStream = new StringInputStream(process.stdout);
          var stderrStringStream = new StringInputStream(process.stderr);
          stdoutStringStream.onLine =
              makeReadHandler(stdoutStringStream, stdout);
          stderrStringStream.onLine =
              makeReadHandler(stderrStringStream, stderr);
          if (timeoutTimer == null) {
            // Create one timeout timer when starting test case, remove it at
            // the end.
            timeoutTimer = new Timer(1000 * testCase.timeout, timeoutHandler);
          }
          // If the timeout fired in between two commands, kill the just
          // started process immediately.
          if (timedOut) safeKill(process);
        });
        processFuture.handleException((e) {
          print("Process error:");
          print("  Command: $command");
          print("  Error: $e");
          testComplete(createCommandOutput(command, -1, false));
          return true;
        });
      }
    });
  }

  void timeoutHandler(Timer unusedTimer) {
    timedOut = true;
    safeKill(process);
  }

  void safeKill(Process p) {
    if (p != null) {
      try {
        p.kill();
      } on ProcessException {
        // Hopefully, this means that the process died on its own.
      }
    }
  }
}

/**
 * This class holds a value, that can be changed.  It is used when
 * closures need a shared value, that they can all change and read.
 */
class MutableValue<T> {
  MutableValue(T this.value);
  T value;
}

class BatchRunnerProcess {
  Command _command;
  String _executable;
  List<String> _batchArguments;

  Process _process;
  StringInputStream _stdoutStream;
  StringInputStream _stderrStream;

  TestCase _currentTest;
  List<String> _testStdout;
  List<String> _testStderr;
  String _status;
  bool _stdoutDrained = false;
  bool _stderrDrained = false;
  MutableValue<bool> _ignoreStreams;
  Date _startTime;
  Timer _timer;

  bool _isWebDriver;

  BatchRunnerProcess(TestCase testCase) {
    _command = testCase.commands.last;
    _executable = testCase.commands.last.executable;
    _batchArguments = testCase.batchRunnerArguments;
    _isWebDriver = testCase.usesWebDriver;
  }

  bool get active => _currentTest != null;

  void startTest(TestCase testCase) {
    Expect.isNull(_currentTest);
    _currentTest = testCase;
    _command = testCase.commands.last;
    if (_process == null) {
      // Start process if not yet started.
      _executable = testCase.commands.last.executable;
      _startProcess(() {
        doStartTest(testCase);
      });
    } else if (testCase.commands.last.executable != _executable) {
      // Restart this runner with the right executable for this test
      // if needed.
      _executable = testCase.commands.last.executable;
      _batchArguments = testCase.batchRunnerArguments;
      _process.onExit = (exitCode) {
        _startProcess(() {
          doStartTest(testCase);
        });
      };
      _process.kill();
    } else {
      doStartTest(testCase);
    }
  }

  Future terminate() {
    if (_process == null) return new Future.immediate(true);
    Completer completer = new Completer();
    Timer killTimer;
    _process.onExit = (exitCode) {
      if (killTimer != null) killTimer.cancel();
      completer.complete(true);
    };
    if (_isWebDriver) {
      // Use a graceful shutdown so our Selenium script can close
      // the open browser processes. On Windows, signals do not exist
      // and a kill is a hard kill.
      _process.stdin.write('--terminate\n'.charCodes);

      // In case the run_selenium process didn't close, kill it after 30s
      int shutdownMillisecs = 30000;
      killTimer = new Timer(shutdownMillisecs, (e) { _process.kill(); });
    } else {
      _process.kill();
    }

    return completer.future;
  }

  void doStartTest(TestCase testCase) {
    _startTime = new Date.now();
    _testStdout = [];
    _testStderr = [];
    _status = null;
    _stdoutDrained = false;
    _stderrDrained = false;
    _ignoreStreams = new MutableValue<bool>(false);  // Captured by closures.
    _stdoutStream.onLine = _readStdout(_stdoutStream, _testStdout);
    _stderrStream.onLine = _readStderr(_stderrStream, _testStderr);
    _timer = new Timer(testCase.timeout * 1000, _timeoutHandler);
    var line = _createArgumentsLine(testCase.batchTestArguments);
    _process.stdin.onError = (err) {
      print('Error on batch runner input stream stdin');
      print('  Input line: $line');
      print('  Previous test\'s status: $_status');
      print('  Error: $err');
      throw err;
    };
    _process.stdin.write(line.charCodes);
  }

  String _createArgumentsLine(List<String> arguments) {
    return Strings.join(arguments, ' ').concat('\n');
  }

  void _reportResult() {
    if (!active) return;
    // _status == '>>> TEST {PASS, FAIL, OK, CRASH, FAIL, TIMEOUT}'

    var outcome = _status.split(" ")[2];
    var exitCode = 0;
    if (outcome == "CRASH") exitCode = -10;
    if (outcome == "FAIL" || outcome == "TIMEOUT") exitCode = 1;
    new CommandOutput.fromCase(_currentTest,
                               _command,
                               exitCode,
                               false,
                               (outcome == "TIMEOUT"),
                               _testStdout,
                               _testStderr,
                               new Date.now().difference(_startTime),
                               false);
    var test = _currentTest;
    _currentTest = null;
    test.completed();
  }

  void _stderrDone() {
    _stderrDrained = true;
    // Move on when both stdout and stderr has been drained.
    if (_stdoutDrained) _reportResult();
  }

  void _stdoutDone() {
    _stdoutDrained = true;
    // Move on when both stdout and stderr has been drained.
    if (_stderrDrained) _reportResult();
  }

  VoidFunction _readStdout(StringInputStream stream, List<String> buffer) {
    var ignoreStreams = _ignoreStreams;  // Capture this mutable object.
    void reader() {
      if (ignoreStreams.value) {
         while (stream.readLine() != null) {
          // Do nothing.
        }
        return;
      }
      // Otherwise, process output and call _reportResult() when done.
      var line = stream.readLine();
      while (line != null) {
        if (line.startsWith('>>> TEST')) {
          _status = line;
        } else if (line.startsWith('>>> BATCH START')) {
          // ignore
        } else if (line.startsWith('>>> ')) {
          throw new Exception('Unexpected command from dartc batch runner.');
        } else {
          buffer.add(line);
        }
        line = stream.readLine();
      }
      if (_status != null) {
        _timer.cancel();
        _stdoutDone();
      }
    }
    return reader;
  }

  VoidFunction _readStderr(StringInputStream stream, List<String> buffer) {
    var ignoreStreams = _ignoreStreams;  // Capture this mutable object.
    void reader() {
      if (ignoreStreams.value) {
        while (stream.readLine() != null) {
          // Do nothing.
        }
        return;
      }
      // Otherwise, process output and call _reportResult() when done.
      var line = stream.readLine();
      while (line != null) {
        if (line.startsWith('>>> EOF STDERR')) {
          _stderrDone();
        } else {
          buffer.add(line);
        }
        line = stream.readLine();
      }
    }
    return reader;
  }

  ExitCodeEvent makeExitHandler(String status) {
    void handler(int exitCode) {
      if (active) {
        if (_timer != null) _timer.cancel();
        _status = status;
        // Read current content of streams, ignore any later output.
        _ignoreStreams.value = true;
        var line = _stdoutStream.readLine();
        while (line != null) {
          _testStdout.add(line);
          line = _stdoutStream.readLine();
        }
        line = _stderrStream.readLine();
        while (line != null) {
          _testStderr.add(line);
          line = _stderrStream.readLine();
        }
        _stderrDrained = true;
        _stdoutDrained = true;
        _startProcess(_reportResult);
      } else {  // No active test case running.
        _process = null;
      }
    }
    return handler;
  }

  void _timeoutHandler(ignore) {
    _process.onExit = makeExitHandler(">>> TEST TIMEOUT");
    _process.kill();
  }

  _startProcess(callback) {
    Future processFuture = Process.start(_executable, _batchArguments);
    processFuture.then((Process p) {
      _process = p;
      _stdoutStream = new StringInputStream(_process.stdout);
      _stderrStream = new StringInputStream(_process.stderr);
      _process.onExit = makeExitHandler(">>> TEST CRASH");
      callback();
    });
    processFuture.handleException((e) {
      print("Process error:");
      print("  Command: $_executable ${Strings.join(_batchArguments, ' ')}");
      print("  Error: $e");
      // If there is an error starting a batch process, chances are that
      // it will always fail. So rather than re-trying a 1000+ times, we
      // exit.
      exit(1);
      return true;
    });
  }
}

/**
 * ProcessQueue is the master control class, responsible for running all
 * the tests in all the TestSuites that have been registered.  It includes
 * a rate-limited queue to run a limited number of tests in parallel,
 * a ProgressIndicator which prints output when tests are started and
 * and completed, and a summary report when all tests are completed,
 * and counters to determine when all of the tests in all of the test suites
 * have completed.
 *
 * Because multiple configurations may be run on each test suite, the
 * ProcessQueue contains a cache in which a test suite may record information
 * about its list of tests, and may retrieve that information when it is called
 * upon to enqueue its tests again.
 */
class ProcessQueue {
  int _numProcesses = 0;
  int _activeTestListers = 0;
  int _maxProcesses;

  /** The number of tests we allow to actually fail before we stop retrying. */
  int _MAX_FAILED_NO_RETRY = 4;
  bool _verbose;
  bool _listTests;
  Function _allDone;
  EnqueueMoreWork _enqueueMoreWork;
  Queue<TestCase> _tests;
  ProgressIndicator _progress;

  // For dartc/selenium batch processing we keep a list of batch processes.
  Map<String, List<BatchRunnerProcess>> _batchProcesses;

  // Cache information about test cases per test suite. For multiple
  // configurations there is no need to repeatedly search the file
  // system, generate tests, and search test files for options.
  Map<String, List<TestInformation>> _testCache;

  /**
   * String indicating the browser used to run the tests. Empty if no browser
   * used.
   */
  String browserUsed = '';

  /**
   * Process running the selenium server .jar (only used for Safari and Opera
   * tests.)
   */
  Process _seleniumServer = null;

  /** True if we are in the process of starting the server. */
  bool _startingServer = false;

  /** True if we find that there is already a selenium jar running. */
  bool _seleniumAlreadyRunning = false;

  ProcessQueue(int this._maxProcesses,
               String progress,
               Date startTime,
               bool printTiming,
               this._enqueueMoreWork,
               this._allDone,
               [bool verbose = false,
                bool listTests = false])
      : _verbose = verbose,
        _listTests = listTests,
        _tests = new Queue<TestCase>(),
        _progress = new ProgressIndicator.fromName(progress,
                                                   startTime,
                                                   printTiming),
        _batchProcesses = new Map<String, List<BatchRunnerProcess>>(),
        _testCache = new Map<String, List<TestInformation>>() {
    _checkDone();
  }

  /**
   * Registers a TestSuite so that all of its tests will be run.
   */
  void addTestSuite(TestSuite testSuite) {
    _activeTestListers++;
    testSuite.forEachTest(_runTest, _testCache, _testListerDone);
  }

  void _testListerDone() {
    _activeTestListers--;
    _checkDone();
  }

  /**
   * Perform any cleanup needed once all tests in a TestSuite have completed
   * and notify our progress indicator that we are done.
   */
  void _cleanupAndMarkDone() {
    _allDone();
    if (browserUsed != '' && _seleniumServer != null) {
      _seleniumServer.kill();
    } else {
      _progress.allDone();
    }
  }

  void _checkDone() {
    // When there are no more active test listers ask for more work
    // from process queue users.
    if (_activeTestListers == 0) {
     _enqueueMoreWork(this);
    }
    // If there is still no work, we are done.
    if (_activeTestListers == 0) {
      _progress.allTestsKnown();
      if (_tests.isEmpty && _numProcesses == 0) {
        _terminateBatchRunners().then((_) => _cleanupAndMarkDone());
      }
    }
  }

  /**
   * True if we are using a browser + platform combination that needs the
   * Selenium server jar.
   */
  bool get _needsSelenium => (Platform.operatingSystem == 'macos' &&
      browserUsed == 'safari') || browserUsed == 'opera';

  /** True if the Selenium Server is ready to be used. */
  bool get _isSeleniumAvailable => _seleniumServer != null ||
      _seleniumAlreadyRunning;

  /**
   * Restart all the processes that have been waiting/stopped for the server to
   * start up. If we just call this once we end up with a single-"threaded" run.
   */
  void resumeTesting() {
    for (int i = 0; i < _maxProcesses; i++) _tryRunTest();
  }

  /** Start the Selenium Server jar, if appropriate for this platform. */
  void _ensureSeleniumServerRunning() {
    if (!_isSeleniumAvailable && !_startingServer) {
      _startingServer = true;

      // Check to see if the jar was already running before the program started.
      String cmd = 'ps';
      var arg = ['aux'];
      if (Platform.operatingSystem == 'windows') {
        cmd = 'tasklist';
        arg.add('/v');
      }

      Future processFuture = Process.start(cmd, arg);
      processFuture.then((Process p) {
        // Drain stderr to not leak resources.
        p.stderr.onData = p.stderr.read;
        final StringInputStream stdoutStringStream =
            new StringInputStream(p.stdout);
        stdoutStringStream.onLine = () {
          var line = stdoutStringStream.readLine();
          while (null != line) {
            var regexp = new RegExp(r".*selenium-server-standalone.*");
            if (regexp.hasMatch(line)) {
              _seleniumAlreadyRunning = true;
              resumeTesting();
            }
            line = stdoutStringStream.readLine();
          }
          if (!_isSeleniumAvailable) {
            _startSeleniumServer();
          }
        };
      });
      processFuture.handleException((e) {
        print("Error starting process:");
        print("  Command: $cmd ${Strings.join(arg, ' ')}");
        print("  Error: $e");
        // TODO(ahe): How to report this as a test failure?
        exit(1);
        return true;
      });
    }
  }

  void _runTest(TestCase test) {
    if (test.usesWebDriver) {
      browserUsed = test.configuration['browser'];
      if (_needsSelenium) _ensureSeleniumServerRunning();
    }
    _progress.testAdded();
    _tests.add(test);
    _tryRunTest();
  }

  /**
   * Monitor the output of the Selenium server, to know when we are ready to
   * begin running tests.
   * source: Output(Stream) from the Java server.
   */
  VoidFunction makeSeleniumServerHandler(StringInputStream source) {
    void handler() {
      if (source.closed) return;  // TODO(whesse): Remove when bug is fixed.
      var line = source.readLine();
      while (null != line) {
        if (new RegExp(r".*Started.*Server.*").hasMatch(line) ||
            new RegExp(r"Exception.*Selenium is already running.*").hasMatch(
            line)) {
          resumeTesting();
        }
        line = source.readLine();
      }
    }
    return handler;
  }

  /**
   * For browser tests using Safari or Opera, we need to use the Selenium 1.0
   * Java server.
   */
  void _startSeleniumServer() {
    // Get the absolute path to the Selenium jar.
    String filePath = TestUtils.testScriptPath;
    String pathSep = Platform.pathSeparator;
    int index = filePath.lastIndexOf(pathSep);
    filePath = '${filePath.substring(0, index)}${pathSep}testing${pathSep}';
    var lister = new Directory(filePath).list();
    lister.onFile = (String file) {
      if (new RegExp(r"selenium-server-standalone-.*\.jar").hasMatch(file)
          && _seleniumServer == null) {
        Future processFuture = Process.start('java', ['-jar', file]);
        processFuture.then((Process server) {
          _seleniumServer = server;
          // Heads up: there seems to an obscure data race of some form in
          // the VM between launching the server process and launching the test
          // tasks that disappears when you read IO (which is convenient, since
          // that is our condition for knowing that the server is ready).
          StringInputStream stdoutStringStream =
              new StringInputStream(_seleniumServer.stdout);
          StringInputStream stderrStringStream =
              new StringInputStream(_seleniumServer.stderr);
          stdoutStringStream.onLine =
              makeSeleniumServerHandler(stdoutStringStream);
          stderrStringStream.onLine =
              makeSeleniumServerHandler(stderrStringStream);
        });
        processFuture.handleException((e) {
          print("Process error:");
          print("  Command: java -jar $file");
          print("  Error: $e");
          // TODO(ahe): How to report this as a test failure?
          exit(1);
          return true;
        });
      }
    };
  }

  Future _terminateBatchRunners() {
    var futures = new List();
    for (var runners in _batchProcesses.values) {
      for (var runner in runners) {
        futures.add(runner.terminate());
      }
    }
    return Futures.wait(futures);
  }

  BatchRunnerProcess _getBatchRunner(TestCase test) {
    // Start batch processes if needed
    var compiler = test.configuration['compiler'];
    var runners = _batchProcesses[compiler];
    if (runners == null) {
      runners = new List<BatchRunnerProcess>(_maxProcesses);
      for (int i = 0; i < _maxProcesses; i++) {
        runners[i] = new BatchRunnerProcess(test);
      }
      _batchProcesses[compiler] = runners;
    }

    for (var runner in runners) {
      if (!runner.active) return runner;
    }
    throw new Exception('Unable to find inactive batch runner.');
  }

  void _tryRunTest() {
    _checkDone();
    if (_numProcesses < _maxProcesses && !_tests.isEmpty) {
      TestCase test = _tests.removeFirst();
      if (_listTests) {
        var fields = [test.displayName,
                      Strings.join(new List.from(test.expectedOutcomes), ','),
                      test.isNegative.toString()];
        fields.addAll(test.commands.last.arguments);
        print(Strings.join(fields, '\t'));
        return;
      }
      if (test.usesWebDriver && _needsSelenium && !_isSeleniumAvailable || (test
          is BrowserTestCase && test.waitingForOtherTest)) {
        // The test is not yet ready to run. Put the test back in
        // the queue.  Avoid spin-polling by using a timeout.
        _tests.add(test);
        new Timer(100, (timer) {_tryRunTest();});  // Don't lose a process.
        return;
      }
      if (_verbose) {
        int i = 1;
        for (Command command in test.commands) {
          print('$i. ${command.commandLine}');
          i++;
        }
      }
      _progress.start(test);
      TestCaseEvent oldCallback = test.completedHandler;
      void wrapper(TestCase test_arg) {
        _numProcesses--;
        _progress.done(test_arg);
        if (test_arg is BrowserTestCase) test_arg.notifyObservers();
        _tryRunTest();
        oldCallback(test_arg);
      };
      test.completedHandler = wrapper;

      if ((test.configuration['compiler'] == 'dartc' &&
           test.displayName != 'dartc/junit_tests') ||
          (test.commands.length == 1 && test.usesWebDriver &&
           !test.configuration['noBatch'])) {
        // Dartc and browser test cases that do not require a precompilation
        // step, start with the batch runner right away.
        _getBatchRunner(test).startTest(test);
      } else {
        // Once we've actually failed a test, technically, we wouldn't need to
        // bother retrying any subsequent tests since the bot is already red.
        // However, we continue to retry tests until we have actually failed
        // four tests (arbitrarily chosen) for more debugable output, so that
        // the developer doesn't waste his or her time trying to fix a bunch of
        // tests that appear to be broken but were actually just flakes that
        // didn't get retried because there had already been one failure.
        bool allowRetry = _MAX_FAILED_NO_RETRY > _progress.numFailedTests;
        new RunningProcess(test, allowRetry, this).start();
      }
      _numProcesses++;
    }
  }
}
