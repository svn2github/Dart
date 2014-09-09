library pub_tests;
import 'package:path/path.dart' as p;
import 'package:scheduled_test/scheduled_test.dart';
import '../descriptor.dart' as d;
import '../test_pub.dart';
main() {
  initConfig();
  integration(
      "doesn't create a snapshot for a package that depends on the " "entrypoint",
      () {
    servePackages((builder) {
      builder.serve("foo", "1.2.3", deps: {
        'bar': '1.2.3'
      },
          contents: [
              d.dir("bin", [d.file("hello.dart", "void main() => print('hello!');")])]);
      builder.serve("bar", "1.2.3", deps: {
        'myapp': 'any'
      });
    });
    d.appDir({
      "foo": "1.2.3"
    }).create();
    pubGet();
    d.nothing(p.join(appPath, '.pub', 'bin')).validate();
  });
}
