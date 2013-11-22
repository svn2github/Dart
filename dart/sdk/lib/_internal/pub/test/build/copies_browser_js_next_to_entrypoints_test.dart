// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:convert';

import 'package:path/path.dart' as path;
import 'package:scheduled_test/scheduled_test.dart';

import '../descriptor.dart' as d;
import '../test_pub.dart';

main() {
  initConfig();

  integration("compiles dart.js and interop.js next to entrypoints", () {
    // Dart2js can take a long time to compile dart code, so we increase the
    // timeout to cope with that.
    currentSchedule.timeout *= 3;

    serve([
      d.dir('api', [
        d.dir('packages', [
          d.file('browser', JSON.encode({
            'versions': [packageVersionApiMap(packageMap('browser', '1.0.0'))]
          })),
          d.dir('browser', [
            d.dir('versions', [
              d.file('1.0.0', JSON.encode(
                  packageVersionApiMap(
                      packageMap('browser', '1.0.0'),
                      full: true)))
            ])
          ])
        ])
      ]),
      d.dir('packages', [
        d.dir('browser', [
          d.dir('versions', [
            d.tar('1.0.0.tar.gz', [
              d.file('pubspec.yaml', yaml(packageMap("browser", "1.0.0"))),
              d.dir('lib', [
                d.file('dart.js', 'contents of dart.js'),
                d.file('interop.js', 'contents of interop.js')
              ])
            ])
          ])
        ])
      ])
    ]);

    d.dir(appPath, [
      d.appPubspec({"browser": "1.0.0"}),
      d.dir('web', [
        d.file('file.dart', 'void main() => print("hello");'),
        d.dir('subdir', [
          d.file('subfile.dart', 'void main() => print("subhello");')
        ])
      ])
    ]).create();

    pubGet();

    schedulePub(args: ["build"],
        output: new RegExp(r"Built 8 files!"),
        exitCode: 0);

    d.dir(appPath, [
      d.dir('build', [
        d.matcherFile('file.dart.js', isNot(isEmpty)),
        d.dir('packages', [d.dir('browser', [
          d.file('dart.js', 'contents of dart.js'),
          d.file('interop.js', 'contents of interop.js')
        ])]),
        d.dir('subdir', [
          d.dir('packages', [d.dir('browser', [
            d.file('dart.js', 'contents of dart.js'),
            d.file('interop.js', 'contents of interop.js')
          ])]),
          d.matcherFile('subfile.dart.js', isNot(isEmpty))
        ])
      ])
    ]).validate();
  });
}
