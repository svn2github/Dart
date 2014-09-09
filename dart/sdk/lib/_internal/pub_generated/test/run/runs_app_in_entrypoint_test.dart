import '../descriptor.dart' as d;
import '../test_pub.dart';
const SCRIPT = """
import 'dart:io';

main() {
  stdout.writeln("stdout output");
  stderr.writeln("stderr output");
  exitCode = 123;
}
""";
main() {
  initConfig();
  integration('runs a Dart application in the entrypoint package', () {
    d.dir(
        appPath,
        [d.appPubspec(), d.dir("bin", [d.file("script.dart", SCRIPT)])]).create();
    var pub = pubRun(args: ["script"]);
    pub.stdout.expect("stdout output");
    pub.stderr.expect("stderr output");
    pub.shouldExit(123);
  });
}
