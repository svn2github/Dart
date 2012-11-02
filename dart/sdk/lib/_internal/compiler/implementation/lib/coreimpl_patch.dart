// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Patch file for dart:coreimpl classes.

// Patch for RegExp implementation.
// TODO(ager): Split out into regexp_patch.dart and allow #source in
// patch files?
patch class JSSyntaxRegExp {
  final String _pattern;
  final bool _multiLine;
  final bool _ignoreCase;

  patch const JSSyntaxRegExp(String pattern,
                             {bool multiLine: false,
                              bool ignoreCase: false})
      : _pattern = pattern,
        _multiLine = multiLine,
        _ignoreCase = ignoreCase;

  patch Match firstMatch(String str) {
    List<String> m = regExpExec(this, checkString(str));
    if (m == null) return null;
    var matchStart = regExpMatchStart(m);
    // m.lastIndex only works with flag 'g'.
    var matchEnd = matchStart + m[0].length;
    return new _MatchImplementation(pattern, str, matchStart, matchEnd, m);
  }

  patch bool hasMatch(String str) => regExpTest(this, checkString(str));

  patch String stringMatch(String str) {
    var match = firstMatch(str);
    return match == null ? null : match.group(0);
  }

  patch Iterable<Match> allMatches(String str) {
    checkString(str);
    return new _AllMatchesIterable(this, str);
  }

  patch String get pattern => _pattern;
  patch bool get multiLine => _multiLine;
  patch bool get ignoreCase => _ignoreCase;

  static JSSyntaxRegExp _globalVersionOf(JSSyntaxRegExp other) {
    JSSyntaxRegExp re = new JSSyntaxRegExp(other.pattern,
                                           multiLine: other.multiLine,
                                           ignoreCase: other.ignoreCase);
    regExpAttachGlobalNative(re);
    return re;
  }

  _getNative() => regExpGetNative(this);
}

class _MatchImplementation implements Match {
  final String pattern;
  final String str;
  final int start;
  final int end;
  final List<String> _groups;

  const _MatchImplementation(
      String this.pattern,
      String this.str,
      int this.start,
      int this.end,
      List<String> this._groups);

  String group(int index) => _groups[index];
  String operator [](int index) => group(index);
  int get groupCount => _groups.length - 1;

  List<String> groups(List<int> groups) {
    List<String> out = [];
    for (int i in groups) {
      out.add(group(i));
    }
    return out;
  }
}

class _AllMatchesIterable implements Iterable<Match> {
  final JSSyntaxRegExp _re;
  final String _str;

  const _AllMatchesIterable(this._re, this._str);

  Iterator<Match> iterator() => new _AllMatchesIterator(_re, _str);
}

class _AllMatchesIterator implements Iterator<Match> {
  final RegExp _re;
  final String _str;
  Match _next;
  bool _done;

  _AllMatchesIterator(JSSyntaxRegExp re, String this._str)
    : _done = false, _re = JSSyntaxRegExp._globalVersionOf(re);

  Match next() {
    if (!hasNext) {
      throw new StateError("No more elements");
    }

    // _next is set by [hasNext].
    var next = _next;
    _next = null;
    return next;
  }

  bool get hasNext {
    if (_done) {
      return false;
    } else if (_next != null) {
      return true;
    }

    // firstMatch actually acts as nextMatch because of
    // hidden global flag.
    _next = _re.firstMatch(_str);
    if (_next == null) {
      _done = true;
      return false;
    } else {
      return true;
    }
  }
}
