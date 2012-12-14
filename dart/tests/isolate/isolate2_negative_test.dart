// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Dart test program for testing that exceptions in other isolates bring down
// the program.

library isolate2_negative_test;
import 'dart:isolate';

void entry() {
  throw "foo";
}

main() {
  SendPort port = spawnFunction(entry);
  // TODO(ngeoffray): Remove this code and update dart2js to wait for
  // all ports to be closed.
  // Make sure the main thread stays alive so that the spawned isolate
  // can run.
  port.receive((msg) {});
}
