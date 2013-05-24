// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub_tests;

import 'dart:io';

import '../../descriptor.dart' as d;
import '../../test_pub.dart';

main() {
  initConfig();
  integration("updates Git packages to a nonexistent pubspec", () {
    ensureGit();

    var repo = d.git('foo.git', [
      d.libDir('foo'),
      d.libPubspec('foo', '1.0.0')
    ]);
    repo.create();

    d.appDir([{"git": "../foo.git"}]).create();

    pubInstall();

    d.dir(packagesPath, [
      d.dir('foo', [
        d.file('foo.dart', 'main() => "foo";')
      ])
    ]).validate();

    repo.runGit(['rm', 'pubspec.yaml']);
    repo.runGit(['commit', '-m', 'delete']);

    pubUpdate(error: 'Package "foo" doesn\'t have a pubspec.yaml file.');

    d.dir(packagesPath, [
      d.dir('foo', [
        d.file('foo.dart', 'main() => "foo";')
      ])
    ]).validate();
  });
}
