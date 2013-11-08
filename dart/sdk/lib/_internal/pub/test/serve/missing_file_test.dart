// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS d.file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub_tests;

import 'package:path/path.dart' as path;
import 'package:scheduled_test/scheduled_test.dart';

import '../../lib/src/io.dart';
import '../descriptor.dart' as d;
import '../test_pub.dart';
import 'utils.dart';

main() {
  initConfig();
  integration("responds with a 404 for missing files", () {
    d.dir(appPath, [
      d.appPubspec(),
      d.dir("asset", [
        d.file("nope.png", "nope")
      ]),
      d.dir("lib", [
        d.file("nope.dart", "nope")
      ]),
      d.dir("web", [
        d.file("index.html", "<body>"),
      ])
    ]).create();

    // Start the server with the files present so that it creates barback
    // assets for them.
    pubServe();

    // TODO(rnystrom): When pub serve supports file watching, we'll have to do
    // something here to specifically disable that so that we can get barback
    // into the inconsistent state of thinking there is an asset but where the
    // underlying file does not exist. One option would be configure barback
    // with an insanely long delay between polling to ensure a poll doesn't
    // happen.

    // Now delete them.
    schedule(() {
      deleteEntry(path.join(sandboxDir, appPath, "asset", "nope.png"));
      deleteEntry(path.join(sandboxDir, appPath, "lib", "nope.dart"));
      deleteEntry(path.join(sandboxDir, appPath, "web", "index.html"));
    }, "delete files");

    requestShould404("index.html");
    requestShould404("packages/myapp/nope.dart");
    requestShould404("assets/myapp/nope.png");
    requestShould404("dir/packages/myapp/nope.dart");
    requestShould404("dir/assets/myapp/nope.png");
    endPubServe();
  });
}
