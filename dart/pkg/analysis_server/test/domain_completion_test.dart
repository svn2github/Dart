// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.domain.completion;

import 'dart:async';

import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/domain_completion.dart';
import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_services/completion/completion_suggestion.dart';
import 'package:analysis_services/constants.dart';
import 'package:analysis_services/index/index.dart' show Index;
import 'package:analysis_services/index/local_memory_index.dart';
import 'package:analysis_testing/reflective_tests.dart';
import 'package:unittest/unittest.dart';

import 'analysis_abstract.dart';

main() {
  groupSep = ' | ';
  runReflectiveTests(CompletionTest);
}

@ReflectiveTestCase()
class CompletionTest extends AbstractAnalysisTest {
  String completionId;
  int completionOffset;
  int replacementOffset;
  int replacementLength;
  List<CompletionSuggestion> suggestions = [];
  bool suggestionsDone = false;

  String addTestFile(String content) {
    completionOffset = content.indexOf('^');
    expect(completionOffset, isNot(equals(-1)), reason: 'missing ^');
    int nextOffset = content.indexOf('^', completionOffset + 1);
    expect(nextOffset, equals(-1), reason: 'too many ^');
    return super.addTestFile(
        content.substring(0, completionOffset)
        + content.substring(completionOffset + 1));
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

  void assertValidId(String id) {
    expect(id, isNotNull);
    expect(id.isNotEmpty, isTrue);
  }

  @override
  Index createIndex() {
    return createLocalMemoryIndex();
  }

  Future getSuggestions() {
    return waitForTasksFinished().then((_) {
      Request request = new Request('0', COMPLETION_GET_SUGGESTIONS);
      request.setParameter(FILE, testFile);
      request.setParameter(OFFSET, completionOffset);
      Response response = handleSuccessfulRequest(request);
      completionId = response.getResult(ID);
      assertValidId(completionId);
      return waitForSuggestions();
    });
  }

  void processNotification(Notification notification) {
    if (notification.event == COMPLETION_RESULTS) {
      String id = notification.getParameter(ID);
      assertValidId(id);
      if (id == completionId) {
        expect(suggestionsDone, isFalse);
        replacementOffset = notification.getParameter(REPLACEMENT_OFFSET);
        replacementLength = notification.getParameter(REPLACEMENT_LENGTH);
        suggestionsDone = notification.getParameter(LAST);
        expect(suggestionsDone, isNotNull);
        for (Map<String, Object> json in notification.getParameter(RESULTS)) {
          expect(json, isNotNull);
          suggestions.add(new CompletionSuggestion.fromJson(json));
        }
      }
    }
  }

  @override
  void setUp() {
    super.setUp();
    createProject();
    handler = new CompletionDomainHandler(server);
  }

  test_suggestions_html() {
    testFile = '/project/web/test.html';
    addTestFile('''
      <html>^</html>
    ''');
    return getSuggestions().then((_) {
      expect(replacementOffset, equals(completionOffset));
      expect(replacementLength, equals(0));
      expect(suggestions, hasLength(0));
    });
  }

  test_suggestions_importedType() {
    addTestFile('''
      import 'dart:html';
      main() {^}
    ''');
    return getSuggestions().then((_) {
      expect(replacementOffset, equals(completionOffset));
      expect(replacementLength, equals(0));
      assertHasResult(CompletionSuggestionKind.CLASS, 'Object');
      assertHasResult(CompletionSuggestionKind.CLASS, 'HtmlElement');
      assertNoResult('test');
    });
  }

  test_suggestions_topLevel() {
    addTestFile('''
      typedef foo();
      var test = '';
      main() {tes^t}
    ''');
    return getSuggestions().then((_) {
//      expect(replacementOffset, equals(completionOffset - 3));
//      expect(replacementLength, equals(4));
      assertHasResult(CompletionSuggestionKind.CLASS, 'Object');
      assertHasResult(CompletionSuggestionKind.TOP_LEVEL_VARIABLE, 'test');
      assertNoResult('HtmlElement');
    });
  }

  Future waitForSuggestions() {
    if (suggestionsDone) {
      return new Future.value();
    }
    return new Future.delayed(Duration.ZERO, waitForSuggestions);
  }
}
