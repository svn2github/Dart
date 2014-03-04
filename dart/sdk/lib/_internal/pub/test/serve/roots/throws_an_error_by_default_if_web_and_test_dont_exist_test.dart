// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS d.file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub_tests;

import '../../../lib/src/exit_codes.dart' as exit_codes;

import '../../descriptor.dart' as d;
import '../../test_pub.dart';
import '../utils.dart';

main() {
  initConfig();
  integration("throws an error by default if web and test don't exist", () {
    d.dir(appPath, [
      d.appPubspec()
    ]).create();

    var server = startPubServe(createWebDir: false);
    server.stderr.expect(emitsLines(
        'Your package must have "web" and/or "test" directories to serve,\n'
        'or you must pass in directories to serve explicitly.'));
    server.shouldExit(exit_codes.USAGE);
  });
}
