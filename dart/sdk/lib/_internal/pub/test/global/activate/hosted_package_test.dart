// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:scheduled_test/scheduled_test.dart';

import '../../descriptor.dart' as d;
import '../../test_pub.dart';

main() {
  initConfig();
  integration('installs and activates the best version of a package', () {
    servePackages([
      packageMap("foo", "1.0.0"),
      packageMap("foo", "1.2.3"),
      packageMap("foo", "2.0.0-wildly.unstable")
    ]);

    schedulePub(args: ["global", "activate", "foo"], output: """
Downloading foo 1.2.3...
Resolving dependencies...
Activated foo 1.2.3.
    """);

    // Should be in global package cache.
    d.dir(cachePath, [
      d.dir('global_packages', [
        d.matcherFile('foo.lock', contains('1.2.3'))
      ])
    ]).validate();
  });
}
