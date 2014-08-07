// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library allocations_test;

import 'test_helper.dart';
import 'package:observatory/service_io.dart';
import 'package:unittest/unittest.dart';

import 'dart:io';

main() {
  String script = 'allocations_script.dart';
  var process = new TestLauncher(script);
  process.launch().then((port) {
    String addr = 'ws://localhost:$port/ws';
    new WebSocketVM(new WebSocketVMTarget(addr)).get('vm')
        .then((VM vm) => vm.isolates.first.load())
        .then((Isolate isolate) => isolate.rootLib.load())
        .then((Library lib) {
          expect(lib.url.endsWith(script), isTrue);
          return lib.classes.first.load();
        })
        .then((Class fooClass) {
          expect(fooClass.name, equals('Foo'));
          expect(fooClass.newSpace.accumulated.instances +
                 fooClass.oldSpace.accumulated.instances, equals(3));
          exit(0);
        });
  });
}
