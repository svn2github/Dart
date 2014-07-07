// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.domain.analysis.notification.occurrences;

import 'dart:async';

import 'package:analysis_server/src/analysis_server.dart';
import 'package:analysis_server/src/computer/computer_occurrences.dart';
import 'package:analysis_server/src/computer/element.dart';
import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_testing/reflective_tests.dart';
import 'package:unittest/unittest.dart';

import 'analysis_abstract.dart';


main() {
  group('notification.occurrences', () {
    runReflectiveTests(AnalysisNotificationOccurrencesTest);
  });
}


@ReflectiveTestCase()
class AnalysisNotificationOccurrencesTest extends AbstractAnalysisTest {
  List<Occurrences> occurrencesList;
  Occurrences testOccurences;

  /**
   * Asserts that there is an offset of [search] in [testOccurences].
   */
  void assertHasOffset(String search) {
    int offset = findOffset(search);
    expect(testOccurences.offsets, contains(offset));
  }

  /**
   * Validates that there is a region at the offset of [search] in [testFile].
   * If [length] is not specified explicitly, then length of an identifier
   * from [search] is used.
   */
  void assertHasRegion(String search, [int length = -1]) {
    int offset = findOffset(search);
    if (length == -1) {
      length = findIdentifierLength(search);
    }
    findRegion(offset, length, true);
  }

  /**
   * Finds an [Occurrences] with the given [offset] and [length].
   *
   * If [exists] is `true`, then fails if such [Occurrences] does not exist.
   * Otherwise remembers this it into [testOccurences].
   *
   * If [exists] is `false`, then fails if such [Occurrences] exists.
   */
  void findRegion(int offset, int length, [bool exists]) {
    for (Occurrences occurrences in occurrencesList) {
      if (occurrences.length != length) {
        continue;
      }
      for (int occurrenceOffset in occurrences.offsets) {
        if (occurrenceOffset == offset) {
          if (exists == false) {
            fail('Not expected to find (offset=$offset; length=$length) in\n'
                '${occurrencesList.join('\n')}');
          }
          testOccurences = occurrences;
          return;
        }
      }
    }
    if (exists == true) {
      fail('Expected to find (offset=$offset; length=$length) in\n'
          '${occurrencesList.join('\n')}');
    }
  }

  Future prepareOccurrences(then()) {
    addAnalysisSubscription(AnalysisService.OCCURRENCES, testFile);
    return waitForTasksFinished().then((_) {
      then();
    });
  }

  void processNotification(Notification notification) {
    if (notification.event == ANALYSIS_OCCURRENCES) {
      String file = notification.getParameter(FILE);
      if (file == testFile) {
        occurrencesList = <Occurrences>[];
        List<Map<String, Object>> jsonList = notification.getParameter(
            OCCURRENCES);
        for (Map<String, Object> json in jsonList) {
          occurrencesList.add(new Occurrences.fromJson(json));
        }
      }
    }
  }

  @override
  void setUp() {
    super.setUp();
    createProject();
  }

  test_afterAnalysis() {
    addTestFile('''
main() {
  var vvv = 42;
  print(vvv);
}
''');
    return waitForTasksFinished().then((_) {
      return prepareOccurrences(() {
        assertHasRegion('vvv =');
        expect(testOccurences.element.kind, ElementKind.LOCAL_VARIABLE);
        expect(testOccurences.element.name, 'vvv');
        assertHasOffset('vvv = 42');
        assertHasOffset('vvv);');
      });
    });
  }

  test_classType() {
    addTestFile('''
main() {
  int a = 1;
  int b = 2;
  int c = 3;
}
int VVV = 4;
''');
    return prepareOccurrences(() {
      assertHasRegion('int a');
      expect(testOccurences.element.kind, ElementKind.CLASS);
      expect(testOccurences.element.name, 'int');
      assertHasOffset('int a');
      assertHasOffset('int b');
      assertHasOffset('int c');
      assertHasOffset('int VVV');
    });
  }

  test_localVariable() {
    addTestFile('''
main() {
  var vvv = 42;
  vvv += 5;
  print(vvv);
}
''');
    return prepareOccurrences(() {
      assertHasRegion('vvv =');
      expect(testOccurences.element.kind, ElementKind.LOCAL_VARIABLE);
      expect(testOccurences.element.name, 'vvv');
      assertHasOffset('vvv = 42');
      assertHasOffset('vvv += 5');
      assertHasOffset('vvv);');
    });
  }
}
