library pub_tests;
import '../descriptor.dart' as d;
import '../test_pub.dart';
const TRANSFORMER = """
import 'dart:async';

import 'package:barback/barback.dart';

class ModeTransformer extends Transformer {
  final BarbackSettings settings;
  ModeTransformer.asPlugin(this.settings);

  String get allowedExtensions => '.txt';

  Future apply(Transform transform) {
    return new Future.value().then((_) {
      var id = transform.primaryInput.id.changeExtension(".out");
      transform.addOutput(new Asset.fromString(id, settings.mode.toString()));
    });
  }
}
""";
main() {
  initConfig();
  withBarbackVersions("any", () {
    integration("defaults to release mode", () {
      d.dir(appPath, [d.pubspec({
          "name": "myapp",
          "transformers": ["myapp/src/transformer"]
        }),
            d.dir("lib", [d.dir("src", [d.file("transformer.dart", TRANSFORMER)])]),
            d.dir("web", [d.file("foo.txt", "foo")])]).create();
      createLockFile('myapp', pkg: ['barback']);
      schedulePub(args: ["build"]);
      d.dir(
          appPath,
          [d.dir('build', [d.dir('web', [d.file('foo.out', 'release')])])]).validate();
    });
  });
}
