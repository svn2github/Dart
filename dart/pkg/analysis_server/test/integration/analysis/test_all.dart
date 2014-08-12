// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.integration.analysis.all;

import 'package:unittest/unittest.dart';

import 'error_test.dart' as error_test;
import 'get_errors_after_analysis_test.dart' as get_errors_after_analysis_test;
import 'get_errors_before_analysis_test.dart' as get_errors_before_analysis_test;
import 'get_hover_test.dart' as get_hover_test;
import 'update_content_test.dart' as update_content_test;

/**
 * Utility for manually running all integration tests.
 */
main() {
  groupSep = ' | ';
  group('analysis', () {
    error_test.main();
    get_errors_after_analysis_test.main();
    get_errors_before_analysis_test.main();
    get_hover_test.main();
    update_content_test.main();
  });
}