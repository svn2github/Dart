// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:io';

import "package:expect/expect.dart";
import "package:async_helper/async_helper.dart";

void main() {
  asyncStart();
  Process.run(Platform.executable,
              [Uri.parse(Platform.script)
                   .resolve('print_sync_script.dart').toString()])
      .then((out) {
        asyncEnd();
        Expect.equals(1002, out.stdout.split('\n').length);
      });
}
