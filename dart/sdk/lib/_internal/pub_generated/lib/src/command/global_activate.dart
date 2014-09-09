library pub.command.global_activate;
import 'dart:async';
import '../command.dart';
import '../utils.dart';
import '../version.dart';
class GlobalActivateCommand extends PubCommand {
  String get description => "Make a package's executables globally available.";
  String get usage => "pub global activate <package...>";
  bool get takesArguments => true;
  GlobalActivateCommand() {
    commandParser.addOption(
        "source",
        abbr: "s",
        help: "The source used to find the package.",
        allowed: ["git", "hosted", "path"],
        defaultsTo: "hosted");
  }
  Future onRun() {
    var args = commandOptions.rest;
    readArg([String error]) {
      if (args.isEmpty) usageError(error);
      var arg = args.first;
      args = args.skip(1);
      return arg;
    }
    validateNoExtraArgs() {
      if (args.isEmpty) return;
      var unexpected = args.map((arg) => '"$arg"');
      var arguments = pluralize("argument", unexpected.length);
      usageError("Unexpected $arguments ${toSentence(unexpected)}.");
    }
    switch (commandOptions["source"]) {
      case "git":
        var repo = readArg("No Git repository given.");
        validateNoExtraArgs();
        return globals.activateGit(repo);
      case "hosted":
        var package = readArg("No package to activate given.");
        var constraint = VersionConstraint.any;
        if (args.isNotEmpty) {
          try {
            constraint = new VersionConstraint.parse(readArg());
          } on FormatException catch (error) {
            usageError(error.message);
          }
        }
        validateNoExtraArgs();
        return globals.activateHosted(package, constraint);
      case "path":
        var path = readArg("No package to activate given.");
        validateNoExtraArgs();
        return globals.activatePath(path);
    }
    throw "unreachable";
  }
}
