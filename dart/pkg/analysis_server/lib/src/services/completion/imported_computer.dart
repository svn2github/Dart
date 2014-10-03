// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library services.completion.computer.dart.toplevel;

import 'dart:async';

import 'package:analysis_server/src/protocol.dart' as protocol show Element,
    ElementKind;
import 'package:analysis_server/src/protocol.dart' hide Element, ElementKind;
import 'package:analysis_server/src/services/completion/dart_completion_manager.dart';
import 'package:analysis_server/src/services/completion/suggestion_builder.dart';
import 'package:analysis_server/src/services/search/search_engine.dart';
import 'package:analyzer/src/generated/ast.dart';
import 'package:analyzer/src/generated/element.dart';

/**
 * A computer for calculating imported class and top level variable
 * `completion.getSuggestions` request results.
 */
class ImportedComputer extends DartCompletionComputer {

  @override
  bool computeFast(DartCompletionRequest request) {
    // TODO: implement computeFast
    // - compute results based upon current search, then replace those results
    // during the full compute phase
    // - filter results based upon completion offset
    return false;
  }

  @override
  Future<bool> computeFull(DartCompletionRequest request) {
    return request.node.accept(new _ImportedVisitor(request));
  }
}

/**
 * A visitor for determining which imported class and top level variable
 * should be suggested and building those suggestions.
 */
class _ImportedVisitor extends GeneralizingAstVisitor<Future<bool>> {
  final DartCompletionRequest request;

  _ImportedVisitor(this.request);

  @override
  Future<bool> visitBlock(Block node) {
    return _addImportedElementSuggestions();
  }

  @override
  Future<bool> visitNode(AstNode node) {
    return new Future.value(false);
  }

  @override
  Future<bool> visitSimpleIdentifier(SimpleIdentifier node) {
    AstNode parent = node.parent;
    if (parent is Combinator) {
      return _addCombinatorSuggestions(parent);
    }
    if (parent is ExpressionStatement) {
      return _addImportedElementSuggestions();
    }
    return new Future.value(false);
  }

  Future _addCombinatorSuggestions(Combinator node) {
    var directive = node.getAncestor((parent) => parent is NamespaceDirective);
    if (directive is NamespaceDirective) {
      LibraryElement library = directive.uriElement;
      LibraryElementSuggestionBuilder.suggestionsFor(request, library);
      return new Future.value(true);
    }
    return new Future.value(false);
  }

  Future<bool> _addImportedElementSuggestions() {
    var future = request.searchEngine.searchTopLevelDeclarations('');
    return future.then((List<SearchMatch> matches) {

      Set<LibraryElement> visibleLibs = new Set<LibraryElement>();
      Set<LibraryElement> excludedLibs = new Set<LibraryElement>();

      Map<LibraryElement, Set<String>> showNames =
          new Map<LibraryElement, Set<String>>();
      Map<LibraryElement, Set<String>> hideNames =
          new Map<LibraryElement, Set<String>>();

      // Exclude elements from the local library
      // as they will be included by the LocalComputer
      excludedLibs.add(request.unit.element.library);

      // Build the set of visible and excluded libraries
      // and the list of names that should be shown or hidden
      request.unit.directives.forEach((Directive directive) {
        if (directive is ImportDirective) {
          ImportElement element = directive.element;
          if (element != null) {
            LibraryElement lib = element.importedLibrary;
            SimpleIdentifier prefix = directive.prefix;
            if (prefix == null) {
              visibleLibs.add(lib);
              directive.combinators.forEach((Combinator combinator) {
                if (combinator is ShowCombinator) {
                  showNames[lib] =
                      combinator.shownNames.map((SimpleIdentifier id) => id.name).toSet();
                } else if (combinator is HideCombinator) {
                  hideNames[lib] =
                      combinator.hiddenNames.map((SimpleIdentifier id) => id.name).toSet();
                }
              });
            } else {
              String completion = prefix.name;
              if (completion != null && completion.length > 0) {
                CompletionSuggestion suggestion = new CompletionSuggestion(
                    CompletionSuggestionKind.LIBRARY_PREFIX,
                    CompletionRelevance.DEFAULT,
                    completion,
                    completion.length,
                    0,
                    element.isDeprecated,
                    false);
                suggestion.element = new protocol.Element.fromEngine(lib);
                request.suggestions.add(suggestion);
              }
              excludedLibs.add(lib);
            }
          }
        }
      });

      // Compute the set of possible classes, functions, and top level variables
      matches.forEach((SearchMatch match) {
        if (match.kind == MatchKind.DECLARATION) {
          Element element = match.element;
          LibraryElement lib = element.library;
          if (element.isPublic && !excludedLibs.contains(lib)) {
            String completion = element.displayName;
            Set<String> show = showNames[lib];
            Set<String> hide = hideNames[lib];
            if ((show == null || show.contains(completion)) &&
                (hide == null || !hide.contains(completion))) {

              CompletionSuggestionKind kind =
                  new CompletionSuggestionKind.fromElementKind(element.kind);

              CompletionRelevance relevance;
              if (visibleLibs.contains(lib) || lib.isDartCore) {
                relevance = CompletionRelevance.DEFAULT;
              } else {
                relevance = CompletionRelevance.LOW;
              }

              CompletionSuggestion suggestion = new CompletionSuggestion(
                  kind,
                  relevance,
                  completion,
                  completion.length,
                  0,
                  element.isDeprecated,
                  false);

              suggestion.element = new protocol.Element.fromEngine(element);

              DartType type;
              if (element is TopLevelVariableElement) {
                type = element.type;
              } else if (element is FunctionElement) {
                type = element.returnType;
              }
              if (type != null) {
                String name = type.displayName;
                if (name != null && name.length > 0 && name != 'dynamic') {
                  suggestion.returnType = name;
                }
              }

              request.suggestions.add(suggestion);
            }
          }
        }
      });
      return true;
    });
  }
}
