// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.search.element_references;

import 'dart:async';

import 'package:analysis_server/src/computer/element.dart';
import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_server/src/search/search_domain.dart';
import 'package:analysis_server/src/search/search_result.dart';
import 'package:analysis_services/index/index.dart' show Index;
import 'package:analysis_services/index/local_memory_index.dart';
import 'package:analysis_testing/reflective_tests.dart';
import 'package:unittest/unittest.dart';

import '../analysis_abstract.dart';


main() {
  groupSep = ' | ';
  group('findElementReferences', () {
    runReflectiveTests(ElementReferencesTest);
  });
}


@ReflectiveTestCase()
class ElementReferencesTest extends AbstractAnalysisTest {
  String searchId;
  bool searchDone = false;
  List<SearchResult> results = <SearchResult>[];
  SearchResult result;

  void assertHasResult(SearchResultKind kind, String search, [int length]) {
    int offset = findOffset(search);
    if (length == null) {
      length = findIdentifierLength(search);
    }
    findResult(kind, testFile, offset, length, true);
  }

  void assertNoResult(SearchResultKind kind, String search, [int length]) {
    int offset = findOffset(search);
    if (length == null) {
      length = findIdentifierLength(search);
    }
    findResult(kind, testFile, offset, length, false);
  }

  @override
  Index createIndex() {
    return createLocalMemoryIndex();
  }

  fail_test_hierarchyMembers_field_explicit() {
    // TODO(scheglov) implement
    addTestFile('''
class A {
  int fff; // in A
}
class B extends A {
  int fff; // in B
}
class C extends B {
  int fff; // in C
}
main(A a, B b, C c) {
  a.fff = 10;
  b.fff = 20;
  c.fff = 30;
}
''');
    return findElementReferences('fff; // in B', false).then((_) {
      assertHasResult(SearchResultKind.DECLARATION, 'fff; // in A');
      assertHasResult(SearchResultKind.DECLARATION, 'fff; // in B');
      assertHasResult(SearchResultKind.DECLARATION, 'fff; // in C');
      assertHasResult(SearchResultKind.WRITE, 'fff = 10;');
      assertHasResult(SearchResultKind.WRITE, 'fff = 20;');
      assertHasResult(SearchResultKind.WRITE, 'fff = 30;');
    });
  }

  fail_test_hierarchyMembers_method() {
    // TODO(scheglov) implement
    addTestFile('''
class A {
  mmm() {} // in A
}
class B extends A {
  mmm() {} // in B
}
class C extends B {
  mmm() {} // in C
}
main(A a, B b, C c) {
  a.mmm(10);
  b.mmm(20);
  c.mmm(30);
}
''');
    return findElementReferences('mmm() {} // in B', false).then((_) {
      assertHasResult(SearchResultKind.INVOCATION, 'mmm(10)');
      assertHasResult(SearchResultKind.INVOCATION, 'mmm(20)');
      assertHasResult(SearchResultKind.INVOCATION, 'mmm(30)');
    });
  }

  Future findElementReferences(String search, bool includePotential) {
    int offset = findOffset(search);
    return waitForTasksFinished().then((_) {
      Request request = new Request('0', SEARCH_FIND_ELEMENT_REFERENCES);
      request.setParameter(FILE, testFile);
      request.setParameter(OFFSET, offset);
      request.setParameter(INCLUDE_POTENTIAL, includePotential);
      Response response = handleSuccessfulRequest(request);
      searchId = response.getResult(ID);
      results.clear();
      return _waitForSearchResults();
    });
  }

  void findResult(SearchResultKind kind, String file, int offset, int length,
      bool expected) {
    for (SearchResult result in results) {
      Location location = result.location;
      if (result.kind == kind &&
          location.file == file &&
          location.offset == offset &&
          location.length == length) {
        if (!expected) {
          fail('Unexpected result $result in\n' + results.join('\n'));
        }
        this.result = result;
        return;
      }
    }
    if (expected) {
      fail(
          'Not found: "search" kind=$kind offset=$offset length=$length\nin\n' +
              results.join('\n'));
    }
  }

  String getPathString(List<Element> path) {
    return path.map((Element element) {
      return '${element.kind} ${element.name}';
    }).join('\n');
  }

  void processNotification(Notification notification) {
    if (notification.event == SEARCH_RESULTS) {
      String id = notification.getParameter(ID);
      if (id == searchId) {
        for (Map<String, Object> json in notification.getParameter(RESULTS)) {
          results.add(new SearchResult.fromJson(json));
        }
        searchDone = notification.getParameter(LAST);
      }
    }
  }

  @override
  void setUp() {
    super.setUp();
    createProject();
    handler = new SearchDomainHandler(server);
  }

  test_constructor_named() {
    addTestFile('''
class A {
  A.named(p);
}
main() {
  new A.named(1);
  new A.named(2);
}
''');
    return findElementReferences('named(p)', false).then((_) {
      assertHasResult(SearchResultKind.REFERENCE, '.named(1)', 6);
      assertHasResult(SearchResultKind.REFERENCE, '.named(2)', 6);
    });
  }

  test_constructor_unamed() {
    addTestFile('''
class A {
  A(p);
}
main() {
  new A(1);
  new A(2);
}
''');
    return findElementReferences('A(p)', false).then((_) {
      assertHasResult(SearchResultKind.REFERENCE, '(1)', 0);
      assertHasResult(SearchResultKind.REFERENCE, '(2)', 0);
    });
  }

  test_field_explicit() {
    addTestFile('''
class A {
  var fff; // declaration
  A(this.fff); // in constructor
  m() {
    fff = 2;
    fff += 3;
    print(fff); // in m()
    fff(); // in m()
  }
}
main(A a) {
  a.fff = 20;
  a.fff += 30;
  print(a.fff); // in main()
  a.fff(); // in main()
}
''');
    return findElementReferences('fff; // declaration', false).then((_) {
      expect(results, hasLength(10));
      assertHasResult(SearchResultKind.DECLARATION, 'fff; // declaration');
      assertHasResult(SearchResultKind.REFERENCE, 'fff); // in constructor');
      // m()
      assertHasResult(SearchResultKind.WRITE, 'fff = 2;');
      assertHasResult(SearchResultKind.WRITE, 'fff += 3;');
      assertHasResult(SearchResultKind.READ, 'fff); // in m()');
      assertHasResult(SearchResultKind.INVOCATION, 'fff(); // in m()');
      // main()
      assertHasResult(SearchResultKind.WRITE, 'fff = 20;');
      assertHasResult(SearchResultKind.WRITE, 'fff += 30;');
      assertHasResult(SearchResultKind.READ, 'fff); // in main()');
      assertHasResult(SearchResultKind.INVOCATION, 'fff(); // in main()');
    });
  }

  test_field_implicit() {
    addTestFile('''
class A {
  var  get fff => null;
  void set fff(x) {}
  m() {
    print(fff); // in m()
    fff = 1;
  }
}
main(A a) {
  print(a.fff); // in main()
  a.fff = 10;
}
''');
    var forGetter = findElementReferences('fff =>', false).then((_) {
      expect(results, hasLength(4));
      assertHasResult(SearchResultKind.READ, 'fff); // in m()');
      assertHasResult(SearchResultKind.WRITE, 'fff = 1;');
      assertHasResult(SearchResultKind.READ, 'fff); // in main()');
      assertHasResult(SearchResultKind.WRITE, 'fff = 10;');
    });
    var forSetter = findElementReferences('fff(x) {}', false).then((_) {
      expect(results, hasLength(4));
      assertHasResult(SearchResultKind.READ, 'fff); // in m()');
      assertHasResult(SearchResultKind.WRITE, 'fff = 1;');
      assertHasResult(SearchResultKind.READ, 'fff); // in main()');
      assertHasResult(SearchResultKind.WRITE, 'fff = 10;');
    });
    return Future.wait([forGetter, forSetter]);
  }

  test_field_inFormalParameter() {
    addTestFile('''
class A {
  var fff; // declaration
  A(this.fff); // in constructor
  m() {
    fff = 2;
    print(fff); // in m()
  }
}
''');
    return findElementReferences('fff); // in constructor', false).then((_) {
      expect(results, hasLength(4));
      assertHasResult(SearchResultKind.DECLARATION, 'fff; // declaration');
      assertHasResult(SearchResultKind.REFERENCE, 'fff); // in constructor');
      assertHasResult(SearchResultKind.WRITE, 'fff = 2;');
      assertHasResult(SearchResultKind.READ, 'fff); // in m()');
    });
  }

  test_function() {
    addTestFile('''
fff(p) {}
main() {
  fff(1);
  print(fff);
}
''');
    return findElementReferences('fff(p) {}', false).then((_) {
      expect(results, hasLength(2));
      assertHasResult(SearchResultKind.INVOCATION, 'fff(1)');
      assertHasResult(SearchResultKind.REFERENCE, 'fff);');
    });
  }

  test_localVariable() {
    addTestFile('''
main() {
  var vvv = 1;
  print(vvv);
  vvv += 3;
  vvv = 2;
  vvv();
}
''');
    return findElementReferences('vvv = 1', false).then((_) {
      expect(results, hasLength(5));
      assertHasResult(SearchResultKind.DECLARATION, 'vvv = 1');
      assertHasResult(SearchResultKind.READ, 'vvv);');
      assertHasResult(SearchResultKind.READ_WRITE, 'vvv += 3');
      assertHasResult(SearchResultKind.WRITE, 'vvv = 2');
      assertHasResult(SearchResultKind.INVOCATION, 'vvv();');
    });
  }

  test_method() {
    addTestFile('''
class A {
  mmm(p) {}
  m() {
    mmm(1);
    print(mmm); // in m()
  }
}
main(A a) {
  a.mmm(10);
  print(a.mmm); // in main()
}
''');
    return findElementReferences('mmm(p) {}', false).then((_) {
      expect(results, hasLength(4));
      assertHasResult(SearchResultKind.INVOCATION, 'mmm(1);');
      assertHasResult(SearchResultKind.REFERENCE, 'mmm); // in m()');
      assertHasResult(SearchResultKind.INVOCATION, 'mmm(10);');
      assertHasResult(SearchResultKind.REFERENCE, 'mmm); // in main()');
    });
  }

  test_method_propagatedType() {
    addTestFile('''
class A {
  mmm(p) {}
}
main() {
  var a = new A();
  a.mmm(10);
  print(a.mmm);
}
''');
    return findElementReferences('mmm(p) {}', false).then((_) {
      expect(results, hasLength(2));
      assertHasResult(SearchResultKind.INVOCATION, 'mmm(10);');
      assertHasResult(SearchResultKind.REFERENCE, 'mmm);');
    });
  }

  test_oneUnit_twoLibraries() {
    var pathA = '/project/bin/libA.dart';
    var pathB = '/project/bin/libB.dart';
    var codeA = '''
library lib;
part 'test.dart';
main() {
  fff(1);
}
''';
    var codeB = '''
library lib;
part 'test.dart';
main() {
  fff(2);
}
''';
    addFile(pathA, codeA);
    addFile(pathB, codeB);
    addTestFile('''
part of lib;
fff(p) {}
''');
    return findElementReferences('fff(p) {}', false).then((_) {
      expect(results, hasLength(2));
      findResult(
          SearchResultKind.INVOCATION,
          pathA,
          codeA.indexOf('fff(1)'),
          3,
          true);
      findResult(
          SearchResultKind.INVOCATION,
          pathB,
          codeB.indexOf('fff(2)'),
          3,
          true);
    });
  }

  test_oneUnit_zeroLibraries() {
    addTestFile('''
part of lib;
fff(p) {}
main() {
  fff(10);
}
''');
    return findElementReferences('fff(p) {}', false).then((_) {
      expect(results, isEmpty);
    });
  }

  test_parameter() {
    addTestFile('''
main(ppp) {
  print(ppp);
  ppp += 3;
  ppp = 2;
  ppp();
}
''');
    return findElementReferences('ppp) {', false).then((_) {
      expect(results, hasLength(5));
      assertHasResult(SearchResultKind.DECLARATION, 'ppp) {');
      assertHasResult(SearchResultKind.READ, 'ppp);');
      assertHasResult(SearchResultKind.READ_WRITE, 'ppp += 3');
      assertHasResult(SearchResultKind.WRITE, 'ppp = 2');
      assertHasResult(SearchResultKind.INVOCATION, 'ppp();');
    });
  }

  test_path_inConstructor_named() {
    addTestFile('''
library my_lib;
class A {}
class B {
  B.named() {
    A a = null;
  }
}
''');
    return findElementReferences('A {}', false).then((_) {
      assertHasResult(SearchResultKind.REFERENCE, 'A a = null;');
      expect(getPathString(result.path), '''
LOCAL_VARIABLE a
CONSTRUCTOR named
CLASS B
COMPILATION_UNIT test.dart
LIBRARY my_lib''');
    });
  }

  test_path_inConstructor_unnamed() {
    addTestFile('''
library my_lib;
class A {}
class B {
  B() {
    A a = null;
  }
}
''');
    return findElementReferences('A {}', false).then((_) {
      assertHasResult(SearchResultKind.REFERENCE, 'A a = null;');
      expect(getPathString(result.path), '''
LOCAL_VARIABLE a
CONSTRUCTOR 
CLASS B
COMPILATION_UNIT test.dart
LIBRARY my_lib''');
    });
  }

  test_path_inFunction() {
    addTestFile('''
library my_lib;
class A {}
main() {
  A a = null;
}
''');
    return findElementReferences('A {}', false).then((_) {
      assertHasResult(SearchResultKind.REFERENCE, 'A a = null;');
      expect(getPathString(result.path), '''
LOCAL_VARIABLE a
FUNCTION main
COMPILATION_UNIT test.dart
LIBRARY my_lib''');
    });
  }

  test_potential_disabled() {
    addTestFile('''
class A {
  test(p) {}
}
main(A a, p) {
  a.test(1);
  p.test(2);
}
''');
    return findElementReferences('test(p) {}', false).then((_) {
      assertHasResult(SearchResultKind.INVOCATION, 'test(1);');
      assertNoResult(SearchResultKind.INVOCATION, 'test(2);');
    });
  }

  test_potential_field() {
    addTestFile('''
class A {
  test; // declaration
}
main(A a, p) {
  a.test = 1;
  p.test = 2;
  print(p.test); // p
}
''');
    return findElementReferences('test; // declaration', true).then((_) {
      {
        assertHasResult(SearchResultKind.WRITE, 'test = 1;');
        expect(result.isPotential, isFalse);
      }
      {
        assertHasResult(SearchResultKind.WRITE, 'test = 2;');
        expect(result.isPotential, isTrue);
      }
      {
        assertHasResult(SearchResultKind.READ, 'test); // p');
        expect(result.isPotential, isTrue);
      }
    });
  }

  test_potential_method() {
    addTestFile('''
class A {
  test(p) {}
}
main(A a, p) {
  a.test(1);
  p.test(2);
}
''');
    return findElementReferences('test(p) {}', true).then((_) {
      {
        assertHasResult(SearchResultKind.INVOCATION, 'test(1);');
        expect(result.isPotential, isFalse);
      }
      {
        assertHasResult(SearchResultKind.INVOCATION, 'test(2);');
        expect(result.isPotential, isTrue);
      }
    });
  }

  test_topLevelVariable_explicit() {
    addTestFile('''
var vvv = 1;
main() {
  print(vvv);
  vvv += 3;
  vvv = 2;
  vvv();
}
''');
    return findElementReferences('vvv = 1', false).then((_) {
      expect(results, hasLength(5));
      assertHasResult(SearchResultKind.DECLARATION, 'vvv = 1;');
      assertHasResult(SearchResultKind.READ, 'vvv);');
      assertHasResult(SearchResultKind.WRITE, 'vvv += 3');
      assertHasResult(SearchResultKind.WRITE, 'vvv = 2');
      assertHasResult(SearchResultKind.INVOCATION, 'vvv();');
    });
  }

  test_topLevelVariable_implicit() {
    addTestFile('''
get vvv => null;
set vvv(x) {}
main() {
  print(vvv);
  vvv = 1;
}
''');
    var forGetter = findElementReferences('vvv =>', false).then((_) {
      expect(results, hasLength(2));
      assertHasResult(SearchResultKind.READ, 'vvv);');
      assertHasResult(SearchResultKind.WRITE, 'vvv = 1;');
    });
    var forSetter = findElementReferences('vvv(x) {}', false).then((_) {
      expect(results, hasLength(2));
      assertHasResult(SearchResultKind.READ, 'vvv);');
      assertHasResult(SearchResultKind.WRITE, 'vvv = 1;');
    });
    return Future.wait([forGetter, forSetter]);
  }

  test_typeReference_class() {
    addTestFile('''
main() {
  int a = 1;
  int b = 2;
}
''');
    return findElementReferences('int a', false).then((_) {
      assertHasResult(SearchResultKind.REFERENCE, 'int a');
      assertHasResult(SearchResultKind.REFERENCE, 'int b');
    });
  }

  test_typeReference_functionType() {
    addTestFile('''
typedef F();
main(F f) {
}
''');
    return findElementReferences('F()', false).then((_) {
      expect(results, hasLength(1));
      assertHasResult(SearchResultKind.REFERENCE, 'F f');
    });
  }

  test_typeReference_typeVariable() {
    addTestFile('''
class A<T> {
  T f;
  T m() => null;
}
''');
    return findElementReferences('T> {', false).then((_) {
      expect(results, hasLength(2));
      assertHasResult(SearchResultKind.REFERENCE, 'T f;');
      assertHasResult(SearchResultKind.REFERENCE, 'T m()');
    });
  }

  Future _waitForSearchResults() {
    if (searchDone) {
      return new Future.value();
    }
    return new Future.delayed(Duration.ZERO, _waitForSearchResults);
  }
}
