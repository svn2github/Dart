// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.domain.analysis.hover;

import 'dart:async';

import 'package:analysis_server/src/computer/computer_hover.dart';
import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/protocol.dart';
import 'package:unittest/unittest.dart';

import 'analysis_abstract.dart';
import 'reflective_tests.dart';


main() {
  group('notification.hover', () {
    runReflectiveTests(AnalysisHoverTest);
  });
}


@ReflectiveTestCase()
class AnalysisHoverTest extends AbstractAnalysisTest {
  List<Hover> hovers;
  Hover hover;

  Future prepareHover(String search, then()) {
    int offset = findOffset(search);
    return prepareHoverAt(offset, then);
  }

  Future prepareHoverAt(int offset, then()) {
    return waitForTasksFinished().then((_) {
      Request request = new Request('0', ANALYSIS_GET_HOVER);
      request.setParameter(FILE, testFile);
      request.setParameter(OFFSET, offset);
      Response response = handleSuccessfulRequest(request);
      List<Map<String, Object>> hoverJsons = response.getResult(HOVERS);
      hovers = hoverJsons.map((json) {
        return new Hover.fromJson(json);
      }).toList();
      hover = hovers.isNotEmpty ? hovers.first : null;
      then();
    });
  }

  @override
  void setUp() {
    super.setUp();
    createProject();
  }

  test_dartDoc_clunky() {
    addTestFile('''
library my.library;
/**
 * doc aaa
 * doc bbb
 */
main() {
}
''');
    return prepareHover('main() {', () {
      expect(hover.dartDoc, '''doc aaa\ndoc bbb''');
    });
  }

  test_dartDoc_elegant() {
    addTestFile('''
library my.library;
/// doc aaa
/// doc bbb
main() {
}
''');
    return prepareHover('main() {', () {
      expect(hover.dartDoc, '''doc aaa\ndoc bbb''');
    });
  }

  test_expression_function() {
    addTestFile('''
library my.library;
/// doc aaa
/// doc bbb
List<String> fff(int a, String b) {
}
''');
    return prepareHover('fff(int a', () {
      // element
      expect(hover.containingLibraryName, 'my.library');
      expect(hover.containingLibraryPath, testFile);
      expect(hover.dartDoc, '''doc aaa\ndoc bbb''');
      expect(hover.elementDescription, 'fff(int a, String b) → List<String>');
      // types
      expect(hover.staticType, '(int, String) → List<String>');
      expect(hover.propagatedType, isNull);
      // no parameter
      expect(hover.parameter, isNull);
    });
  }

  test_expression_literal_noElement() {
    addTestFile('''
main() {
  foo(123);
}
foo(Object myParameter) {}
''');
    return prepareHover('123', () {
      // literal, no Element
      expect(hover.elementDescription, isNull);
      // types
      expect(hover.staticType, 'int');
      expect(hover.propagatedType, isNull);
      // parameter
      expect(hover.parameter, 'Object myParameter');
    });
  }

  test_expression_method() {
    addTestFile('''
library my.library;
class A {
  /// doc aaa
  /// doc bbb
  List<String> mmm(int a, String b) {
  }
}
''');
    return prepareHover('mmm(int a', () {
      // element
      expect(hover.containingLibraryName, 'my.library');
      expect(hover.containingLibraryPath, testFile);
      expect(hover.dartDoc, '''doc aaa\ndoc bbb''');
      expect(hover.elementDescription, 'A.mmm(int a, String b) → List<String>');
      // types
      expect(hover.staticType, '(int, String) → List<String>');
      expect(hover.propagatedType, isNull);
      // no parameter
      expect(hover.parameter, isNull);
    });
  }

  test_expression_syntheticGetter() {
    addTestFile('''
library my.library;
class A {
  /// doc aaa
  /// doc bbb
  String fff;
}
main(A a) {
  print(a.fff);
}
''');
    return prepareHover('fff);', () {
      // element
      expect(hover.containingLibraryName, 'my.library');
      expect(hover.containingLibraryPath, testFile);
      expect(hover.dartDoc, '''doc aaa\ndoc bbb''');
      expect(hover.elementDescription, 'String fff');
      // types
      expect(hover.staticType, 'String');
      expect(hover.propagatedType, isNull);
    });
  }

  test_expression_variable_hasPropagatedType() {
    addTestFile('''
library my.library;
main() {
  var vvv = 123;
  print(vvv);
}
''');
    return prepareHover('vvv);', () {
      // element
      expect(hover.containingLibraryName, 'my.library');
      expect(hover.containingLibraryPath, testFile);
      expect(hover.dartDoc, isNull);
      expect(hover.elementDescription, 'dynamic vvv');
      // types
      expect(hover.staticType, 'dynamic');
      expect(hover.propagatedType, 'int');
    });
  }
}
