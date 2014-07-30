// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.services.completion.toplevel;

import 'package:analysis_services/completion/completion_suggestion.dart';
import 'package:analysis_services/completion/top_level_computer.dart';
import 'package:analysis_services/index/index.dart';
import 'package:analysis_services/index/local_memory_index.dart';
import 'package:analysis_services/src/search/search_engine.dart';
import 'package:analysis_testing/abstract_single_unit.dart';
import 'package:analysis_testing/reflective_tests.dart';
import 'package:unittest/unittest.dart';
import 'dart:async';

main() {
  groupSep = ' | ';
  runReflectiveTests(TopLevelComputerTest);
}

@ReflectiveTestCase()
class TopLevelComputerTest extends AbstractSingleUnitTest {
  Index index;
  SearchEngineImpl searchEngine;
  TopLevelComputer computer;
  List<CompletionSuggestion> suggestions;

  test_class() {
    addTestUnit('class B {boolean v;}');
    return compute().then((_) {
      assertHasResult(CompletionSuggestionKind.CLASS, 'B');
      assertNoResult('v');
    });
  }

  void addTestUnit(String code) {
    resolveTestUnit(code);
    index.indexUnit(context, testUnit);
  }

  void assertHasResult(CompletionSuggestionKind kind, String completion,
      [CompletionRelevance relevance = CompletionRelevance.DEFAULT,
      bool isDeprecated = false, bool isPotential = false]) {
    var cs = suggestions.firstWhere((cs) => cs.completion == completion, orElse: () {
      var completions = suggestions.map((s) => s.completion).toList();
      fail('expected "$completion" but found\n $completions');
    });
    expect(cs.kind, equals(kind));
    expect(cs.relevance, equals(relevance));
    expect(cs.selectionOffset, equals(completion.length));
    expect(cs.selectionLength, equals(0));
    expect(cs.isDeprecated, equals(isDeprecated));
    expect(cs.isPotential, equals(isPotential));
  }

  void assertNoResult(String completion) {
    if (suggestions.any((cs) => cs.completion == completion)) {
      fail('did not expect completion: $completion');
    }
  }

  Future compute() {
    return computer.compute().then((List<CompletionSuggestion> results) {
      this.suggestions = results;
    });
  }

  @override
  void setUp() {
    super.setUp();
    index = createLocalMemoryIndex();
    searchEngine = new SearchEngineImpl(index);
    computer = new TopLevelComputer(searchEngine);
    verifyNoTestUnitErrors = false;
  }
}