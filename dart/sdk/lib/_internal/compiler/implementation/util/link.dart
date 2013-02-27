// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of org_dartlang_compiler_util;

class Link<T> extends Iterable<T> {
  T get head => null;
  Link<T> get tail => null;

  factory Link.fromList(List<T> list) {
    switch (list.length) {
      case 0:
        return new Link<T>();
      case 1:
        return new LinkEntry<T>(list[0]);
      case 2:
        return new LinkEntry<T>(list[0], new LinkEntry<T>(list[1]));
      case 3:
        return new LinkEntry<T>(
            list[0], new LinkEntry<T>(list[1], new LinkEntry<T>(list[2])));
    }
    Link link = new Link<T>();
    for (int i = list.length ; i > 0; i--) {
      link = link.prepend(list[i - 1]);
    }
    return link;
  }

  const Link();

  Link<T> prepend(T element) {
    return new LinkEntry<T>(element, this);
  }

  Iterator<T> get iterator => new LinkIterator<T>(this);

  void printOn(StringBuffer buffer, [separatedBy]) {
  }

  List toList({ bool growable: false }) => growable ? <T>[] : new List<T>(0);

  bool get isEmpty => true;

  Link<T> reverse() => this;

  Link<T> reversePrependAll(Link<T> from) {
    if (from.isEmpty) return this;
    return this.prepend(from.head).reversePrependAll(from.tail);
  }

  Link<T> skip(int n) {
    if (n == 0) return this;
    throw new RangeError('Index $n out of range');
  }

  void forEach(void f(T element)) {}

  bool operator ==(other) {
    if (other is !Link<T>) return false;
    return other.isEmpty;
  }

  String toString() => "[]";

  get length {
    throw new UnsupportedError('get:length');
  }

  int slowLength() => 0;
}

abstract class LinkBuilder<T> {
  factory LinkBuilder() = LinkBuilderImplementation;

  Link<T> toLink();
  void addLast(T t);

  final int length;
  final bool isEmpty;
}
