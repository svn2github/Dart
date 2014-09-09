library pub_tests;
import '../../descriptor.dart' as d;
import '../../test_pub.dart';
main() {
  initConfig();
  integration("upgrades dependencies whose constraints have been removed", () {
    servePackages((builder) {
      builder.serve("foo", "1.0.0", deps: {
        "shared-dep": "any"
      });
      builder.serve("bar", "1.0.0", deps: {
        "shared-dep": "<2.0.0"
      });
      builder.serve("shared-dep", "1.0.0");
      builder.serve("shared-dep", "2.0.0");
    });
    d.appDir({
      "foo": "any",
      "bar": "any"
    }).create();
    pubUpgrade();
    d.packagesDir({
      "foo": "1.0.0",
      "bar": "1.0.0",
      "shared-dep": "1.0.0"
    }).validate();
    d.appDir({
      "foo": "any"
    }).create();
    pubUpgrade();
    d.packagesDir({
      "foo": "1.0.0",
      "bar": null,
      "shared-dep": "2.0.0"
    }).validate();
  });
}
