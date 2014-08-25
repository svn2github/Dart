// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library services.completion.computer.dart.keyword;

import 'dart:async';

import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_server/src/services/completion/dart_completion_manager.dart';
import 'package:analyzer/src/generated/ast.dart';
import 'package:analyzer/src/generated/scanner.dart';

/**
 * A computer for calculating `completion.getSuggestions` request results
 * for the local library in which the completion is requested.
 */
class KeywordComputer extends DartCompletionComputer {

  @override
  bool computeFast(DartCompletionRequest request) {
    request.node.accept(new _KeywordVisitor(request));
    return true;
  }

  @override
  Future<bool> computeFull(DartCompletionRequest request) {
    return new Future.value(false);
  }
}

/**
 * A vistor for generating keyword suggestions.
 */
class _KeywordVisitor extends GeneralizingAstVisitor {
  final DartCompletionRequest request;

  _KeywordVisitor(this.request);

  @override
  visitClassDeclaration(ClassDeclaration node) {
    // Very simplistic suggestion because analyzer will warn if
    // the extends / with / implements keywords are out of order
    if (node.extendsClause == null) {
      _addSuggestion(Keyword.EXTENDS);
    } else if (node.withClause == null) {
      _addSuggestion(Keyword.WITH);
    }
    if (node.implementsClause == null) {
      _addSuggestion(Keyword.IMPLEMENTS);
    }
  }

  @override
  visitCompilationUnit(CompilationUnit node) {
    Directive firstDirective;
    int endOfDirectives = 0;
    if (node.directives.length > 0) {
      firstDirective = node.directives[0];
      endOfDirectives = node.directives.last.end - 1;
    }
    int startOfDeclarations = node.end;
    if (node.declarations.length > 0) {
      startOfDeclarations = node.declarations[0].offset;
    }

    // Simplistic check for library as first directive
    if (firstDirective is! LibraryDirective) {
      if (firstDirective != null) {
        if (request.offset <= firstDirective.offset) {
          _addSuggestions([Keyword.LIBRARY]);
        }
      } else {
        if (request.offset <= startOfDeclarations) {
          _addSuggestions([Keyword.LIBRARY]);
        }
      }
    }
    if (request.offset <= startOfDeclarations) {
      _addSuggestions([Keyword.EXPORT, Keyword.IMPORT, Keyword.PART]);
    }
    if (request.offset >= endOfDirectives) {
      _addSuggestions(
          [
              Keyword.ABSTRACT,
              Keyword.CLASS,
              Keyword.CONST,
              Keyword.FINAL,
              Keyword.TYPEDEF,
              Keyword.VAR]);
    }
  }

  @override
  visitNode(AstNode node) {
    if (request.offset == node.end) {
      Token token = node.endToken;
      if (token != null && !token.isSynthetic) {
        if (token.lexeme == ';' || token.lexeme == '}') {
          node.parent.accept(this);
        }
      }
    }
  }

  void _addSuggestion(Keyword keyword) {
    String completion = keyword.syntax;
    request.suggestions.add(
        new CompletionSuggestion(
            CompletionSuggestionKind.KEYWORD,
            CompletionRelevance.DEFAULT,
            completion,
            completion.length,
            0,
            false,
            false));
  }

  void _addSuggestions(List<Keyword> keywords) {
    keywords.forEach((Keyword keyword) {
      _addSuggestion(keyword);
    });
  }
}
