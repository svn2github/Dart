// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import '../time_server.dart' as time;

// @static-clean

/**
 * This test exists to ensure that the time sample compiles without errors.
 */
void main() {
  // Reference the sunflower library so that the import isn't marked as unused.
  String s = time.HOST;
  s = null;
  print(s);
}
