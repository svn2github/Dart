// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "package:expect/expect.dart";
import "../../../sdk/lib/_internal/compiler/implementation/dart2jslib.dart";
import "../../../sdk/lib/_internal/compiler/implementation/elements/elements.dart";
import "../../../sdk/lib/_internal/compiler/implementation/tree/tree.dart";
import "../../../sdk/lib/_internal/compiler/implementation/util/util.dart";
import "mock_compiler.dart";
import "parser_helper.dart";

Compiler applyPatch(String script, String patch,
                    {bool analyzeAll: false, bool analyzeOnly: false}) {
  String core = "$DEFAULT_CORELIB\n$script";
  MockCompiler compiler = new MockCompiler(coreSource: core,
                                           analyzeAll: analyzeAll,
                                           analyzeOnly: analyzeOnly);
  var uri = Uri.parse("core.dartp");
  compiler.sourceFiles[uri.toString()] = new MockFile(patch);
  var handler = new LibraryDependencyHandler(compiler);
  compiler.patchParser.patchLibrary(handler, uri, compiler.coreLibrary);
  handler.computeExports();
  return compiler;
}

void expectHasBody(compiler, Element element) {
    var node = element.parseNode(compiler);
    Expect.isNotNull(node, "Element isn't parseable, when a body was expected");
    Expect.isNotNull(node.body);
    // If the element has a body it is either a Block or a Return statement,
    // both with different begin and end tokens.
    Expect.isTrue(node.body is Block || node.body is Return);
    Expect.notEquals(node.body.getBeginToken(), node.body.getEndToken());
}

void expectHasNoBody(compiler, Element element) {
    var node = element.parseNode(compiler);
    Expect.isNotNull(node, "Element isn't parseable, when a body was expected");
    Expect.isFalse(node.hasBody());
}

Element ensure(compiler,
               String name,
               Element lookup(name),
               {bool expectIsPatched: false,
                bool expectIsPatch: false,
                bool checkHasBody: false,
                bool expectIsGetter: false,
                bool expectIsFound: true,
                bool expectIsRegular: false}) {
  var element = lookup(buildSourceString(name));
  if (!expectIsFound) {
    Expect.isNull(element);
    return element;
  }
  Expect.isNotNull(element);
  if (expectIsGetter) {
    Expect.isTrue(element is AbstractFieldElement);
    Expect.isNotNull(element.getter);
    element = element.getter;
  }
  Expect.equals(expectIsPatched, element.isPatched);
  if (expectIsPatched) {
    Expect.isNull(element.origin);
    Expect.isNotNull(element.patch);

    Expect.equals(element, element.declaration);
    Expect.equals(element.patch, element.implementation);

    if (checkHasBody) {
      expectHasNoBody(compiler, element);
      expectHasBody(compiler, element.patch);
    }
  } else {
    Expect.isTrue(element.isImplementation);
  }
  Expect.equals(expectIsPatch, element.isPatch);
  if (expectIsPatch) {
    Expect.isNotNull(element.origin);
    Expect.isNull(element.patch);

    Expect.equals(element.origin, element.declaration);
    Expect.equals(element, element.implementation);

    if (checkHasBody) {
      expectHasBody(compiler, element);
      expectHasNoBody(compiler, element.origin);
    }
  } else {
    Expect.isTrue(element.isDeclaration);
  }
  if (expectIsRegular) {
    Expect.isNull(element.origin);
    Expect.isNull(element.patch);

    Expect.equals(element, element.declaration);
    Expect.equals(element, element.implementation);

    if (checkHasBody) {
      expectHasBody(compiler, element);
    }
  }
  Expect.isFalse(element.isPatched && element.isPatch);
  return element;
}

testPatchFunction() {
  var compiler = applyPatch(
      "external test();",
      "patch test() { return 'string'; } ");
  ensure(compiler, "test", compiler.coreLibrary.find,
         expectIsPatched: true, checkHasBody: true);
  ensure(compiler, "test", compiler.coreLibrary.patch.find,
         expectIsPatch: true, checkHasBody: true);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testPatchConstructor() {
  var compiler = applyPatch(
      """
      class Class {
        external Class();
      }
      """,
      """
      patch class Class {
        patch Class();
      }
      """);
  var classOrigin = ensure(compiler, "Class", compiler.coreLibrary.find,
                           expectIsPatched: true);
  classOrigin.ensureResolved(compiler);
  var classPatch = ensure(compiler, "Class", compiler.coreLibrary.patch.find,
                          expectIsPatch: true);

  Expect.equals(classPatch, classOrigin.patch);
  Expect.equals(classOrigin, classPatch.origin);

  var constructorOrigin = ensure(compiler, "",
                                 (name) => classOrigin.localLookup(name),
                                 expectIsPatched: true);
  var constructorPatch = ensure(compiler, "",
                                (name) => classPatch.localLookup(name),
                                expectIsPatch: true);

  Expect.equals(constructorPatch, constructorOrigin.patch);
  Expect.equals(constructorOrigin, constructorPatch.origin);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testPatchMember() {
  var compiler = applyPatch(
      """
      class Class {
        external String toString();
      }
      """,
      """
      patch class Class {
        patch String toString() => 'string';
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);
  ensure(compiler, "Class", compiler.coreLibrary.patch.find,
         expectIsPatch: true);

  ensure(compiler, "toString", container.lookupLocalMember,
         expectIsPatched: true, checkHasBody: true);
  ensure(compiler, "toString", container.patch.lookupLocalMember,
         expectIsPatch: true, checkHasBody: true);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testPatchGetter() {
  var compiler = applyPatch(
      """
      class Class {
        external int get field;
      }
      """,
      """
      patch class Class {
        patch int get field => 5;
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);
  ensure(compiler,
         "field",
         container.lookupLocalMember,
         expectIsGetter: true,
         expectIsPatched: true,
         checkHasBody: true);
  ensure(compiler,
         "field",
         container.patch.lookupLocalMember,
         expectIsGetter: true,
         expectIsPatch: true,
         checkHasBody: true);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testRegularMember() {
  var compiler = applyPatch(
      """
      class Class {
        void regular() {}
      }
      """,
      """
      patch class Class {
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);
  ensure(compiler, "Class", compiler.coreLibrary.patch.find,
         expectIsPatch: true);

  ensure(compiler, "regular", container.lookupLocalMember,
         checkHasBody: true, expectIsRegular: true);
  ensure(compiler, "regular", container.patch.lookupLocalMember,
         checkHasBody: true, expectIsRegular: true);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testGhostMember() {
  var compiler = applyPatch(
      """
      class Class {
      }
      """,
      """
      patch class Class {
        void ghost() {}
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);
  ensure(compiler, "Class", compiler.coreLibrary.patch.find,
         expectIsPatch: true);

  ensure(compiler, "ghost", container.lookupLocalMember,
         expectIsFound: false);
  ensure(compiler, "ghost", container.patch.lookupLocalMember,
         checkHasBody: true, expectIsRegular: true);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testInjectFunction() {
  var compiler = applyPatch(
      "",
      "int _function() => 5;");
  ensure(compiler,
         "_function",
         compiler.coreLibrary.find,
         expectIsFound: false);
  ensure(compiler,
         "_function",
         compiler.coreLibrary.patch.find,
         checkHasBody: true, expectIsRegular: true);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  Expect.isTrue(compiler.errors.isEmpty,
                "Unexpected errors: ${compiler.errors}");
}

testPatchSignatureCheck() {
  var compiler = applyPatch(
      """
      class Class {
        external String method1();
        external void method2(String str);
        external void method3(String s1);
        external void method4([String str]);
        external void method5({String str});
        external void method6({String str});
        external void method7([String s1]);
        external void method8({String s1});
        external void method9(String str);
        external void method10([String str]);
        external void method11({String str});
      }
      """,
      """
      patch class Class {
        patch int method1() => 0;
        patch void method2() {}
        patch void method3(String s2) {}
        patch void method4([String str, int i]) {}
        patch void method5() {}
        patch void method6([String str]) {}
        patch void method7([String s2]) {}
        patch void method8({String s2}) {}
        patch void method9(int str) {}
        patch void method10([int str]) {}
        patch void method11({int str}) {}
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.ensureResolved(compiler);
  container.parseNode(compiler);

  void expect(String methodName, List warnings, List errors) {
    compiler.warnings.clear();
    compiler.errors.clear();
    compiler.resolver.resolveMethodElement(
        ensure(compiler, methodName, container.lookupLocalMember,
            expectIsPatched: true, checkHasBody: true));
    Expect.equals(warnings.length, compiler.warnings.length,
                  "Unexpected warnings: ${compiler.warnings} on $methodName");
    for (int i = 0 ; i < warnings.length ; i++) {
      Expect.equals(warnings[i], compiler.warnings[i].message.kind);
    }
    Expect.equals(errors.length, compiler.errors.length,
                  "Unexpected errors: ${compiler.errors} on $methodName");
    for (int i = 0 ; i < errors.length ; i++) {
      Expect.equals(errors[i], compiler.errors[i].message.kind);
    }
  }

  expect("method1", [], [MessageKind.PATCH_RETURN_TYPE_MISMATCH]);
  expect("method2", [], [MessageKind.PATCH_REQUIRED_PARAMETER_COUNT_MISMATCH]);
  expect("method3", [MessageKind.PATCH_POINT_TO_PARAMETER],
                    [MessageKind.PATCH_PARAMETER_MISMATCH]);
  expect("method4", [], [MessageKind.PATCH_OPTIONAL_PARAMETER_COUNT_MISMATCH]);
  expect("method5", [], [MessageKind.PATCH_OPTIONAL_PARAMETER_COUNT_MISMATCH]);
  expect("method6", [], [MessageKind.PATCH_OPTIONAL_PARAMETER_NAMED_MISMATCH]);
  expect("method7", [MessageKind.PATCH_POINT_TO_PARAMETER],
                    [MessageKind.PATCH_PARAMETER_MISMATCH]);
  expect("method8", [MessageKind.PATCH_POINT_TO_PARAMETER],
                    [MessageKind.PATCH_PARAMETER_MISMATCH]);
  expect("method9", [MessageKind.PATCH_POINT_TO_PARAMETER],
                    [MessageKind.PATCH_PARAMETER_TYPE_MISMATCH]);
  expect("method10", [MessageKind.PATCH_POINT_TO_PARAMETER],
                     [MessageKind.PATCH_PARAMETER_TYPE_MISMATCH]);
  expect("method11", [MessageKind.PATCH_POINT_TO_PARAMETER],
                     [MessageKind.PATCH_PARAMETER_TYPE_MISMATCH]);
}

testExternalWithoutImplementationTopLevel() {
  var compiler = applyPatch(
      """
      external void foo();
      """,
      """
      // patch void foo() {}
      """);
  var function = ensure(compiler, "foo", compiler.coreLibrary.find);
  compiler.resolver.resolve(function);
  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  print('testExternalWithoutImplementationTopLevel:${compiler.errors}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind ==
          MessageKind.PATCH_EXTERNAL_WITHOUT_IMPLEMENTATION);
  Expect.stringEquals('Error: External method without an implementation.',
                      compiler.errors[0].message.toString());
}

testExternalWithoutImplementationMember() {
  var compiler = applyPatch(
      """
      class Class {
        external void foo();
      }
      """,
      """
      patch class Class {
        // patch void foo() {}
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);

  compiler.warnings.clear();
  compiler.errors.clear();
  compiler.resolver.resolveMethodElement(
      ensure(compiler, "foo", container.lookupLocalMember));
  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  print('testExternalWithoutImplementationMember:${compiler.errors}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind ==
          MessageKind.PATCH_EXTERNAL_WITHOUT_IMPLEMENTATION);
  Expect.stringEquals('Error: External method without an implementation.',
                      compiler.errors[0].message.toString());
}

testIsSubclass() {
  var compiler = applyPatch(
      """
      class A {}
      """,
      """
      patch class A {}
      """);
  ClassElement cls = ensure(compiler, "A", compiler.coreLibrary.find,
                            expectIsPatched: true);
  ClassElement patch = cls.patch;
  Expect.isTrue(cls != patch);
  Expect.isTrue(cls.isSubclassOf(patch));
  Expect.isTrue(patch.isSubclassOf(cls));
}

testPatchNonExistingTopLevel() {
  var compiler = applyPatch(
      """
      // class Class {}
      """,
      """
      patch class Class {}
      """);
  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  print('testPatchNonExistingTopLevel:${compiler.errors}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_EXISTING);
}

testPatchNonExistingMember() {
  var compiler = applyPatch(
      """
      class Class {}
      """,
      """
      patch class Class {
        patch void foo() {}
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  print('testPatchNonExistingMember:${compiler.errors}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_EXISTING);
}

testPatchNonPatchablePatch() {
  var compiler = applyPatch(
      """
      external get foo;
      """,
      """
      patch var foo;
      """);
  ensure(compiler, "foo", compiler.coreLibrary.find);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  print('testPatchNonPatchablePatch:${compiler.errors}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NONPATCHABLE);
}

testPatchNonPatchableOrigin() {
  var compiler = applyPatch(
      """
      external var foo;
      """,
      """
      patch get foo => 0;
      """);
  ensure(compiler, "foo", compiler.coreLibrary.find);

  Expect.isTrue(compiler.warnings.isEmpty,
                "Unexpected warnings: ${compiler.warnings}");
  print('testPatchNonPatchableOrigin:${compiler.errors}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NONPATCHABLE);
}

testPatchNonExternalTopLevel() {
  var compiler = applyPatch(
      """
      void foo() {}
      """,
      """
      patch void foo() {}
      """);
  print('testPatchNonExternalTopLevel.errors:${compiler.errors}');
  print('testPatchNonExternalTopLevel.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_EXTERNAL);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_FUNCTION);
}

testPatchNonExternalMember() {
  var compiler = applyPatch(
      """
      class Class {
        void foo() {}
      }
      """,
      """
      patch class Class {
        patch void foo() {}
      }
      """);
  var container = ensure(compiler, "Class", compiler.coreLibrary.find,
                         expectIsPatched: true);
  container.parseNode(compiler);

  print('testPatchNonExternalMember.errors:${compiler.errors}');
  print('testPatchNonExternalMember.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_EXTERNAL);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_FUNCTION);
}

testPatchNonClass() {
  var compiler = applyPatch(
      """
      external void Class() {}
      """,
      """
      patch class Class {}
      """);
  print('testPatchNonClass.errors:${compiler.errors}');
  print('testPatchNonClass.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_CLASS);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_CLASS);
}

testPatchNonGetter() {
  var compiler = applyPatch(
      """
      external void foo() {}
      """,
      """
      patch get foo => 0;
      """);
  print('testPatchNonClass.errors:${compiler.errors}');
  print('testPatchNonClass.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_GETTER);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_GETTER);
}

testPatchNoGetter() {
  var compiler = applyPatch(
      """
      external set foo(var value) {}
      """,
      """
      patch get foo => 0;
      """);
  print('testPatchNonClass.errors:${compiler.errors}');
  print('testPatchNonClass.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NO_GETTER);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_GETTER);
}

testPatchNonSetter() {
  var compiler = applyPatch(
      """
      external void foo() {}
      """,
      """
      patch set foo(var value) {}
      """);
  print('testPatchNonClass.errors:${compiler.errors}');
  print('testPatchNonClass.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_SETTER);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_SETTER);
}

testPatchNoSetter() {
  var compiler = applyPatch(
      """
      external get foo;
      """,
      """
      patch set foo(var value) {}
      """);
  print('testPatchNonClass.errors:${compiler.errors}');
  print('testPatchNonClass.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NO_SETTER);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_SETTER);
}

testPatchNonFunction() {
  var compiler = applyPatch(
      """
      external get foo;
      """,
      """
      patch void foo() {}
      """);
  print('testPatchNonClass.errors:${compiler.errors}');
  print('testPatchNonClass.warnings:${compiler.warnings}');
  Expect.equals(1, compiler.errors.length);
  Expect.isTrue(
      compiler.errors[0].message.kind == MessageKind.PATCH_NON_FUNCTION);
  Expect.equals(1, compiler.warnings.length);
  Expect.isTrue(
      compiler.warnings[0].message.kind == MessageKind.PATCH_POINT_TO_FUNCTION);
}

testPatchAndSelector() {
  var compiler = applyPatch(
      """
      class A {
        external void clear();
      }
      class B extends A {
      }
      """,
      """
      patch class A {
        int method() => 0;
        patch void clear() {}
      }
      """);
  ClassElement cls = ensure(compiler, "A", compiler.coreLibrary.find,
                            expectIsPatched: true);
  cls.ensureResolved(compiler);

  ensure(compiler, "method", cls.patch.lookupLocalMember,
         checkHasBody: true, expectIsRegular: true);

  ensure(compiler, "clear", cls.lookupLocalMember,
         checkHasBody: true, expectIsPatched: true);

  compiler.phase = Compiler.PHASE_DONE_RESOLVING;

  // Check that a method just in the patch class is a target for a
  // typed selector.
  var selector = new Selector.call(
      buildSourceString('method'), compiler.coreLibrary, 0);
  var typedSelector = new TypedSelector.exact(cls.rawType, selector);
  Element method =
      cls.implementation.lookupLocalMember(buildSourceString('method'));
  Expect.isTrue(selector.applies(method, compiler));
  Expect.isTrue(typedSelector.applies(method, compiler));

  // Check that the declaration method in the declaration class is a target
  // for a typed selector.
  selector = new Selector.call(
      buildSourceString('clear'), compiler.coreLibrary, 0);
  typedSelector = new TypedSelector.exact(cls.rawType, selector);
  method = cls.lookupLocalMember(buildSourceString('clear'));
  Expect.isTrue(selector.applies(method, compiler));
  Expect.isTrue(typedSelector.applies(method, compiler));

  // Check that the declaration method in the declaration class is a target
  // for a typed selector on a subclass.
  cls = ensure(compiler, "B", compiler.coreLibrary.find);
  cls.ensureResolved(compiler);
  typedSelector = new TypedSelector.exact(cls.rawType, selector);
  Expect.isTrue(selector.applies(method, compiler));
  Expect.isTrue(typedSelector.applies(method, compiler));
}

void testAnalyzeAllInjectedMembers() {
  void expect(String patchText, [expectedWarnings]) {
    if (expectedWarnings == null) expectedWarnings = [];
    if (expectedWarnings is! List) {
      expectedWarnings = <MessageKind>[expectedWarnings];
    }

    var compiler = applyPatch('', patchText,
                              analyzeAll: true, analyzeOnly: true);
    compiler.librariesToAnalyzeWhenRun = [Uri.parse('dart:core')];
    compiler.runCompiler(null);
    compareWarningKinds(patchText, expectedWarnings, compiler.warnings);
  }

  expect('String s = 0;', MessageKind.NOT_ASSIGNABLE.warning);
  expect('void method() { String s = 0; }', MessageKind.NOT_ASSIGNABLE.warning);
  expect('''
         class Class {
           String s = 0;
         }
         ''',
         MessageKind.NOT_ASSIGNABLE.warning);
  expect('''
         class Class {
           void method() {
             String s = 0;
           }
         }
         ''',
         MessageKind.NOT_ASSIGNABLE.warning);
}

void testTypecheckPatchedMembers() {
  String originText = "external void method();";
  String patchText = """
                     patch void method() {
                       String s = 0;
                     }
                     """;
  var compiler = applyPatch(originText, patchText,
                            analyzeAll: true, analyzeOnly: true);
  compiler.librariesToAnalyzeWhenRun = [Uri.parse('dart:core')];
  compiler.runCompiler(null);
  compareWarningKinds(patchText,
      [MessageKind.NOT_ASSIGNABLE.warning], compiler.warnings);
}

main() {
  testPatchConstructor();
  testPatchFunction();
  testPatchMember();
  testPatchGetter();
  testRegularMember();
  testGhostMember();
  testInjectFunction();
  testPatchSignatureCheck();

  testExternalWithoutImplementationTopLevel();
  testExternalWithoutImplementationMember();

  testIsSubclass();

  testPatchNonExistingTopLevel();
  testPatchNonExistingMember();
  testPatchNonPatchablePatch();
  testPatchNonPatchableOrigin();
  testPatchNonExternalTopLevel();
  testPatchNonExternalMember();
  testPatchNonClass();
  testPatchNonGetter();
  testPatchNoGetter();
  testPatchNonSetter();
  testPatchNoSetter();
  testPatchNonFunction();

  testPatchAndSelector();

  testAnalyzeAllInjectedMembers();
  testTypecheckPatchedMembers();
}
