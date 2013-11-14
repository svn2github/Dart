// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:scheduled_test/scheduled_test.dart';

import '../descriptor.dart' as d;
import '../test_pub.dart';

main() {
  initConfig();

  // TODO(rnystrom): Should also add tests that other transformers work
  // (#14556).

  integration("compiles Dart entrypoints to JS", () {
    // Dart2js can take a long time to compile dart code, so we increase the
    // timeout to cope with that.
    currentSchedule.timeout *= 3;

    d.dir(appPath, [
      d.appPubspec(),
      d.dir('web', [
        d.file('file.dart', 'void main() => print("hello");'),
        d.file('lib.dart', 'void foo() => print("hello");'),
        d.dir('subdir', [
          d.file('subfile.dart', 'void main() => print("ping");')
        ])
      ])
    ]).create();

    // TODO(rnystrom): If we flesh out the command-line output, validate that
    // here.
    schedulePub(args: ["build"],
        output: new RegExp(r"Built 2 files!"),
        exitCode: 0);

    d.dir(appPath, [
      d.dir('build', [
        d.matcherFile('file.dart.js', isNot(isEmpty)),
        d.nothing('file.dart'),
        d.nothing('lib.dart'),
        d.dir('subdir', [
          d.matcherFile('subfile.dart.js', isNot(isEmpty)),
          d.nothing('subfile.dart')
        ])
      ])
    ]).validate();
  });
}
