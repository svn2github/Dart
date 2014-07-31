// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:path/path.dart' as p;

import '../../../lib/src/io.dart';
import '../../descriptor.dart' as d;
import '../../test_pub.dart';

main() {
  initConfig();
  integration('activating a hosted package deactivates the path one', () {
    servePackages([
      packageMap("foo", "2.0.0")
    ], contents: [
      d.dir("bin", [
        d.file("foo.dart", "main(args) => print('hosted');")
      ])
    ]);

    d.dir("foo", [
      d.libPubspec("foo", "1.0.0"),
      d.dir("bin", [
        d.file("foo.dart", "main() => print('path');")
      ])
    ]).create();

    schedulePub(args: ["global", "activate", "-spath", "../foo"]);
    schedulePub(args: ["global", "activate", "foo"], output: """
        Package foo is already active at path "${canonicalize(p.join(sandboxDir, "foo"))}".
        Downloading foo 2.0.0...
        Resolving dependencies...
        Activated foo 2.0.0.""");

    // Should now run the hosted one.
    var pub = pubRun(global: true, args: ["foo"]);
    pub.stdout.expect("hosted");
    pub.shouldExit();
  });
}
