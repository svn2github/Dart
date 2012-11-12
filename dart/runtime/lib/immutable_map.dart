// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// Immutable map class for compiler generated map literals.

class ImmutableMap<K, V> implements Map<K, V> {
  final _ImmutableArray kvPairs_;

  const ImmutableMap._create(_ImmutableArray keyValuePairs)
      : kvPairs_ = keyValuePairs;


  V operator [](K key) {
    // TODO(hausner): Since the keys are sorted, we could do a binary
    // search. But is it worth it?
    for (int i = 0; i < kvPairs_.length - 1; i += 2) {
      if (key == kvPairs_[i]) {
        return kvPairs_[i+1];
      }
    }
    return null;
  }

  bool get isEmpty {
    return kvPairs_.length == 0;
  }

  int get length {
    return kvPairs_.length ~/ 2;
  }

  void forEach(void f(K key, V value)) {
    for (int i = 0; i < kvPairs_.length; i += 2) {
      f(kvPairs_[i], kvPairs_[i+1]);
    }
  }

  Collection<K> get keys {
    int numKeys = length;
    List<K> list = new List<K>(numKeys);
    for (int i = 0; i < numKeys; i++) {
      list[i] = kvPairs_[i*2];
    }
    return list;
  }

  Collection<V> get values {
    int numValues = length;
    List<V> list = new List<V>(numValues);
    for (int i = 0; i < numValues; i++) {
      list[i] = kvPairs_[i*2 + 1];
    }
    return list;
  }

  bool containsKey(K key) {
    for (int i = 0; i < kvPairs_.length; i += 2) {
      if (key == kvPairs_[i]) {
        return true;
      }
    }
    return false;
  }

  bool containsValue(V value) {
    for (int i = 1; i < kvPairs_.length; i += 2) {
      if (value == kvPairs_[i]) {
        return true;
      }
    }
    return false;
  }

  void operator []=(K key, V value) {
    throw new UnsupportedError("Cannot set value in unmodifiable Map");
  }

  V putIfAbsent(K key, V ifAbsent()) {
    throw new UnsupportedError("Cannot set value in unmodifiable Map");
  }

  void clear() {
    throw new UnsupportedError("Cannot clear unmodifiable Map");
  }

  V remove(K key) {
    throw new UnsupportedError("Cannot remove from unmodifiable Map");
  }

  String toString() {
    return Maps.mapToString(this);
  }
}

