// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "package:expect/expect.dart";

main() {
  Expect.isNull(const String.fromEnvironment('NOT_FOUND'));
  Expect.equals('x',
                const String.fromEnvironment('NOT_FOUND', defaultValue: 'x'));
}
