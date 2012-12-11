// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file

library UnknownElementTest;
import '../../pkg/unittest/lib/unittest.dart';
import '../../pkg/unittest/lib/html_config.dart';
import 'dart:html';

main() {
  useHtmlConfiguration();

  var isUnknownElement =
      predicate((x) => x is UnknownElement, 'is an UnknownELement');

  var foo = new Element.tag('foo');
  foo.id = 'foo';
  var bar = new Element.tag('bar');
  bar.id = 'bar';
  document.body.nodes.addAll([foo, bar]);

  test('type-check', () {
      expect(foo, isUnknownElement);
      expect(bar, isUnknownElement);
      expect(query('#foo'), equals(foo));
      expect(query('#bar'), equals(bar));
    });

  test('dispatch-fail', () {
      expect(() => foo.method1(), throwsNoSuchMethodError);
      expect(() => foo.field1, throwsNoSuchMethodError);
      expect(() { foo.field1 = 42; }, throwsNoSuchMethodError);
    });

  test('dispatch', () {
      dispatch(element, name, args) {
        if (element.xtag == null) {
          element.xtag = new Map();
        }
        var map = element.xtag;

        // FIXME: Remove once VM and Dart2JS converge.
        name = name.replaceFirst(' ', ':');
        switch (element.tagName.toLowerCase()) {
          case 'foo':
            switch (name) {
              case 'get:x':
                return 42;
              case 'baz':
                return '${element.id} - ${args[0]}';
            }
            break;
          case 'bar':
            switch (name) {
              case 'get:y':
                return map['y'];
              case 'set:y=':
                map['y'] = args[0];
                return;
            }
            break;
        }
        throw new NoSuchMethodError(element, name, args, {});
      }
      dynamicUnknownElementDispatcher = dispatch;

      expect(foo.x, equals(42));
      expect(() { foo.x = 7; }, throwsNoSuchMethodError);
      expect(foo.id, equals('foo'));
      expect(() => bar.x, throwsNoSuchMethodError);
      bar.y = 11;
      expect(bar.y, equals(11));
      expect(foo.baz('hello'), equals('foo - hello'));
    });
}
