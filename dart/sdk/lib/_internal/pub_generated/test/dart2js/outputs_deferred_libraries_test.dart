// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:scheduled_test/scheduled_test.dart';

import '../descriptor.dart' as d;
import '../test_pub.dart';

const MAIN = """
import 'dart:async';

import 'a.dart' deferred as a;
import 'b.dart' deferred as b;

void main() {
  Future.wait([lazyA.loadLibrary(), lazyB.loadLibrary()]).then((_) {
    a.fn();
    b.fn();
  });
}
""";

const A = """
library a;

fn() => print("a");
""";

const B = """
library b;

fn() => print("b");
""";

main() {
  initConfig();
  integration("compiles deferred libraries to separate outputs", () {
    // Dart2js can take a long time to compile dart code, so we increase the
    // timeout to cope with that.
    currentSchedule.timeout *= 3;

    d.dir(
        appPath,
        [
            d.appPubspec(),
            d.dir(
                'web',
                [
                    d.file('main.dart', MAIN),
                    d.file('a.dart', A),
                    d.file('b.dart', B)])]).create();

    schedulePub(
        args: ["build"],
        output: new RegExp(r'Built 3 files to "build".'));

    d.dir(
        appPath,
        [
            d.dir(
                'build',
                [
                    d.dir(
                        'web',
                        [
                            d.matcherFile('main.dart.js', isNot(isEmpty)),
                            d.matcherFile('main.dart.js_1.part.js', isNot(isEmpty)),
                            d.matcherFile('main.dart.js_2.part.js', isNot(isEmpty)),])])]).validate();
  });
}
