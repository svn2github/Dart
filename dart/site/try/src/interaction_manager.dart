// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library trydart.interaction_manager;

import 'dart:html';

import 'dart:convert' show
    JSON;

import 'dart:math' show
    max,
    min;

import 'dart:async' show
    Future;

import 'package:compiler/implementation/scanner/scannerlib.dart' show
    BeginGroupToken,
    EOF_TOKEN,
    ErrorToken,
    StringScanner,
    Token,
    UnmatchedToken,
    UnterminatedToken;

import 'package:compiler/implementation/source_file.dart' show
    StringSourceFile;

import 'compilation.dart' show
    currentSource,
    scheduleCompilation;

import 'ui.dart' show
    currentTheme,
    hackDiv,
    mainEditorPane,
    observer,
    outputDiv,
    statusDiv;

import 'decoration.dart' show
    CodeCompletionDecoration,
    Decoration,
    DiagnosticDecoration,
    error,
    info,
    warning;

import 'html_to_text.dart' show
    htmlToText;

import 'compilation_unit.dart' show
    CompilationUnit;

import 'selection.dart' show
    TrySelection,
    isCollapsed;

import 'editor.dart' as editor;

import 'mock.dart' as mock;

import 'settings.dart' as settings;

import 'shadow_root.dart' show
    getShadowRoot,
    removeShadowRootPolyfill,
    setShadowRoot;

const String TRY_DART_NEW_DEFECT =
    'https://code.google.com/p/dart/issues/entry'
    '?template=Try+Dart+Internal+Error';

/**
 * UI interaction manager for the entire application.
 */
abstract class InteractionManager {
  // Design note: All UI interactions go through one instance of this
  // class. This is by design.
  //
  // Simplicity in UI is in the eye of the beholder, not the implementor. Great
  // 'natural UI' is usually achieved with substantial implementation
  // complexity that doesn't modularise well and has nasty complicated state
  // dependencies.
  //
  // In rare cases, some UI components can be independent of this state
  // machine. For example, animation and auto-save loops.

  // Implementation note: The state machine is actually implemented by
  // [InteractionContext], this class represents public event handlers.

  factory InteractionManager() => new InteractionContext();

  InteractionManager.internal();

  void onInput(Event event);

  // TODO(ahe): Rename to onKeyDown (as it is called in response to keydown
  // event).
  void onKeyUp(KeyboardEvent event);

  void onMutation(List<MutationRecord> mutations, MutationObserver observer);

  void onSelectionChange(Event event);

  /// Called when the content of a CompilationUnit changed.
  void onCompilationUnitChanged(CompilationUnit unit);

  Future<List<String>> projectFileNames();

  /// Called when the user selected a new project file.
  void onProjectFileSelected(String projectFile);

  /// Called when notified about a project file changed (on the server).
  void onProjectFileFsEvent(MessageEvent e);
}

/**
 * State machine for UI interactions.
 */
class InteractionContext extends InteractionManager {
  InteractionState state;

  final Map<String, CompilationUnit> projectFiles = <String, CompilationUnit>{};

  CompilationUnit currentCompilationUnit =
      // TODO(ahe): Don't use a fake unit.
      new CompilationUnit('fake', '');

  CompilationUnit lastSaved;

  InteractionContext()
      : super.internal() {
    state = new InitialState(this);
  }

  void onInput(Event event) => state.onInput(event);

  void onKeyUp(KeyboardEvent event) => state.onKeyUp(event);

  void onMutation(List<MutationRecord> mutations, MutationObserver observer) {
    try {
      try {
        return state.onMutation(mutations, observer);
      } finally {
        // Discard any mutations during the observer, as these can lead to
        // infinite loop.
        observer.takeRecords();
      }
    } catch (error, stackTrace) {
      try {
        editor.isMalformedInput = true;
        outputDiv
            ..nodes.clear()
            ..append(new HeadingElement.h1()..appendText('Internal Error'))
            ..appendText('We would appreciate if you take a moment to report '
                         'this at ')
            ..append(
                new AnchorElement(href: TRY_DART_NEW_DEFECT)
                    ..target = '_blank'
                    ..appendText(TRY_DART_NEW_DEFECT))
            ..appendText('\nError and stack trace:\n$error\n')
            ..appendText('$stackTrace\n');
      } catch (e) {
        // Double faults ignored.
      }
      rethrow;
    }
  }

  void onSelectionChange(Event event) => state.onSelectionChange(event);

  void onCompilationUnitChanged(CompilationUnit unit) {
    return state.onCompilationUnitChanged(unit);
  }

  Future<List<String>> projectFileNames() => state.projectFileNames();

  void onProjectFileSelected(String projectFile) {
    return state.onProjectFileSelected(projectFile);
  }

  void onProjectFileFsEvent(MessageEvent e) {
    return state.onProjectFileFsEvent(e);
  }
}

abstract class InteractionState implements InteractionManager {
  InteractionContext get context;

  void set state(InteractionState newState);

  void onStateChanged(InteractionState previous) {
    print('State change ${previous.runtimeType} -> ${runtimeType}.');
  }

  void transitionToInitialState() {
    state = new InitialState(context);
  }
}

class InitialState extends InteractionState {
  final InteractionContext context;
  bool requestCodeCompletion = false;

  InitialState(this.context);

  void set state(InteractionState state) {
    InteractionState previous = context.state;
    if (previous != state) {
      context.state = state;
      state.onStateChanged(previous);
    }
  }

  void onInput(Event event) {
    state = new PendingInputState(context);
  }

  void onKeyUp(KeyboardEvent event) {
    if (computeHasModifier(event)) {
      print('onKeyUp (modified)');
      onModifiedKeyUp(event);
    } else {
      print('onKeyUp (unmodified)');
      onUnmodifiedKeyUp(event);
    }
  }

  void onModifiedKeyUp(KeyboardEvent event) {
  }

  void onUnmodifiedKeyUp(KeyboardEvent event) {
    switch (event.keyCode) {
      case KeyCode.ENTER: {
        Selection selection = window.getSelection();
        if (isCollapsed(selection)) {
          event.preventDefault();
          Node node = selection.anchorNode;
          if (node is Text) {
            Text text = node;
            int offset = selection.anchorOffset;
            // If at end-of-file, insert an extra newline.  The the extra
            // newline ensures that the next line isn't empty.  At least Chrome
            // behaves as if "\n" is just a single line. "\nc" (where c is any
            // character) is two lines, according to Chrome.
            String newline = isAtEndOfFile(text, offset) ? '\n\n' : '\n';
            text.insertData(offset, newline);
            selection.collapse(text, offset + 1);
          } else if (node is Element) {
            node.appendText('\n\n');
            selection.collapse(node.firstChild, 1);
          } else {
            window.console
                ..error('Unexpected node')
                ..dir(node);
          }
        }
        break;
      }
    }

    // This is a hack to get Safari (iOS) to send mutation events on
    // contenteditable.
    // TODO(ahe): Move to onInput?
    var newDiv = new DivElement();
    hackDiv.replaceWith(newDiv);
    hackDiv = newDiv;
  }

  void onMutation(List<MutationRecord> mutations, MutationObserver observer) {
    print('onMutation');

    removeCodeCompletion();

    Selection selection = window.getSelection();
    TrySelection trySelection = new TrySelection(mainEditorPane, selection);

    Set<Node> normalizedNodes = new Set<Node>();
    for (MutationRecord record in mutations) {
      normalizeMutationRecord(record, trySelection, normalizedNodes);
    }

    if (normalizedNodes.length == 1) {
      Node node = normalizedNodes.single;
      if (node is Element && node.classes.contains('lineNumber')) {
        print('Single line change: ${node.outerHtml}');

        removeShadowRootPolyfill(node);

        String currentText = node.text;

        trySelection = new TrySelection(node, selection);
        trySelection.updateText(currentText);

        editor.isMalformedInput = false;
        int offset = 0;
        List<Node> nodes = <Node>[];

        String state = '';
        Element previousLine = node.previousElementSibling;
        if (previousLine != null) {
          state = previousLine.getAttribute('dart-state');
        }
        for (String line in splitLines(currentText)) {
          List<Node> lineNodes = <Node>[];
          state = tokenizeAndHighlight(
              line, state, offset, trySelection, lineNodes);
          offset += line.length;
          nodes.add(makeLine(lineNodes, state));
        }

        node.parent.insertAllBefore(nodes, node);
        node.remove();
        trySelection.adjust(selection);

        // Discard highlighting mutations.
        observer.takeRecords();
        return;
      }
    }

    removeShadowRootPolyfill(mainEditorPane);

    String currentText = mainEditorPane.text;
    trySelection.updateText(currentText);

    context.currentCompilationUnit.content = currentText;

    editor.seenIdentifiers = new Set<String>.from(mock.identifiers);

    editor.isMalformedInput = false;
    int offset = 0;
    List<Node> nodes = <Node>[];

    String state = '';
    for (String line in splitLines(currentText)) {
      List<Node> lineNodes = <Node>[];
      state =
          tokenizeAndHighlight(line, state, offset, trySelection, lineNodes);
      offset += line.length;
      nodes.add(makeLine(lineNodes, state));
    }

    mainEditorPane
        ..nodes.clear()
        ..nodes.addAll(nodes);
    trySelection.adjust(selection);

    // Discard highlighting mutations.
    observer.takeRecords();
  }

  void onSelectionChange(Event event) {
  }

  void onStateChanged(InteractionState previous) {
    super.onStateChanged(previous);
    scheduleCompilation();
  }

  void onCompilationUnitChanged(CompilationUnit unit) {
    if (unit == context.currentCompilationUnit) {
      currentSource = unit.content;
      print("Saved source of '${unit.name}'");
      if (context.projectFiles.containsKey(unit.name)) {
        postProjectFileUpdate(unit);
      }
      scheduleCompilation();
    } else {
      print("Unexpected change to compilation unit '${unit.name}'.");
    }
  }

  void postProjectFileUpdate(CompilationUnit unit) {
    context.lastSaved = unit;
    onError(ProgressEvent event) {
      HttpRequest request = event.target;
      statusDiv.text = "Couldn't save '${unit.name}': ${request.responseText}";
      context.lastSaved = null;
    }
    new HttpRequest()
        ..open("POST", "/project/${unit.name}")
        ..onError.listen(onError)
        ..send(unit.content);
  }

  Future<List<String>> projectFileNames() {
    return getString('project?list').then((String response) {
      WebSocket socket = new WebSocket('ws://127.0.0.1:9090/ws/watch');
      socket.onMessage.listen(context.onProjectFileFsEvent);
      return new List<String>.from(JSON.decode(response));
    });
  }

  void onProjectFileSelected(String projectFile) {
    // Disable editing whilst fetching data.
    mainEditorPane.contentEditable = 'false';

    CompilationUnit unit = context.projectFiles[projectFile];
    Future<CompilationUnit> future;
    if (unit != null) {
      // This project file had been fetched already.
      future = new Future<CompilationUnit>.value(unit);

      // TODO(ahe): Probably better to fetch the sources again.
    } else {
      // This project file has to be fetched.
      future = getString('project/$projectFile').then((String text) {
        CompilationUnit unit = context.projectFiles[projectFile];
        if (unit == null) {
          // Only create a new unit if the value hadn't arrived already.
          unit = new CompilationUnit(projectFile, text);
          context.projectFiles[projectFile] = unit;
        } else {
          // TODO(ahe): Probably better to overwrite sources. Create a new
          // unit?
          // The server should push updates to the client.
        }
        return unit;
      });
    }
    future.then((CompilationUnit unit) {
      mainEditorPane
          ..contentEditable = 'true'
          ..nodes.clear();
      observer.takeRecords(); // Discard mutations.

      transitionToInitialState();
      context.currentCompilationUnit = unit;

      // Install the code, which will trigger a call to onMutation.
      mainEditorPane.appendText(unit.content);
    });
  }

  void transitionToInitialState() {}

  void onProjectFileFsEvent(MessageEvent e) {
    Map map = JSON.decode(e.data);
    List modified = map['modify'];
    if (modified == null) return;
    for (String name in modified) {
      if (context.lastSaved != null && context.lastSaved.name == name) {
        context.lastSaved = null;
        continue;
      }
      if (context.currentCompilationUnit.name == name) {
        mainEditorPane.contentEditable = 'false';
        statusDiv.text = 'Modified on disk';
      }
    }
  }
}

Future<String> getString(uri) {
  return new Future<String>.sync(() => HttpRequest.getString('$uri'));
}

class PendingInputState extends InitialState {
  PendingInputState(InteractionContext context)
      : super(context);

  void onInput(Event event) {
    // Do nothing.
  }

  void onMutation(List<MutationRecord> mutations, MutationObserver observer) {
    super.onMutation(mutations, observer);

    InteractionState nextState = new InitialState(context);
    if (settings.enableCodeCompletion.value) {
      Element parent = editor.getElementAtSelection();
      Element ui;
      if (parent != null) {
        ui = parent.querySelector('.dart-code-completion');
        if (ui != null) {
          nextState = new CodeCompletionState(context, parent, ui);
        }
      }
    }
    state = nextState;
  }
}

class CodeCompletionState extends InitialState {
  final Element activeCompletion;
  final Element ui;
  int minWidth = 0;
  DivElement staticResults;
  SpanElement inline;
  DivElement serverResults;
  String inlineSuggestion;

  CodeCompletionState(InteractionContext context,
                      this.activeCompletion,
                      this.ui)
      : super(context);

  void onInput(Event event) {
    // Do nothing.
  }

  void onModifiedKeyUp(KeyboardEvent event) {
    // TODO(ahe): Handle DOWN (jump to server results).
  }

  void onUnmodifiedKeyUp(KeyboardEvent event) {
    switch (event.keyCode) {
      case KeyCode.DOWN:
        return moveDown(event);

      case KeyCode.UP:
        return moveUp(event);

      case KeyCode.ESC:
        event.preventDefault();
        return endCompletion();

      case KeyCode.TAB:
      case KeyCode.RIGHT:
      case KeyCode.ENTER:
        event.preventDefault();
        return endCompletion(acceptSuggestion: true);
    }
  }

  void moveDown(Event event) {
    event.preventDefault();
    move(1);
  }

  void moveUp(Event event) {
    event.preventDefault();
    move(-1);
  }

  void move(int direction) {
    Element element = editor.moveActive(direction, ui);
    if (element == null) return;
    var text = activeCompletion.firstChild;
    String prefix = "";
    if (text is Text) prefix = text.data.trim();
    updateInlineSuggestion(prefix, element.text);
  }

  void endCompletion({bool acceptSuggestion: false}) {
    if (acceptSuggestion) {
      suggestionAccepted();
    }
    activeCompletion.classes.remove('active');
    mainEditorPane.querySelectorAll('.hazed-suggestion')
        .forEach((e) => e.remove());
    // The above changes create mutation records. This implicitly fire mutation
    // events that result in saving the source code in local storage.
    // TODO(ahe): Consider making this more explicit.
    state = new InitialState(context);
  }

  void suggestionAccepted() {
    if (inlineSuggestion != null) {
      Text text = new Text(inlineSuggestion);
      activeCompletion.replaceWith(text);
      window.getSelection().collapse(text, inlineSuggestion.length);
    }
  }

  void onMutation(List<MutationRecord> mutations, MutationObserver observer) {
    for (MutationRecord record in mutations) {
      if (!activeCompletion.contains(record.target)) {
        endCompletion();
        return super.onMutation(mutations, observer);
      }
    }

    var text = activeCompletion.firstChild;
    if (text is! Text) return endCompletion();
    updateSuggestions(text.data.trim());
  }

  void onStateChanged(InteractionState previous) {
    super.onStateChanged(previous);
    displayCodeCompletion();
  }

  void displayCodeCompletion() {
    Selection selection = window.getSelection();
    if (selection.anchorNode is! Text) {
      return endCompletion();
    }
    Text text = selection.anchorNode;
    if (!activeCompletion.contains(text)) {
      return endCompletion();
    }

    int anchorOffset = selection.anchorOffset;

    String prefix = text.data.substring(0, anchorOffset).trim();
    if (prefix.isEmpty) {
      return endCompletion();
    }

    num height = activeCompletion.getBoundingClientRect().height;
    activeCompletion.classes.add('active');
    Node root = getShadowRoot(ui);

    inline = new SpanElement()
        ..classes.add('hazed-suggestion');
    Text rest = text.splitText(anchorOffset);
    text.parentNode.insertBefore(inline, text.nextNode);
    activeCompletion.parentNode.insertBefore(
        rest, activeCompletion.nextNode);

    staticResults = new DivElement()
        ..classes.addAll(['dart-static', 'dart-limited-height']);
    serverResults = new DivElement()
        ..style.display = 'none'
        ..classes.add('dart-server');
    root.nodes.addAll([staticResults, serverResults]);
    ui.style.top = '${height}px';

    staticResults.nodes.add(buildCompletionEntry(prefix));

    updateSuggestions(prefix);
  }

  void updateInlineSuggestion(String prefix, String suggestion) {
    inlineSuggestion = suggestion;

    minWidth = max(minWidth, activeCompletion.getBoundingClientRect().width);

    activeCompletion.style
        ..display = 'inline-block'
        ..minWidth = '${minWidth}px';

    setShadowRoot(inline, suggestion.substring(prefix.length));
    inline.style.display = '';

    observer.takeRecords(); // Discard mutations.
  }

  void updateSuggestions(String prefix) {
    if (prefix.isEmpty) {
      return endCompletion();
    }

    Token first = tokenize(prefix);
    for (Token token = first; token.kind != EOF_TOKEN; token = token.next) {
      String tokenInfo = token.info.value;
      if (token != first ||
          tokenInfo != 'identifier' &&
          tokenInfo != 'keyword') {
        return endCompletion();
      }
    }

    var borderHeight = 2; // 1 pixel border top & bottom.
    num height = ui.getBoundingClientRect().height - borderHeight;
    ui.style.minHeight = '${height}px';

    minWidth =
        max(minWidth, activeCompletion.getBoundingClientRect().width);

    staticResults.nodes.clear();
    serverResults.nodes.clear();

    if (inlineSuggestion != null && inlineSuggestion.startsWith(prefix)) {
      setShadowRoot(inline, inlineSuggestion.substring(prefix.length));
    }

    List<String> results = editor.seenIdentifiers.where(
        (String identifier) {
          return identifier != prefix && identifier.startsWith(prefix);
        }).toList(growable: false);
    results.sort();
    if (results.isEmpty) results = <String>[prefix];

    results.forEach((String completion) {
      staticResults.nodes.add(buildCompletionEntry(completion));
    });

    if (settings.enableDartMind) {
      // TODO(ahe): Move this code to its own function or class.
      String encodedArg0 = Uri.encodeComponent('"$prefix"');
      String mindQuery =
          'http://dart-mind.appspot.com/rpc'
          '?action=GetExportingPubCompletions'
          '&arg0=$encodedArg0';
      try {
        var serverWatch = new Stopwatch()..start();
        HttpRequest.getString(mindQuery).then((String responseText) {
          serverWatch.stop();
          List<String> serverSuggestions = JSON.decode(responseText);
          if (!serverSuggestions.isEmpty) {
            updateInlineSuggestion(prefix, serverSuggestions.first);
          }
          var root = getShadowRoot(ui);
          for (int i = 1; i < serverSuggestions.length; i++) {
            String completion = serverSuggestions[i];
            DivElement where = staticResults;
            int index = results.indexOf(completion);
            if (index != -1) {
              List<Element> entries = root.querySelectorAll(
                  '.dart-static>.dart-entry');
              entries[index].classes.add('doubleplusgood');
            } else {
              if (results.length > 3) {
                serverResults.style.display = 'block';
                where = serverResults;
              }
              Element entry = buildCompletionEntry(completion);
              entry.classes.add('doubleplusgood');
              where.nodes.add(entry);
            }
          }
          serverResults.appendHtml(
              '<div>${serverWatch.elapsedMilliseconds}ms</div>');
          // Discard mutations.
          observer.takeRecords();
        }).catchError((error, stack) {
          window.console.dir(error);
          window.console.error('$stack');
        });
      } catch (error, stack) {
        window.console.dir(error);
        window.console.error('$stack');
      }
    }
    // Discard mutations.
    observer.takeRecords();
  }

  Element buildCompletionEntry(String completion) {
    return new DivElement()
        ..classes.add('dart-entry')
        ..appendText(completion);
  }

  void transitionToInitialState() {
    endCompletion();
  }
}

Token tokenize(String text) {
  var file = new StringSourceFile('', text);
  return new StringScanner(file, includeComments: true).tokenize();
}

bool computeHasModifier(KeyboardEvent event) {
  return
      event.getModifierState("Alt") ||
      event.getModifierState("AltGraph") ||
      event.getModifierState("CapsLock") ||
      event.getModifierState("Control") ||
      event.getModifierState("Fn") ||
      event.getModifierState("Meta") ||
      event.getModifierState("NumLock") ||
      event.getModifierState("ScrollLock") ||
      event.getModifierState("Scroll") ||
      event.getModifierState("Win") ||
      event.getModifierState("Shift") ||
      event.getModifierState("SymbolLock") ||
      event.getModifierState("OS");
}

String tokenizeAndHighlight(String line,
                            String state,
                            int start,
                            TrySelection trySelection,
                            List<Node> nodes) {
  String newState = '';
  int offset = state.length;
  int adjustedStart = start - state.length;

  //   + offset  + charOffset  + globalOffset   + (charOffset + charCount)
  //   v         v             v                v
  // do          identifier_abcdefghijklmnopqrst
  for (Token token = tokenize('$state$line');
       token.kind != EOF_TOKEN;
       token = token.next) {
    int charOffset = token.charOffset;
    int charCount = token.charCount;

    Token tokenToDecorate = token;
    if (token is UnterminatedToken && isUnterminatedMultiLineToken(token)) {
      newState += '${token.start}';
      continue; // This might not be an error.
    } else {
      Token follow = token.next;
      if (token is BeginGroupToken && token.endGroup != null) {
        follow = token.endGroup.next;
      }
      if (follow is ErrorToken && follow.charOffset == token.charOffset) {
        if (follow is UnmatchedToken) {
          newState += '${follow.begin.value}';
        } else {
          tokenToDecorate = follow;
        }
      }
    }

    if (charOffset < offset) {
      // Happens for scanner errors, or for the [state] prefix.
      continue;
    }

    Decoration decoration = editor.getDecoration(tokenToDecorate);

    if (decoration == null) continue;

    // Add a node for text before current token.
    trySelection.addNodeFromSubstring(
        adjustedStart + offset, adjustedStart + charOffset, nodes);

    // Add a node for current token.
    trySelection.addNodeFromSubstring(
        adjustedStart + charOffset,
        adjustedStart + charOffset + charCount, nodes, decoration);

    offset = charOffset + charCount;
  }

  // Add a node for anything after the last (decorated) token.
  trySelection.addNodeFromSubstring(
      adjustedStart + offset, start + line.length, nodes);

  return newState;
}

bool isUnterminatedMultiLineToken(UnterminatedToken token) {
  return
      token.start == '/*' ||
      token.start == "'''" ||
      token.start == '"""' ||
      token.start == "r'''" ||
      token.start == 'r"""';
}

void normalizeMutationRecord(MutationRecord record,
                             TrySelection selection,
                             Set<Node> normalizedNodes) {
  for (Node node in record.addedNodes) {
    if (node.parent == null) continue;
    StringBuffer buffer = new StringBuffer();
    int selectionOffset = htmlToText(node, buffer, selection);
    Text newNode = new Text('$buffer');
    node.replaceWith(newNode);
    normalizedNodes.add(findLine(newNode));
    if (selectionOffset != -1) {
      selection.anchorNode = newNode;
      selection.anchorOffset = selectionOffset;
    }
  }
  if (!record.removedNodes.isEmpty) {
    normalizedNodes.add(findLine(record.target));
  }
  if (record.type == "characterData") {
    normalizedNodes.add(findLine(record.target));
  }
}

// Finds the line of [node] (a parent node with CSS class 'lineNumber').
// If no such parent exists, return mainEditorPane if it is a parent.
// Otherwise return [node].
Node findLine(Node node) {
  for (Node n = node; n != null; n = n.parent) {
    if (n is Element && n.classes.contains('lineNumber')) return n;
    if (n == mainEditorPane) return n;
  }
  return node;
}

Element makeLine(List<Node> lineNodes, String state) {
  return new SpanElement()
      ..setAttribute('dart-state', state)
      ..nodes.addAll(lineNodes)
      ..classes.add('lineNumber');
}

bool isAtEndOfFile(Text text, int offset) {
  Node line = findLine(text);
  return
      line.nextNode == null &&
      text.parent.nextNode == null &&
      offset == text.length;
}

List<String> splitLines(String text) {
  return text.split(new RegExp('^', multiLine: true));
}

void removeCodeCompletion() {
  List<Node> highlighting =
      mainEditorPane.querySelectorAll('.dart-code-completion');
  for (Element element in highlighting) {
    element.remove();
  }
}
