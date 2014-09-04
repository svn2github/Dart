// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'package:unittest/unittest.dart';

import 'analysis/test_all.dart' as analysis_all;
import 'analysis_hover_test.dart' as analysis_hover_test;
import 'analysis_notification_highlights_test.dart' as analysis_notification_highlights_test;
import 'analysis_notification_navigation_test.dart' as analysis_notification_navigation_test;
import 'analysis_notification_occurrences_test.dart' as analysis_notification_occurrences_test;
import 'analysis_notification_outline_test.dart' as analysis_notification_outline_test;
import 'analysis_notification_overrides_test.dart' as analysis_notification_overrides_test;
import 'analysis_server_test.dart' as analysis_server_test;
import 'channel/test_all.dart' as channel_test;
import 'computer/test_all.dart' as computer_test_all;
import 'context_manager_test.dart' as context_manager_test;
import 'domain_analysis_test.dart' as domain_analysis_test;
import 'domain_completion_test.dart' as completion_test;
import 'domain_server_test.dart' as domain_server_test;
import 'edit/test_all.dart' as edit_all;
import 'operation/test_all.dart' as operation_test_all;
import 'package_map_provider_test.dart' as package_map_provider_test;
import 'protocol_test.dart' as protocol_test;
import 'search/test_all.dart' as search_all;
import 'services/test_all.dart' as services_all;
import 'socket_server_test.dart' as socket_server_test;

/**
 * Utility for manually running all tests.
 */
main() {
  groupSep = ' | ';
  group('analysis_server', () {
    analysis_all.main();
    analysis_hover_test.main();
    analysis_notification_highlights_test.main();
    analysis_notification_navigation_test.main();
    analysis_notification_occurrences_test.main();
    analysis_notification_outline_test.main();
    analysis_notification_overrides_test.main();
    analysis_server_test.main();
    channel_test.main();
    completion_test.main();
    computer_test_all.main();
    context_manager_test.main();
    domain_analysis_test.main();
    domain_server_test.main();
    edit_all.main();
    operation_test_all.main();
    package_map_provider_test.main();
    protocol_test.main();
    search_all.main();
    services_all.main();
    socket_server_test.main();
  });
}