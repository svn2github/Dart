library java.core;

import "dart:math" as math;
import "dart:io";
import "dart:uri";

class System {
  static final String pathSeparator = Platform.pathSeparator;
  static final int pathSeparatorChar = Platform.pathSeparator.codeUnitAt(0);

  static int currentTimeMillis() {
    return (new DateTime.now()).millisecondsSinceEpoch;
  }
  static String getProperty(String name) {
    if (name == 'os.name') {
      return Platform.operatingSystem;
    }
    if (name == 'line.separator') {
      if (Platform.operatingSystem == 'windows') {
        return '\r\n';
      }
      return '\n';
    }
    return null;
  }
  static String getenv(String name) => Platform.environment[name];

}

/**
 * Limited implementation of "o is instanceOfType", see
 * http://code.google.com/p/dart/issues/detail?id=8184
 */
bool isInstanceOf(o, Type t) {
  if (o == null) {
    return false;
  }
  if (o.runtimeType == t) {
    return true;
  }
  String oTypeName = o.runtimeType.toString();
  String tTypeName = t.toString();
  if (oTypeName == tTypeName) {
    return true;
  }
  if (oTypeName == "${tTypeName}Impl") {
    return true;
  }
  if (oTypeName.startsWith("_HashMap") && tTypeName == "Map") {
    return true;
  }
  if (oTypeName.startsWith("List") && tTypeName == "List") {
    return true;
  }
  return false;
}

class JavaArrays {
  static bool equals(List a, List b) {
    if (a.length != b.length) {
      return false;
    }
    var len = a.length;
    for (int i = 0; i < len; i++) {
      if (a[i] != b[i]) {
        return false;
      }
    }
    return true;
  }
  static int makeHashCode(List a) {
    if (a == null) {
      return 0;
    }
    int result = 1;
    for (var element in a) {
      result = 31 * result + (element == null ? 0 : element.hashCode);
    }
    return result;
  }
  static List asList(List list) => list;
}

class Character {
  static const int MAX_VALUE = 0xffff;
  static const int MAX_CODE_POINT = 0x10ffff;
  static bool isLetter(int c) {
    return c >= 0x41 && c <= 0x5A || c >= 0x61 && c <= 0x7A;
  }
  static bool isLetterOrDigit(int c) {
    return isLetter(c) || c >= 0x30 && c <= 0x39;
  }
  static int digit(int codePoint, int radix) {
    if (radix != 16) {
      throw new ArgumentError("only radix == 16 is supported");
    }
    if (0x30 <= codePoint && codePoint <= 0x39) {
      return codePoint - 0x30;
    }
    if (0x41 <= codePoint && codePoint <= 0x46) {
      return 0xA + (codePoint - 0x41);
    }
  }
  static String toChars(int codePoint) {
    throw new UnsupportedOperationException();
  }
}

class CharBuffer {
  final String _content;
  CharBuffer(this._content);
  static CharBuffer wrap(String content) => new CharBuffer(content);
  int charAt(int index) => _content.codeUnitAt(index);
  int length() => _content.length;
  String subSequence(int start, int end) => _content.substring(start, end);
}

class JavaString {
  static String format(String fmt, List args) {
    return fmt;
  }
}

/**
 * Very limited printf implementation, supports only %s and %d.
 */
String _printf(String fmt, List args) {
  StringBuffer sb = new StringBuffer();
  bool markFound = false;
  int argIndex = 0;
  for (int i = 0; i < fmt.length; i++) {
    int c = fmt.codeUnitAt(i);
    if (c == 0x25) {
      if (markFound) {
        sb.addCharCode(c);
        markFound = false;
      } else {
        markFound = true;
      }
      continue;
    }
    if (markFound) {
      markFound = false;
      // %d
      if (c == 0x64) {
        sb.add(args[argIndex++]);
        continue;
      }
      // %s
      if (c == 0x73) {
        sb.add(args[argIndex++]);
        continue;
      }
      // unknown
      throw new IllegalArgumentException('[$fmt][$i] = 0x${c.toRadixString(16)}');
    } else {
      sb.addCharCode(c);
    }
  }
  return sb.toString();
}

abstract class PrintWriter {
  void print(x);

  void println() {
    this.print('\n');
  }

  void printlnObject(String s) {
    this.print(s);
    this.println();
  }

  void printf(String fmt, List args) {
    this.print(_printf(fmt, args));
  }
}

class PrintStringWriter extends PrintWriter {
  final StringBuffer _sb = new StringBuffer();

  void print(x) {
    _sb.add(x);
  }

  String toString() => _sb.toString();
}

class StringUtils {
  static List<String> split(String s, String pattern) => s.split(pattern);
  static String replace(String s, String from, String to) => s.replaceAll(from, to);
  static String repeat(String s, int n) {
    StringBuffer sb = new StringBuffer();
    for (int i = 0; i < n; i++) {
      sb.add(s);
    }
    return sb.toString();
  }
}

class Math {
  static num max(num a, num b) => math.max(a, b);
  static num min(num a, num b) => math.min(a, b);
}

class RuntimeException implements Exception {
  String toString() => "RuntimeException";
}

class JavaException implements Exception {
  final String message;
  final Exception e;
  JavaException([this.message = "", this.e = null]);
  JavaException.withCause(this.e) : message = null;
  String toString() => "JavaException: $message $e";
}

class IllegalArgumentException implements Exception {
  final String message;
  const IllegalArgumentException([this.message = "", Exception e = null]);
  String toString() => "IllegalStateException: $message";
}

class IllegalStateException implements Exception {
  final String message;
  const IllegalStateException([this.message = ""]);
  String toString() => "IllegalStateException: $message";
}

class UnsupportedOperationException implements Exception {
  String toString() => "UnsupportedOperationException";
}

class NumberFormatException implements Exception {
  String toString() => "NumberFormatException";
}

class URISyntaxException implements Exception {
  String toString() => "URISyntaxException";
}

class IOException implements Exception {
  String toString() => "IOException";
}

class ListWrapper<E> extends Collection<E> implements List<E> {
  List<E> elements = new List<E>();

  Iterator<E> get iterator {
    return elements.iterator;
  }

  E operator [](int index) {
    return elements[index];
  }

  void operator []=(int index, E value) {
    elements[index] = value;
  }

  void set length(int newLength) {
    elements.length = newLength;
  }

  void add(E value) {
    elements.add(value);
  }

  void addLast(E value) {
    elements.addLast(value);
  }

  void addAll(Iterable<E> iterable) {
    elements.addAll(iterable);
  }

  void sort([int compare(E a, E b)]) {
    elements.sort(compare);
  }

  int indexOf(E element, [int start = 0]) {
    return elements.indexOf(element, start);
  }

  int lastIndexOf(E element, [int start]) {
    return elements.lastIndexOf(element, start);
  }

  void clear() {
    elements.clear();
  }

  void remove(Object element) {
    return elements.remove(element);
  }

  E removeAt(int index) {
    return elements.removeAt(index);
  }

  E removeLast() {
    return elements.removeLast();
  }

  Iterable<E> get reversed => elements.reversed;

  List<E> getRange(int start, int length) {
    return elements.getRange(start, length);
  }

  void setRange(int start, int length, List<E> from, [int startFrom]) {
    elements.setRange(start, length, from, startFrom);
  }

  void removeRange(int start, int length) {
    elements.removeRange(start, length);
  }

  void insertRange(int start, int length, [E fill]) {
    elements.insertRange(start, length, fill);
  }
}

class JavaIterator<E> {
  Collection<E> _collection;
  List<E> _elements = new List<E>();
  int _coPos = 0;
  int _elPos = 0;
  E _current = null;
  JavaIterator(this._collection) {
    Iterator iterator = _collection.iterator;
    while (iterator.moveNext()) {
      _elements.add(iterator.current);
    }
  }

  bool get hasNext {
    return _elPos < _elements.length;
  }

  E next() {
    _current = _elements[_elPos];
    _coPos++;
    _elPos++;
    return _current;
  }

  void remove() {
    if (_collection is List) {
      _coPos--;
      (_collection as List).remove(_coPos);
    } else if (_collection is Set) {
      _collection.remove(_current);
    } else {
      throw new StateError("Unsupported collection ${_collection.runtimeType}");
    }
  }
}

class MapEntry<K, V> {
  K _key;
  V _value;
  MapEntry(this._key, this._value);
  K getKey() => _key;
  V getValue() => _value;
}

Set<MapEntry> getMapEntrySet(Map m) {
  Set<MapEntry> result = new Set();
  m.forEach((k, v) {
    result.add(new MapEntry(k, v));
  });
  return result;
}

bool javaSetAdd(Set s, o) {
  if (!s.contains(o)) {
    s.add(o);
    return true;
  }
  return false;
}

void javaMapPutAll(Map target, Map source) {
  source.forEach((k, v) {
    target[k] = v;
  });
}

File newRelativeFile(File base, String child) {
  var childPath = new Path(base.fullPathSync()).join(new Path(child));
  return new File.fromPath(childPath);
}

File newFileFromUri(Uri uri) {
  return new File(uri.path);
}

File getAbsoluteFile(File file) {
  var path = file.fullPathSync();
  return new File(path);
}

Uri newUriFromFile(File file) {
  return new Uri.fromComponents(path: file.fullPathSync());
}
