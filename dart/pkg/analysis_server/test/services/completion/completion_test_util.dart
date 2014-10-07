// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.services.completion.util;

import 'dart:async';

import 'package:analysis_server/src/protocol.dart' as protocol show Element,
    ElementKind;
import 'package:analysis_server/src/protocol.dart' hide Element;
import 'package:analysis_server/src/services/completion/dart_completion_manager.dart';
import 'package:analysis_server/src/services/completion/imported_computer.dart';
import 'package:analysis_server/src/services/completion/invocation_computer.dart';
import 'package:analysis_server/src/services/completion/local_computer.dart';
import 'package:analysis_server/src/services/index/index.dart';
import 'package:analysis_server/src/services/index/local_memory_index.dart';
import 'package:analysis_server/src/services/search/search_engine_internal.dart';
import 'package:analyzer/src/generated/ast.dart';
import 'package:analyzer/src/generated/element.dart';
import 'package:analyzer/src/generated/engine.dart';
import 'package:analyzer/src/generated/source.dart';
import 'package:unittest/unittest.dart';

import '../../abstract_context.dart';

class AbstractCompletionTest extends AbstractContextTest {
  Index index;
  SearchEngineImpl searchEngine;
  DartCompletionComputer computer;
  String testFile = '/completionTest.dart';
  Source testSource;
  CompilationUnit testUnit;
  int completionOffset;
  AstNode completionNode;
  bool _computeFastCalled = false;
  DartCompletionRequest request;

  void addResolvedUnit(String file, String code) {
    Source source = addSource(file, code);
    CompilationUnit unit = resolveLibraryUnit(source);
    index.indexUnit(context, unit);
  }

  void addTestSource(String content) {
    expect(completionOffset, isNull, reason: 'Call addTestUnit exactly once');
    completionOffset = content.indexOf('^');
    expect(completionOffset, isNot(equals(-1)), reason: 'missing ^');
    int nextOffset = content.indexOf('^', completionOffset + 1);
    expect(nextOffset, equals(-1), reason: 'too many ^');
    content = content.substring(0, completionOffset) +
        content.substring(completionOffset + 1);
    testSource = addSource(testFile, content);
    request =
        new DartCompletionRequest(context, searchEngine, testSource, completionOffset);
  }

  void assertNoSuggestions() {
    expect(request.suggestions, hasLength(0));
  }

  CompletionSuggestion assertNotSuggested(String completion) {
    CompletionSuggestion suggestion = request.suggestions.firstWhere(
        (cs) => cs.completion == completion,
        orElse: () => null);
    if (suggestion != null) {
      _failedCompletion(
          'did not expect completion: $completion\n  $suggestion');
    }
    return null;
  }

  CompletionSuggestion assertSuggest(CompletionSuggestionKind kind,
      String completion, [CompletionRelevance relevance = CompletionRelevance.DEFAULT,
      bool isDeprecated = false, bool isPotential = false]) {
    CompletionSuggestion cs;
    request.suggestions.forEach((s) {
      if (s.completion == completion && s.kind == kind) {
        if (cs == null) {
          cs = s;
        } else {
          _failedCompletion(
              'expected exactly one $completion',
              request.suggestions.where((s) => s.completion == completion));
        }
      }
    });
    if (cs == null) {
      _failedCompletion('expected $completion $kind', request.suggestions);
    }
    expect(cs.kind, equals(kind));
    expect(cs.relevance, equals(relevance));
    expect(cs.selectionOffset, equals(completion.length));
    expect(cs.selectionLength, equals(0));
    expect(cs.isDeprecated, equals(isDeprecated));
    expect(cs.isPotential, equals(isPotential));
    return cs;
  }

  CompletionSuggestion assertSuggestClass(String name,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.CLASS, name, relevance);
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.CLASS));
    expect(element.name, equals(name));
    expect(element.returnType, isNull);
    return cs;
  }

  CompletionSuggestion assertSuggestConstructor(String name,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.CONSTRUCTOR, name, relevance);
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.CONSTRUCTOR));
    expect(element.name, equals(name));
    expect(element.returnType, isNull);
    return cs;
  }

  CompletionSuggestion assertSuggestFunction(String name, String returnType,
      bool isDeprecated, [CompletionRelevance relevance =
      CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs = assertSuggest(
        CompletionSuggestionKind.FUNCTION,
        name,
        relevance,
        isDeprecated);
    expect(cs.returnType, equals(returnType));
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.FUNCTION));
    expect(element.name, equals(name));
    expect(element.isDeprecated, equals(isDeprecated));
    expect(
        element.returnType,
        equals(returnType != null ? returnType : 'dynamic'));
    return cs;
  }

  CompletionSuggestion assertSuggestGetter(String name, String returnType,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.GETTER, name, relevance);
    expect(cs.returnType, equals(returnType));
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.GETTER));
    expect(element.name, equals(name));
    expect(
        element.returnType,
        equals(returnType != null ? returnType : 'dynamic'));
    return cs;
  }

  CompletionSuggestion assertSuggestLibraryPrefix(String prefix,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    // Library prefix should only be suggested by ImportedComputer
    if (computer is ImportedComputer) {
      CompletionSuggestion cs =
          assertSuggest(CompletionSuggestionKind.LIBRARY_PREFIX, prefix, relevance);
      protocol.Element element = cs.element;
      expect(element, isNotNull);
      expect(element.kind, equals(protocol.ElementKind.LIBRARY));
      expect(element.returnType, isNull);
      return cs;
    } else {
      return null;
    }
  }

  CompletionSuggestion assertSuggestLocalVariable(String name,
      String returnType, [CompletionRelevance relevance =
      CompletionRelevance.DEFAULT]) {
    // Local variables should only be suggested by LocalComputer
    if (computer is LocalComputer) {
      CompletionSuggestion cs =
          assertSuggest(CompletionSuggestionKind.LOCAL_VARIABLE, name, relevance);
      expect(cs.returnType, equals(returnType));
      protocol.Element element = cs.element;
      expect(element, isNotNull);
      expect(element.kind, equals(protocol.ElementKind.LOCAL_VARIABLE));
      expect(element.name, equals(name));
      expect(
          element.returnType,
          equals(returnType != null ? returnType : 'dynamic'));
      return cs;
    } else {
      return null;
    }
  }

  CompletionSuggestion assertSuggestMethod(String name, String declaringType,
      String returnType, [CompletionRelevance relevance =
      CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.METHOD, name, relevance);
    expect(cs.declaringType, equals(declaringType));
    expect(cs.returnType, equals(returnType));
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.METHOD));
    expect(element.name, equals(name));
    expect(
        element.returnType,
        equals(returnType != null ? returnType : 'dynamic'));
    return cs;
  }

  CompletionSuggestion assertSuggestParameter(String name, String returnType,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.PARAMETER, name, relevance);
    expect(cs.returnType, equals(returnType));
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.PARAMETER));
    expect(element.name, equals(name));
    expect(
        element.returnType,
        equals(returnType != null ? returnType : 'dynamic'));
    return cs;
  }

  CompletionSuggestion assertSuggestSetter(String name,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.SETTER, name, relevance);
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.SETTER));
    expect(element.name, equals(name));
    expect(element.returnType, isNull);
    return cs;
  }

  CompletionSuggestion assertSuggestTopLevelVar(String name, String returnType,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    CompletionSuggestion cs =
        assertSuggest(CompletionSuggestionKind.TOP_LEVEL_VARIABLE, name, relevance);
    expect(cs.returnType, equals(returnType));
    protocol.Element element = cs.element;
    expect(element, isNotNull);
    expect(element.kind, equals(protocol.ElementKind.TOP_LEVEL_VARIABLE));
    expect(element.name, equals(name));
    //TODO (danrubel) return type level variable 'type' but not as 'returnType'
//    expect(
//        element.returnType,
//        equals(returnType != null ? returnType : 'dynamic'));
    return cs;
  }

  void assertSuggestTopLevelVarGetterSetter(String name, String returnType,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    assertSuggestGetter(name, returnType);
    assertSuggestSetter(name);
  }

  bool computeFast() {
    _computeFastCalled = true;
    testUnit = context.parseCompilationUnit(testSource);
    completionNode =
        new NodeLocator.con1(completionOffset).searchWithin(testUnit);
    request.unit = testUnit;
    request.node = completionNode;
    return computer.computeFast(request);
  }

  Future<bool> computeFull([bool fullAnalysis = false]) {
    if (!_computeFastCalled) {
      expect(computeFast(), isFalse);
    }

    // Index SDK
    for (Source librarySource in context.librarySources) {
      CompilationUnit unit =
          context.getResolvedCompilationUnit2(librarySource, librarySource);
      if (unit != null) {
        index.indexUnit(context, unit);
      }
    }

    var result = context.performAnalysisTask();
    bool resolved = false;
    while (result.hasMoreWork) {

      // Update the index
      result.changeNotices.forEach((ChangeNotice notice) {
        CompilationUnit unit = notice.compilationUnit;
        if (unit != null) {
          index.indexUnit(context, unit);
        }
      });

      // If the unit has been resolved, then finish the completion
      LibraryElement library = context.getLibraryElement(testSource);
      if (library != null) {
        CompilationUnit unit =
            context.getResolvedCompilationUnit(testSource, library);
        if (unit != null) {
          request.unit = unit;
          request.node =
              new NodeLocator.con1(completionOffset).searchWithin(unit);
          resolved = true;
          if (!fullAnalysis) {
            break;
          }
        }
      }

      result = context.performAnalysisTask();
    }
    if (!resolved) {
      fail('expected unit to be resolved');
    }
    return computer.computeFull(request);
  }

  @override
  void setUp() {
    super.setUp();
    index = createLocalMemoryIndex();
    searchEngine = new SearchEngineImpl(index);
  }

  void _failedCompletion(String message,
      [Iterable<CompletionSuggestion> completions]) {
    StringBuffer sb = new StringBuffer(message);
    if (completions != null) {
      sb.write('\n  found');
      completions.toList()
          ..sort((CompletionSuggestion s1, CompletionSuggestion s2) {
            String c1 = s1.completion.toLowerCase();
            String c2 = s2.completion.toLowerCase();
            return c1.compareTo(c2);
          })
          ..forEach((CompletionSuggestion suggestion) {
            sb.write('\n    ${suggestion.completion} -> $suggestion');
          });
    }
    if (completionNode != null) {
      sb.write('\n  in');
      AstNode node = completionNode;
      while (node != null) {
        sb.write('\n    ${node.runtimeType}');
        node = node.parent;
      }
    }
    fail(sb.toString());
  }
}

/**
 * Common tests for `ImportedTypeComputerTest`, `InvocationComputerTest`,
 * and `LocalComputerTest`.
 */
class AbstractSelectorSuggestionTest extends AbstractCompletionTest {

  CompletionSuggestion assertLocalSuggestMethod(String name,
      String declaringType, String returnType, [CompletionRelevance relevance =
      CompletionRelevance.DEFAULT]) {
    if (computer is LocalComputer) {
      return assertSuggestMethod(name, declaringType, returnType, relevance);
    } else {
      return assertNotSuggested(name);
    }
  }

  CompletionSuggestion assertSuggestImportedClass(String name,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    if (computer is ImportedComputer) {
      return assertSuggestClass(name, relevance);
    } else {
      return assertNotSuggested(name);
    }
  }

  CompletionSuggestion assertSuggestInvocationGetter(String name, String returnType,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    if (computer is InvocationComputer) {
      return assertSuggestGetter(name, returnType, relevance);
    } else {
      return null;
    }
  }

  CompletionSuggestion assertSuggestLocalClass(String name,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT]) {
    if (computer is LocalComputer) {
      return assertSuggestClass(name, relevance);
    } else {
      return assertNotSuggested(name);
    }
  }

  test_Block() {
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
      class X {a() {var f; {var x;} ^ var r;} Z b() { }}
      class Z { }''');
    computeFast();
    return computeFull(true).then((_) {

      assertSuggestLocalClass('X');
      assertSuggestLocalClass('Z');
      assertLocalSuggestMethod('a', 'X', null);
      assertLocalSuggestMethod('b', 'X', 'Z');
      assertSuggestLocalVariable('f', null);
      // Don't suggest locals out of scope
      assertNotSuggested('r');
      assertNotSuggested('x');

      assertSuggestImportedClass('A');
      assertNotSuggested('_B');
      assertSuggestImportedClass('C');
      // hidden element suggested as low relevance
      assertSuggestImportedClass('D', CompletionRelevance.LOW);
      assertSuggestImportedClass('EE');
      // hidden element suggested as low relevance
      assertSuggestImportedClass('F', CompletionRelevance.LOW);
      assertSuggestLibraryPrefix('g');
      assertNotSuggested('G');
      assertSuggestImportedClass('H', CompletionRelevance.LOW);
      assertSuggestImportedClass('Object');
      // TODO (danrubel) suggest HtmlElement as low relevance
      assertNotSuggested('HtmlElement');
    });
  }

  test_CascadeExpression_selector1() {
    addSource('/testB.dart', '''
      class B { }''');
    addTestSource('''
      import "/testB.dart";
      class A {var b; X _c;}
      class X{}
      // looks like a cascade to the parser
      // but the user is trying to get completions for a non-cascade
      main() {A a; a.^.z}''');
    computeFast();
    return computeFull(true).then((_) {
      assertSuggestInvocationGetter('b', null);
      assertSuggestInvocationGetter('_c', 'X');
      assertNotSuggested('Object');
      assertNotSuggested('A');
      assertNotSuggested('B');
      assertNotSuggested('X');
      assertNotSuggested('z');
    });
  }

  test_CascadeExpression_selector2() {
    addSource('/testB.dart', '''
      class B { }''');
    addTestSource('''
      import "/testB.dart";
      class A {var b; X _c;}
      class X{}
      main() {A a; a..^z}''');
    computeFast();
    assertNoSuggestions();
    return computeFull(true).then((_) {
      assertSuggestInvocationGetter('b', null);
      assertSuggestInvocationGetter('_c', 'X');
      assertNotSuggested('Object');
      assertNotSuggested('A');
      assertNotSuggested('B');
      assertNotSuggested('X');
      assertNotSuggested('z');
    });
  }
}
