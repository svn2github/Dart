// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library services.correction.change;

import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/protocol2.dart';
import 'package:analysis_server/src/services/json.dart';


/**
 * A description of a single change to one or more files. 
 */
class Change implements HasToJson {
  /**
   * A textual description of the change to be applied. 
   */
  final String message;

  /**
   * A list of the [FileEdit]s used to effect the change. 
   */
  final List<SourceFileEdit> fileEdits = <SourceFileEdit>[];

  /**
   * A list of the [LinkedEditGroup]s in the change. 
   */
  final List<LinkedEditGroup> linkedEditGroups = <LinkedEditGroup>[];

  /**
   * The position that should be selected after the edits have been applied.
   */
  Position selection;

  Change(this.message);

  /**
   * Adds [edit] to the [FileEdit] for the given [file].
   */
  void addEdit(String file, SourceEdit edit) {
    SourceFileEdit fileEdit = getFileEdit(file);
    if (fileEdit == null) {
      fileEdit = new SourceFileEdit(file, <SourceEdit>[]);
      addFileEdit(fileEdit);
    }
    fileEdit.add(edit);
  }

  /**
   * Adds the given [FileEdit].
   */
  void addFileEdit(SourceFileEdit edit) {
    fileEdits.add(edit);
  }

  /**
   * Adds the given [LinkedEditGroup].
   */
  void addLinkedEditGroup(LinkedEditGroup linkedEditGroup) {
    linkedEditGroups.add(linkedEditGroup);
  }

  /**
   * Returns the [FileEdit] for the given [file], maybe `null`.
   */
  SourceFileEdit getFileEdit(String file) {
    for (SourceFileEdit fileEdit in fileEdits) {
      if (fileEdit.file == file) {
        return fileEdit;
      }
    }
    return null;
  }

  @override
  Map<String, Object> toJson() {
    Map<String, Object> json = {
      MESSAGE: message,
      EDITS: objectToJson(fileEdits),
      LINKED_EDIT_GROUPS: objectToJson(linkedEditGroups)
    };
    if (selection != null) {
      json[SELECTION] = selection.toJson();
    }
    return json;
  }

  @override
  String toString() =>
      'Change(message=$message, edits=$fileEdits, '
          'linkedEditGroups=$linkedEditGroups, selection=$selection)';
}


/**
 * A group of linked [Position]s in multiple files that are simultaneously
 * modified - if one gets edited, all other positions in a group are edited the
 * same way. All linked positions in a group have the same content.
 */
class LinkedEditGroup implements HasToJson {
  int length;
  final List<Position> positions = <Position>[];
  final List<LinkedEditSuggestion> suggestions = <LinkedEditSuggestion>[];

  void addPosition(Position position, int length) {
    positions.add(position);
    this.length = length;
  }

  void addSuggestion(LinkedEditSuggestion suggestion) {
    suggestions.add(suggestion);
  }

  @override
  Map<String, Object> toJson() {
    return {
      LENGTH: length,
      POSITIONS: objectToJson(positions),
      SUGGESTIONS: objectToJson(suggestions)
    };
  }

  @override
  String toString() =>
      'LinkedEditGroup(length=$length, positions=$positions, suggestions=$suggestions)';
}


/**
 * A suggestion of a value that could be used to replace all of the linked edit
 * regions in a [LinkedEditGroup].
 */
class LinkedEditSuggestion implements HasToJson {
  final LinkedEditSuggestionKind kind;
  final String value;

  LinkedEditSuggestion(this.kind, this.value);

  bool operator ==(other) {
    if (other is LinkedEditSuggestion) {
      return other.kind == kind && other.value == value;
    }
    return false;
  }

  @override
  Map<String, Object> toJson() {
    return {
      KIND: kind.name,
      VALUE: value
    };
  }

  @override
  String toString() => '(kind=$kind, value=$value)';
}


/**
 * An enumeration of the kind of values that can be suggested for a linked edit.
 */
class LinkedEditSuggestionKind {
  static const METHOD = const LinkedEditSuggestionKind('METHOD');
  static const PARAMETER = const LinkedEditSuggestionKind('PARAMETER');
  static const TYPE = const LinkedEditSuggestionKind('TYPE');
  static const VARIABLE = const LinkedEditSuggestionKind('VARIABLE');
  final String name;

  const LinkedEditSuggestionKind(this.name);

  @override
  String toString() => name;
}
