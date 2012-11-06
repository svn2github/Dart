/*
 * Copyright (c) 2012, the Dart project authors.
 * 
 * Licensed under the Eclipse Public License v1.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 * 
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */
package com.google.dart.engine.parser;

import com.google.dart.engine.ast.CompilationUnit;
import com.google.dart.engine.ast.SimpleIdentifier;
import com.google.dart.engine.ast.StringLiteral;
import com.google.dart.engine.ast.SuperExpression;
import com.google.dart.engine.ast.TryStatement;
import com.google.dart.engine.ast.TypedLiteral;
import com.google.dart.engine.scanner.Token;

/**
 * The class {@code ErrorParserTest} defines parser tests that test the parsing of code to ensure
 * that errors are correctly reported, and in some cases, not reported.
 */
public class ErrorParserTest extends ParserTestCase {
  public void fail_expectedListOrMapLiteral() throws Exception {
    // It isn't clear that this test can ever pass. The parser is currently create a synthetic list
    // literal in this case, but isSynthetic() isn't overridden for ListLiteral. The problem is that
    // the synthetic list literals that are being created are not always zero length (because they
    // could have type parameters), which violates the contract of isSynthetic().
    TypedLiteral literal = parse(
        "parseListOrMapLiteral",
        new Class[] {Token.class},
        new Object[] {null},
        "1",
        ParserErrorCode.EXPECTED_LIST_OR_MAP_LITERAL);
    assertTrue(literal.isSynthetic());
  }

  public void fail_illegalAssignmentToNonAssignable_superAssigned() throws Exception {
    // TODO(brianwilkerson) When this test starts to pass, remove the test
    // test_illegalAssignmentToNonAssignable_superAssigned.
    parse("parseExpression", "super = x;", ParserErrorCode.ILLEGAL_ASSIGNMENT_TO_NON_ASSIGNABLE);
  }

  public void fail_unexpectedToken_invalidPostfixExpression() throws Exception {
    // Note: this might not be the right error to produce, but some error should be produced
    parse("parseExpression", "f()++", ParserErrorCode.UNEXPECTED_TOKEN);
  }

  public void test_breakOutsideOfLoop_breakInDoStatement() throws Exception {
    parse("parseDoStatement", "do {break;} while (x);");
  }

  public void test_breakOutsideOfLoop_breakInForStatement() throws Exception {
    parse("parseForStatement", "for (; x;) {break;}");
  }

  public void test_breakOutsideOfLoop_breakInIfStatement() throws Exception {
    parse("parseIfStatement", "if (x) {break;}", ParserErrorCode.BREAK_OUTSIDE_OF_LOOP);
  }

  public void test_breakOutsideOfLoop_breakInSwitchStatement() throws Exception {
    parse("parseSwitchStatement", "switch (x) {case 1: break;}");
  }

  public void test_breakOutsideOfLoop_breakInWhileStatement() throws Exception {
    parse("parseWhileStatement", "while (x) {break;}");
  }

  public void test_breakOutsideOfLoop_functionExpression_inALoop() throws Exception {
    parse("parseStatement", "for(; x;) {() {break;};}", ParserErrorCode.BREAK_OUTSIDE_OF_LOOP);
  }

  public void test_breakOutsideOfLoop_functionExpression_withALoop() throws Exception {
    parse("parseStatement", "() {for (; x;) {break;}};");
  }

  public void test_builtInIdentifierAsTypeDefName() throws Exception {
    parse(
        "parseTypeAlias",
        new Class[] {Parser.CommentAndMetadata.class},
        new Object[] {emptyCommentAndMetadata()},
        "typedef as();",
        ParserErrorCode.BUILT_IN_IDENTIFIER_AS_TYPEDEF_NAME);
  }

  public void test_builtInIdentifierAsTypeName() throws Exception {
    parse(
        "parseClassDeclaration",
        new Class[] {Parser.CommentAndMetadata.class},
        new Object[] {emptyCommentAndMetadata()},
        "class as {}",
        ParserErrorCode.BUILT_IN_IDENTIFIER_AS_TYPE_NAME);
  }

  public void test_builtInIdentifierAsTypeVariableName() throws Exception {
    parse("parseTypeParameter", "as", ParserErrorCode.BUILT_IN_IDENTIFIER_AS_TYPE_VARIABLE_NAME);
  }

  public void test_continueOutsideOfLoop_continueInDoStatement() throws Exception {
    parse("parseDoStatement", "do {continue;} while (x);");
  }

  public void test_continueOutsideOfLoop_continueInForStatement() throws Exception {
    parse("parseForStatement", "for (; x;) {continue;}");
  }

  public void test_continueOutsideOfLoop_continueInIfStatement() throws Exception {
    parse("parseIfStatement", "if (x) {continue;}", ParserErrorCode.CONTINUE_OUTSIDE_OF_LOOP);
  }

  public void test_continueOutsideOfLoop_continueInSwitchStatement() throws Exception {
    parse("parseSwitchStatement", "switch (x) {case 1: continue a;}");
  }

  public void test_continueOutsideOfLoop_continueInWhileStatement() throws Exception {
    parse("parseWhileStatement", "while (x) {continue;}");
  }

  public void test_continueOutsideOfLoop_functionExpression_inALoop() throws Exception {
    parse("parseStatement", "for(; x;) {() {continue;};}", ParserErrorCode.CONTINUE_OUTSIDE_OF_LOOP);
  }

  public void test_continueOutsideOfLoop_functionExpression_withALoop() throws Exception {
    parse("parseStatement", "() {for (; x;) {continue;}};");
  }

  public void test_continueWithoutLabelInCase_error() throws Exception {
    parse(
        "parseSwitchStatement",
        "switch (x) {case 1: continue;}",
        ParserErrorCode.CONTINUE_WITHOUT_LABEL_IN_CASE);
  }

  public void test_continueWithoutLabelInCase_noError() throws Exception {
    parse("parseSwitchStatement", "switch (x) {case 1: continue a;}");
  }

  public void test_continueWithoutLabelInCase_noError_switchInLoop() throws Exception {
    parse("parseWhileStatement", "while (a) { switch (b) {default: continue;}}");
  }

  public void test_directiveAfterDeclaration_classBeforeDirective() throws Exception {
    CompilationUnit unit = parse(
        "parseCompilationUnit",
        "class Foo{} library l;",
        ParserErrorCode.DIRECTIVE_AFTER_DECLARATION);
    assertNotNull(unit);
  }

  public void test_directiveAfterDeclaration_classBetweenDirectives() throws Exception {
    CompilationUnit unit = parse(
        "parseCompilationUnit",
        "library l;\nclass Foo{}\npart 'a.dart';",
        ParserErrorCode.DIRECTIVE_AFTER_DECLARATION);
    assertNotNull(unit);
  }

  public void test_duplicateLabelInSwitchStatement() throws Exception {
    parse(
        "parseSwitchStatement",
        "switch (e) {l1: case 0: break; l1: case 1: break;}",
        ParserErrorCode.DUPLICATE_LABEL_IN_SWITCH_STATEMENT);
  }

  public void test_expectedCaseOrDefault() throws Exception {
    parse("parseSwitchStatement", "switch (e) {break;}", ParserErrorCode.EXPECTED_CASE_OR_DEFAULT);
  }

  public void test_expectedIdentifier_number() throws Exception {
    SimpleIdentifier expression = parse(
        "parseSimpleIdentifier",
        "1",
        ParserErrorCode.EXPECTED_IDENTIFIER);
    assertTrue(expression.isSynthetic());
  }

  public void test_expectedStringLiteral() throws Exception {
    StringLiteral expression = parse(
        "parseStringLiteral",
        "1",
        ParserErrorCode.EXPECTED_STRING_LITERAL);
    assertTrue(expression.isSynthetic());
  }

  public void test_expectedToken_commaMissingInArgumentList() throws Exception {
    parse("parseArgumentList", "(x, y z)", ParserErrorCode.EXPECTED_TOKEN);
  }

  public void test_expectedToken_semicolonMissingAfterExpression() throws Exception {
    parse("parseStatement", "x", ParserErrorCode.EXPECTED_TOKEN);
  }

  public void test_expectedToken_whileMissingInDoStatement() throws Exception {
    parse("parseStatement", "do {} (x);", ParserErrorCode.EXPECTED_TOKEN);
  }

  public void test_exportDirectiveAfterPartDirective() throws Exception {
    parseCompilationUnit(
        "part 'a.dart'; export 'b.dart';",
        ParserErrorCode.EXPORT_DIRECTIVE_AFTER_PART_DIRECTIVE);
  }

  public void test_externalConstructorWithBody() throws Exception {
    parse(
        "parseClassMember",
        "external factory A() {}",
        ParserErrorCode.EXTERNAL_CONSTRUCTOR_WITH_BODY);
  }

  public void test_externalMethodWithBody() throws Exception {
    parse("parseClassMember", "external m() {}", ParserErrorCode.EXTERNAL_METHOD_WITH_BODY);
  }

  public void test_illegalAssignmentToNonAssignable_superAssigned() throws Exception {
    // TODO(brianwilkerson) When the test fail_illegalAssignmentToNonAssignable_superAssigned starts
    // to pass, remove this test (there should only be one error generated, but we're keeping this
    // test until that time so that we can catch other forms of regressions.
    parse(
        "parseExpression",
        "super = x;",
        ParserErrorCode.MISSING_ASSIGNABLE_SELECTOR,
        ParserErrorCode.ILLEGAL_ASSIGNMENT_TO_NON_ASSIGNABLE);
  }

  public void test_importDirectiveAfterPartDirective() throws Exception {
    parseCompilationUnit(
        "part 'a.dart'; import 'b.dart';",
        ParserErrorCode.IMPORT_DIRECTIVE_AFTER_PART_DIRECTIVE);
  }

  public void test_libraryDirectiveNotFirst() throws Exception {
    parse(
        "parseCompilationUnit",
        "import 'x.dart'; library l;",
        ParserErrorCode.LIBRARY_DIRECTIVE_NOT_FIRST);
  }

  public void test_libraryDirectiveNotFirst_afterPart() throws Exception {
    CompilationUnit unit = parse(
        "parseCompilationUnit",
        "part 'a.dart';\nlibrary l;",
        ParserErrorCode.LIBRARY_DIRECTIVE_NOT_FIRST);
    assertNotNull(unit);
  }

  public void test_missingAssignableSelector_identifiersAssigned() throws Exception {
    parse("parseExpression", "x.y = y;");
  }

  public void test_missingAssignableSelector_primarySelectorPostfix() throws Exception {
    parse("parseExpression", "x(y)(z)++", ParserErrorCode.MISSING_ASSIGNABLE_SELECTOR);
  }

  public void test_missingAssignableSelector_selector() throws Exception {
    parse("parseExpression", "x(y)(z).a++");
  }

  public void test_missingAssignableSelector_superPrimaryExpression() throws Exception {
    SuperExpression expression = parse(
        "parsePrimaryExpression",
        "super",
        ParserErrorCode.MISSING_ASSIGNABLE_SELECTOR);
    assertNotNull(expression.getKeyword());
  }

  public void test_missingAssignableSelector_superPropertyAccessAssigned() throws Exception {
    parse("parseExpression", "super.x = x;");
  }

  public void test_missingCatchOrFinally() throws Exception {
    TryStatement statement = parse(
        "parseTryStatement",
        "try {}",
        ParserErrorCode.MISSING_CATCH_OR_FINALLY);
    assertNotNull(statement);
  }

  public void test_mixedParameterGroups_namedPositional() throws Exception {
    parse("parseFormalParameterList", "(a, {b}, [c])", ParserErrorCode.MIXED_PARAMETER_GROUPS);
  }

  public void test_mixedParameterGroups_positionalNamed() throws Exception {
    parse("parseFormalParameterList", "(a, [b], {c})", ParserErrorCode.MIXED_PARAMETER_GROUPS);
  }

  public void test_multipleLibraryDirectives() throws Exception {
    parse(
        "parseCompilationUnit",
        "library l; library m;",
        ParserErrorCode.MULTIPLE_LIBRARY_DIRECTIVES);
  }

  public void test_multipleNamedParameterGroups() throws Exception {
    parse(
        "parseFormalParameterList",
        "(a, {b}, {c})",
        ParserErrorCode.MULTIPLE_NAMED_PARAMETER_GROUPS);
  }

  public void test_multiplePartOfDirectives() throws Exception {
    parse(
        "parseCompilationUnit",
        "part of l; part of m;",
        ParserErrorCode.MULTIPLE_PART_OF_DIRECTIVES);
  }

  public void test_multiplePositionalParameterGroups() throws Exception {
    parse(
        "parseFormalParameterList",
        "(a, [b], [c])",
        ParserErrorCode.MULTIPLE_POSITIONAL_PARAMETER_GROUPS);
  }

  public void test_namedParameterOutsideGroup() throws Exception {
    parse("parseFormalParameterList", "(a, b : 0)", ParserErrorCode.NAMED_PARAMETER_OUTSIDE_GROUP);
  }

  public void test_nonPartOfDirectiveInPart_after() throws Exception {
    parse(
        "parseCompilationUnit",
        "part of l; part 'f.dart';",
        ParserErrorCode.NON_PART_OF_DIRECTIVE_IN_PART);
  }

  public void test_nonPartOfDirectiveInPart_before() throws Exception {
    parse(
        "parseCompilationUnit",
        "part 'f.dart'; part of m;",
        ParserErrorCode.NON_PART_OF_DIRECTIVE_IN_PART);
  }

  public void test_nonUserDefinableOperator() throws Exception {
    parse(
        "parseClassMember",
        "operator +=(int x) => x + 1;",
        ParserErrorCode.NON_USER_DEFINABLE_OPERATOR);
  }

  public void test_positionalAfterNamedArgument() throws Exception {
    parse("parseArgumentList", "(x: 1, 2)", ParserErrorCode.POSITIONAL_AFTER_NAMED_ARGUMENT);
  }

  public void test_positionalParameterOutsideGroup() throws Exception {
    parse(
        "parseFormalParameterList",
        "(a, b = 0)",
        ParserErrorCode.POSITIONAL_PARAMETER_OUTSIDE_GROUP);
  }

  public void test_staticConstructor() throws Exception {
    parse("parseClassMember", "static C.m() {}", ParserErrorCode.STATIC_CONSTRUCTOR);
  }

  public void test_staticOperator_noReturnType() throws Exception {
    parse("parseClassMember", "static operator +(int x) => x + 1;", ParserErrorCode.STATIC_OPERATOR);
  }

  public void test_staticOperator_returnType() throws Exception {
    parse(
        "parseClassMember",
        "static int operator +(int x) => x + 1;",
        ParserErrorCode.STATIC_OPERATOR);
  }

  public void test_staticTopLevelDeclaration() throws Exception {
    parse(
        "parseCompilationUnitMember",
        new Class[] {Parser.CommentAndMetadata.class},
        new Object[] {emptyCommentAndMetadata()},
        "static var x;",
        ParserErrorCode.STATIC_TOP_LEVEL_DECLARATION);
  }

  public void test_useOfUnaryPlusOperator() throws Exception {
    parse("parseUnaryExpression", "+x", ParserErrorCode.USE_OF_UNARY_PLUS_OPERATOR);
  }

  public void test_wrongSeparatorForNamedParameter() throws Exception {
    parse(
        "parseFormalParameterList",
        "(a, {b = 0})",
        ParserErrorCode.WRONG_SEPARATOR_FOR_NAMED_PARAMETER);
  }

  public void test_wrongSeparatorForPositionalParameter() throws Exception {
    parse(
        "parseFormalParameterList",
        "(a, [b : 0])",
        ParserErrorCode.WRONG_SEPARATOR_FOR_POSITIONAL_PARAMETER);
  }
}
