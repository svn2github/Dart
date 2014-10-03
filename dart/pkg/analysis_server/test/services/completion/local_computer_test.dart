// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.services.completion.dart.local;

import 'package:analysis_server/src/services/completion/local_computer.dart';
import 'package:unittest/unittest.dart';

import '../../reflective_tests.dart';
import 'completion_test_util.dart';

main() {
  groupSep = ' | ';
  runReflectiveTests(LocalComputerTest);
}

@ReflectiveTestCase()
class LocalComputerTest extends AbstractCompletionTest {

  @override
  void setUp() {
    super.setUp();
    computer = new LocalComputer();
  }

  test_BinaryExpression_LHS() {
    // SimpleIdentifier  BinaryExpression  VariableDeclaration
    // VariableDeclarationList  VariableDeclarationStatement
    addTestSource('main() {int a = 1, b = ^ + 2;}');
    expect(computeFast(), isTrue);
    assertSuggestLocalVariable('a', 'int');
    assertNotSuggested('b');
  }

  test_BinaryExpression_RHS() {
    // SimpleIdentifier  BinaryExpression  VariableDeclaration
    // VariableDeclarationList  VariableDeclarationStatement
    addTestSource('main() {int a = 1, b = 2 + ^;}');
    expect(computeFast(), isTrue);
    assertSuggestLocalVariable('a', 'int');
    assertNotSuggested('b');
  }

  test_Block() {
    // Block  BlockFunctionBody
    addTestSource('class A {a() {var f; {var x;} ^ var g;}}');
    expect(computeFast(), isTrue);
    assertSuggestClass('A');
    assertSuggestLocalVariable('f', null);
    assertNotSuggested('g');
    assertNotSuggested('x');
  }

  test_CatchClause_typed() {
    // Block  CatchClause  TryStatement
    addTestSource('class A {a() {try{} on E catch (e) {^}}}');
    expect(computeFast(), isTrue);
    assertSuggestParameter('e', 'E');
  }

  test_CatchClause_untyped() {
    // Block  CatchClause  TryStatement
    addTestSource('class A {a() {try{} catch (e, s) {^}}}');
    expect(computeFast(), isTrue);
    assertSuggestParameter('e', null);
    assertSuggestParameter('s', 'StackTrace');
  }

  test_ClassDeclaration_body() {
    // ClassDeclaration  CompilationUnit
    addTestSource( //
    'import "boo.dart" as x;' //
    ' @deprecated class A {^}' //
    ' class _B {}' //
    ' A T;');
    expect(computeFast(), isTrue);
    var a = assertSuggestClass('A');
    expect(a.element.isDeprecated, isTrue);
    expect(a.element.isPrivate, isFalse);
    var b = assertSuggestClass('_B');
    expect(b.element.isDeprecated, isFalse);
    expect(b.element.isPrivate, isTrue);
    assertSuggestTopLevelVar('T', 'A');
    assertSuggestLibraryPrefix('x');
  }

  test_ExpressionStatement_name() {
    // ExpressionStatement  Block
    addTestSource('class A {a() {var f; A ^}}');
    expect(computeFast(), isTrue);
    assertNotSuggested('A');
    assertNotSuggested('a');
    assertNotSuggested('f');
  }

  test_FieldDeclaration_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // FieldDeclaration
    addTestSource('class A {B ^}}');
    expect(computeFast(), isTrue);
    assertNotSuggested('A');
  }

  test_ForEachStatement_body_typed() {
    // Block  ForEachStatement
    addTestSource('main(args) {for (int foo in bar) {^}}');
    expect(computeFast(), isTrue);
    assertSuggestLocalVariable('foo', 'int');
  }

  test_ForEachStatement_body_untyped() {
    // Block  ForEachStatement
    addTestSource('main(args) {for (foo in bar) {^}}');
    expect(computeFast(), isTrue);
    assertSuggestLocalVariable('foo', null);
  }

  test_ForStatement_body() {
    // Block  ForStatement
    addTestSource('main(args) {for (int i; i < 10; ++i) {^}}');
    expect(computeFast(), isTrue);
    assertSuggestLocalVariable('i', 'int');
  }

  test_FunctionExpression_body_function() {
    // Block  BlockFunctionBody  FunctionExpression
    addTestSource('String foo(List args) {x.then((R b) {^});}');
    expect(computeFast(), isTrue);
    var f = assertSuggestFunction('foo', 'String', false);
    expect(f.element.isPrivate, isFalse);
    assertSuggestParameter('args', 'List');
    assertSuggestParameter('b', 'R');
  }

  test_InstanceCreationExpression() {
    // SimpleIdentifier  TypeName  ConstructorName  InstanceCreationExpression
    addTestSource('class A {a() {var f; {var x;} new ^}} class B { }');
    expect(computeFast(), isTrue);
    assertSuggestClass('A');
    assertSuggestClass('B');
    // TODO (danrubel) should only suggest types
    //assertNotSuggested('a');
    assertSuggestMethod('a', 'A', null);
    //assertNotSuggested('f');
    assertSuggestLocalVariable('f', null);
    assertNotSuggested('x');
  }

  test_MethodDeclaration_body_getters() {
    // Block  BlockFunctionBody  MethodDeclaration
    addTestSource('class A {@deprecated X get f => 0; Z a() {^} get _g => 1;}');
    expect(computeFast(), isTrue);
    var a = assertSuggestMethod('a', 'A', 'Z');
    expect(a.element.isDeprecated, isFalse);
    expect(a.element.isPrivate, isFalse);
    var f = assertSuggestGetter('f', 'X');
    expect(f.element.isDeprecated, isTrue);
    expect(f.element.isPrivate, isFalse);
    var g = assertSuggestGetter('_g', null);
    expect(g.element.isDeprecated, isFalse);
    expect(g.element.isPrivate, isTrue);
  }

  test_MethodDeclaration_members() {
    // Block  BlockFunctionBody  MethodDeclaration
    addTestSource('class A {@deprecated X f; Z _a() {^} var _g;}');
    expect(computeFast(), isTrue);
    var a = assertSuggestMethod('_a', 'A', 'Z');
    expect(a.element.isDeprecated, isFalse);
    expect(a.element.isPrivate, isTrue);
    var f = assertSuggestGetter('f', 'X');
    expect(f.element.isDeprecated, isTrue);
    expect(f.element.isPrivate, isFalse);
    var g = assertSuggestGetter('_g', null);
    expect(g.element.isDeprecated, isFalse);
    expect(g.element.isPrivate, isTrue);
  }

  test_MethodDeclaration_parameters_named() {
    // Block  BlockFunctionBody  MethodDeclaration
    addTestSource('class A {@deprecated Z a(X x, {y: boo}) {^}}');
    expect(computeFast(), isTrue);
    var a = assertSuggestMethod('a', 'A', 'Z');
    expect(a.element.isDeprecated, isTrue);
    expect(a.element.isPrivate, isFalse);
    assertSuggestParameter('x', 'X');
    assertSuggestParameter('y', null);
  }

  test_MethodDeclaration_parameters_positional() {
    // Block  BlockFunctionBody  MethodDeclaration
    addTestSource('class A {Z a(X x, [int y=1]) {^}}');
    expect(computeFast(), isTrue);
    assertSuggestMethod('a', 'A', 'Z');
    assertSuggestParameter('x', 'X');
    assertSuggestParameter('y', 'int');
  }

  test_TopLevelVariableDeclaration_typed_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // TopLevelVariableDeclaration
    addTestSource('class A {} B ^');
    expect(computeFast(), isTrue);
    assertNotSuggested('A');
  }

  test_TopLevelVariableDeclaration_untyped_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // TopLevelVariableDeclaration
    addTestSource('class A {} var ^');
    expect(computeFast(), isTrue);
    assertNotSuggested('A');
  }

  test_VariableDeclarationStatement_name() {
    // SimpleIdentifier  VariableDeclaration  VariableDeclarationList
    // VariableDeclarationStatement
    addTestSource('class _A {a() {var f; var ^}}');
    expect(computeFast(), isTrue);
    assertNotSuggested('A');
    assertNotSuggested('a');
    assertNotSuggested('f');
  }

  test_VariableDeclaration_RHS() {

        // VariableDeclaration  VariableDeclarationList  VariableDeclarationStatement
    addTestSource('class A {a() {var f; {var x;} var e = ^ var g;}}');
    expect(computeFast(), isTrue);
    assertSuggestClass('A');
    assertSuggestLocalVariable('f', null);
    assertNotSuggested('g');
    assertNotSuggested('x');
  }
}
