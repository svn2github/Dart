// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.services.completion.toplevel;

import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_server/src/services/completion/imported_computer.dart';
import 'package:unittest/unittest.dart';

import '../../reflective_tests.dart';
import 'completion_test_util.dart';

main() {
  groupSep = ' | ';
  runReflectiveTests(ImportedTypeComputerTest);
}

@ReflectiveTestCase()
class ImportedTypeComputerTest extends AbstractCompletionTest {

  @override
  void setUp() {
    super.setUp();
    computer = new ImportedComputer();
  }

  test_Block_class() {
    // Block  BlockFunctionBody  MethodDeclaration  ClassDeclaration
    addSource('/testAB.dart', '''
      class A {int x;}
      class _B { }''');
    addSource('/testCD.dart', '''
      class C { }
      class D { }''');
    addSource('/testEEF.dart', '''
      class EE { }
      class F { }''');
    addSource('/testG.dart', 'class G { }');
    addSource('/testH.dart', 'class H { }'); // not imported
    addTestSource('''
      import "/testAB.dart";
      import "/testCD.dart" hide D;
      import "/testEEF.dart" show EE;
      import "/testG.dart" as g;
      class X {foo(){^}}''');
    // pass true for full analysis to pick up unimported source
    return computeFull(true).then((_) {
      assertSuggestClass('A');
      assertNotSuggested('x');
      assertNotSuggested('_B');
      assertSuggestClass('C');
      assertNotSuggested('D');
      assertSuggestClass('EE');
      assertNotSuggested('F');
      assertSuggestLibraryPrefix('g');
      assertNotSuggested('G');
      assertSuggestClass('H', CompletionRelevance.LOW);
      // Should not suggest compilation unit elements
      // which are returned by the LocalComputer
      assertNotSuggested('X');
      assertSuggestClass('Object');
      // TODO (danrubel) suggest HtmlElement as low relevance
      assertNotSuggested('HtmlElement');
    });
  }

  test_Block_function() {
    addSource('/testA.dart', '''
      export "dart:math" hide sin;
      @deprecated A() {int x;}
      _B() {}''');
    addTestSource('''
      import "/testA.dart";
      class X {foo(){^}}''');
    return computeFull().then((_) {
      assertSuggestFunction('A', null, true);
      assertNotSuggested('x');
      assertNotSuggested('_B');
      // TODO (danrubel) should suggest exported elements from imported lib
      //assertSuggestFunction('cos', 'num', false);
      assertNotSuggested('cos');
      assertNotSuggested('sin');
      // Should not suggest compilation unit elements
      // which are returned by the LocalComputer
      assertNotSuggested('X');
    });
  }

  test_Block_topLevelVar() {
    // Block  BlockFunctionBody  MethodDeclaration
    addSource('/testA.dart', '''
      String T1;
      var _T2;''');
    addSource('/testB.dart', /* not imported */ '''
      int T3;
      var _T4;''');
    addTestSource('''
      import "/testA.dart";
      class C {foo(){^}}''');
    // pass true for full analysis to pick up unimported source
    return computeFull(true).then((_) {
      assertSuggestTopLevelVar('T1', 'String');
      assertNotSuggested('_T2');
      assertSuggestTopLevelVar('T3', 'int', CompletionRelevance.LOW);
      assertNotSuggested('_T4');
    });
  }

  test_ExpressionStatement_class() {
    // SimpleIdentifier  ExpressionStatement  Block
    addSource('/testA.dart', '''
      class A {int x;}
      class _B { }''');
    addTestSource('''
      import "/testA.dart";
      class C {foo(){O^}}''');
    return computeFull().then((_) {
      assertSuggestClass('A');
      assertNotSuggested('x');
      assertNotSuggested('_B');
      // Should not suggest compilation unit elements
      // which are returned by the LocalComputer
      assertNotSuggested('C');
    });
  }

  test_ExpressionStatement_name() {
    // ExpressionStatement  Block  BlockFunctionBody  MethodDeclaration
    addSource('/testA.dart', '''
      B T1;
      class B{}''');
    addTestSource('''
      import "/testA.dart";
      class C {a() {C ^}}''');
    return computeFull().then((_) {
      assertNotSuggested('T1');
    });
  }

  test_FieldDeclaration_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // FieldDeclaration
    addSource('/testA.dart', 'class A { }');
    addTestSource('''
      import "/testA.dart";
      class C {A ^}''');
    return computeFull().then((_) {
      assertNotSuggested('A');
    });
  }

  test_FieldDeclaration_name_varType() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // FieldDeclaration
    addSource('/testA.dart', 'class A { }');
    addTestSource('''
      import "/testA.dart";
      class C {var ^}''');
    return computeFull().then((_) {
      assertNotSuggested('A');
    });
  }

  test_HideCombinator_class() {
    // SimpleIdentifier  HideCombinator  ImportDirective
    addSource('/testAB.dart', '''
      library libAB;
      part '/partAB.dart';
      class A { }
      class B { }''');
    addSource('/partAB.dart', '''
      part of libAB;
      class PB { }''');
    addSource('/testCD.dart', '''
      class C { }
      class D { }''');
    addTestSource('''
      import "/testAB.dart" hide ^;
      import "/testCD.dart";
      class X {}''');
    return computeFull().then((_) {
      assertSuggestClass('A');
      assertSuggestClass('B');
      assertSuggestClass('PB');
      assertNotSuggested('C');
      assertNotSuggested('D');
      assertNotSuggested('Object');
    });
  }

  test_ImportDirective_dart() {
    // SimpleStringLiteral  ImportDirective
    addTestSource('''
      import "dart^";
      main() {}''');
    return computeFull().then((_) {
      assertNotSuggested('Object');
    });
  }

  test_ShowCombinator_class() {
    // SimpleIdentifier  ShowCombinator  ImportDirective
    addSource('/testAB.dart', '''
      class A { }
      class B { }''');
    addSource('/testCD.dart', '''
      class C { }
      class D { }''');
    addTestSource('''
      import "/testAB.dart" show ^;
      import "/testCD.dart";
      class X {}''');
    return computeFull().then((_) {
      assertSuggestClass('A');
      assertSuggestClass('B');
      assertNotSuggested('C');
      assertNotSuggested('D');
      assertNotSuggested('Object');
    });
  }

  test_TopLevelVariableDeclaration_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // TopLevelVariableDeclaration
    addSource('/testA.dart', 'class B { };');
    addTestSource('''
      import "/testA.dart";
      class C {} B ^''');
    return computeFull().then((_) {
      assertNotSuggested('B');
    });
  }

  test_TopLevelVariableDeclaration_name_untyped() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // TopLevelVariableDeclaration
    addSource('/testA.dart', 'class B { };');
    addTestSource('''
      import "/testA.dart";
      class C {} var ^''');
    return computeFull().then((_) {
      assertNotSuggested('B');
    });
  }

  test_VariableDeclaration_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // VariableDeclarationStatement  Block
    addSource('/testA.dart', 'var T1;');
    addTestSource('''
      import "/testA.dart";
      class C {a() {var ^}}''');
    return computeFull().then((_) {
      assertNotSuggested('T1');
    });
  }

  // TODO (danrubel) implement
  xtest_InstanceCreationExpression() {
    // SimpleIdentifier  TypeName  ConstructorName  InstanceCreationExpression
    addSource('/testA.dart', '''
      class A {int x;}
      B() { }''');
    addTestSource('''
      import "/testA.dart";
      class C {foo(){new ^}}''');
    return computeFull().then((_) {
      assertSuggestClass('A');
      assertNotSuggested('x');
      assertNotSuggested('B');
      // Should not suggest compilation unit elements
      // which are returned by the LocalComputer
      assertNotSuggested('C');
    });
  }

  // TODO (danrubel) implement
  xtest_VariableDeclarationStatement_RHS() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // VariableDeclarationStatement
    addSource('/testA.dart', 'class A {int x;} class _B { }');
    addTestSource('''
      import "/testA.dart";
      class C {foo(){var e = ^}}''');
    return computeFull().then((_) {
      assertSuggestClass('A');
      assertNotSuggested('x');
      assertNotSuggested('_B');
      // Should not suggest compilation unit elements
      // which are returned by the LocalComputer
      assertNotSuggested('C');
    });
  }
}
