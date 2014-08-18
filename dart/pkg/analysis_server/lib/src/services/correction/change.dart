// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library services.correction.change;

import 'package:analysis_server/src/services/constants.dart';
import 'package:analysis_server/src/services/json.dart';
import 'package:analyzer/src/generated/source.dart';


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
  final List<FileEdit> fileEdits = <FileEdit>[];

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
  void addEdit(String file, Edit edit) {
    FileEdit fileEdit = getFileEdit(file);
    if (fileEdit == null) {
      fileEdit = new FileEdit(file);
      addFileEdit(fileEdit);
    }
    fileEdit.add(edit);
  }

  /**
   * Adds the given [FileEdit].
   */
  void addFileEdit(FileEdit edit) {
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
  FileEdit getFileEdit(String file) {
    for (FileEdit fileEdit in fileEdits) {
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
 * A description of a single change to a single file. 
 */
class Edit implements HasToJson {
  /**
   * The offset of the region to be modified. 
   */
  final int offset;

  /**
   * The length of the region to be modified.
   */
  final int length;

  /**
   * The text that is to replace the specified region in the original text. 
   */
  final String replacement;

  /**
   * An identifier that uniquely identifies this source edit from other edits in
   * the same response. This field is omitted unless a containing structure
   * needs to be able to identify the edit for some reason.
   *
   * For example, some refactoring operations can produce edits that might not
   * be appropriate (referred to as potential edits). Such edits will have an id
   * so that they can be referenced. Edits in the same response that do not need
   * to be referenced will not have an id.
   */
  String id;

  Edit(this.offset, this.length, this.replacement);

  Edit.range(SourceRange range, String replacement)
      : this(range.offset, range.length, replacement);

  /**
   * The offset of a character immediately after the region to be modified. 
   */
  int get end => offset + length;

  bool operator ==(other) {
    if (other is Edit) {
      return other.offset == offset &&
          other.length == length &&
          other.replacement == replacement;
    }
    return false;
  }

  /**
   * Get the result of applying the edit to the given [code].
   */
  String apply(String code) {
    return code.substring(0, offset) + replacement + code.substring(end);
  }

  @override
  Map<String, Object> toJson() {
    return {
      OFFSET: offset,
      LENGTH: length,
      REPLACEMENT: replacement
    };
  }

  @override
  String toString() {
    StringBuffer sb = new StringBuffer();
    sb.write('Edit(offset=');
    sb.write(offset);
    sb.write(', length=');
    sb.write(length);
    sb.write(', replacement=:>');
    sb.write(replacement);
    sb.write('<:');
    if (id != null) {
      sb.write(', id=');
      sb.write(id);
    }
    sb.write(')');
    return sb.toString();
  }

  /**
   * Get the result of applying a set of [edits] to the given [code].  Edits
   * are applied in the order they appear in [edits].
   */
  static String applySequence(String code, Iterable<Edit> edits) {
    edits.forEach((Edit edit) {
      code = edit.apply(code);
    });
    return code;
  }
}


/**
 * A description of a set of changes to a single file.
 *
 * [Edit]s are added in the order of decreasing offset, so they are easy to
 * apply to the original file content without correcting offsets.
 */
class FileEdit implements HasToJson {
  /**
   * The file to be modified.
   */
  final String file;

  /**
   * A list of the [Edit]s used to effect the change. 
   */
  final List<Edit> edits = <Edit>[];

  FileEdit(this.file);

  /**
   * Adds the given [Edit] to the list.
   */
  void add(Edit edit) {
    int index = 0;
    while (index < edits.length && edits[index].offset > edit.offset) {
      index++;
    }
    edits.insert(index, edit);
  }

  /**
   * Adds the given [Edit]s.
   */
  void addAll(Iterable<Edit> edits) {
    edits.forEach(add);
  }

  @override
  Map<String, Object> toJson() {
    return {
      FILE: file,
      EDITS: objectToJson(edits)
    };
  }

  @override
  String toString() => "FileEdit(file=$file, edits=$edits)";
}


/**
 * A group of linked [Position]s in multiple files that are simultaneously
 * modified - if one gets edited, all other positions in a group are edited the
 * same way. All linked positions in a group have the same content.
 */
class LinkedEditGroup implements HasToJson {
  final String id;
  int length;
  final List<Position> positions = <Position>[];
  final List<LinkedEditSuggestion> suggestions = <LinkedEditSuggestion>[];

  LinkedEditGroup(this.id);

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
      ID: id,
      LENGTH: length,
      POSITIONS: objectToJson(positions),
      SUGGESTIONS: objectToJson(suggestions)
    };
  }

  @override
  String toString() =>
      'LinkedEditGroup(id=$id, length=$length, '
          'positions=$positions, suggestions=$suggestions)';
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


/**
 * A position in a file.
 */
class Position implements HasToJson {
  final String file;
  final int offset;

  Position(this.file, this.offset);

  int get hashCode {
    int hash = file.hashCode;
    hash = hash * 31 + offset;
    return hash;
  }

  bool operator ==(other) {
    if (other is Position) {
      return other.file == file && other.offset == offset;
    }
    return false;
  }

  @override
  Map<String, Object> toJson() {
    return {
      FILE: file,
      OFFSET: offset
    };
  }

  @override
  String toString() => 'Position(file=$file, offset=$offset)';
}
