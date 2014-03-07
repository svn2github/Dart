// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS d.file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub_tests;

import 'package:scheduled_test/scheduled_test.dart';

import '../../descriptor.dart' as d;
import '../../test_pub.dart';

main() {
  initConfig();
  integration("includes proper URLs in generated JS and source map", () {
    d.dir(appPath, [
      d.appPubspec(),
      d.dir("web", [
        d.file("main.dart", "void main() => print('hello');")
      ])
    ]).create();

    schedulePub(args: ["build"],
        output: new RegExp(r'Built 3 files to "build".'),
        exitCode: 0);

    d.dir(appPath, [
      d.dir('build', [
        d.dir('web', [
          d.matcherFile('main.dart.js', allOf([
            contains("# sourceMappingURL=main.dart.js.map"),
            contains("@ sourceMappingURL=main.dart.js.map")
          ])),
          d.matcherFile('main.dart.js.map', contains('"file": "main.dart.js"'))
        ])
      ])
    ]).validate();
  });
}
