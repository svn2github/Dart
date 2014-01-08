library java.engine;

import 'java_core.dart';

class StringUtilities {
  static const String EMPTY = '';
  static const List<String> EMPTY_ARRAY = const <String> [];
  static String intern(String s) => s;
  static bool isTagName(String s) {
    if (s == null || s.length == 0) {
      return false;
    }
    int sz = s.length;
    for (int i = 0; i < sz; i++) {
      int c = s.codeUnitAt(i);
      if (!Character.isLetter(c)) {
        if (i == 0) {
          return false;
        }
        if (!Character.isDigit(c) && c != 0x2D) {
          return false;
        }
      }
    }
    return true;
  }
  static String substringBefore(String str, String separator) {
    if (str == null || str.isEmpty) {
      return str;
    }
    int pos = str.indexOf(separator);
    if (pos < 0) {
      return str;
    }
    return str.substring(0, pos);
  }
}

class FileNameUtilities {
  static String getExtension(String fileName) {
    if (fileName == null) {
      return "";
    }
    int index = fileName.lastIndexOf('.');
    if (index >= 0) {
      return fileName.substring(index + 1);
    }
    return "";
  }
}

class ArrayUtils {
  static List addAll(List target, List source) {
    List result = new List.from(target);
    result.addAll(source);
    return result;
  }
}

class ObjectUtilities {
  static int combineHashCodes(int first, int second) => first * 31 + second;
}

class UUID {
  static int __nextId = 0;
  final String id;
  UUID(this.id);
  String toString() => id;
  static UUID randomUUID() => new UUID((__nextId).toString());
}
