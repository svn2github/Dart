// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library template_binding.test.utils;

import 'dart:html';
import 'package:observe/observe.dart';
import 'package:template_binding/template_binding.dart';
import 'package:unittest/unittest.dart';

import 'package:observe/src/microtask.dart';
export 'package:observe/src/microtask.dart';

final bool parserHasNativeTemplate = () {
  var div = new DivElement()..innerHtml = '<table><template>';
  return div.firstChild.firstChild != null &&
      div.firstChild.firstChild.tagName == 'TEMPLATE';
}();

recursivelySetTemplateModel(element, model, [delegate]) {
  for (var node in element.queryAll('*')) {
    if (isSemanticTemplate(node)) {
      templateBind(node)
          ..bindingDelegate = delegate
          ..model = model;
    }
  }
}

dispatchEvent(type, target) {
  target.dispatchEvent(new Event(type, cancelable: false));
}

class FooBarModel extends Observable {
  @observable var foo;
  @observable var bar;

  FooBarModel([this.foo, this.bar]);
}

@reflectable
class FooBarNotifyModel extends ChangeNotifier implements FooBarModel {
  var _foo;
  var _bar;

  FooBarNotifyModel([this._foo, this._bar]);

  get foo => _foo;
  set foo(value) {
    _foo = notifyPropertyChange(#foo, _foo, value);
  }

  get bar => _bar;
  set bar(value) {
    _bar = notifyPropertyChange(#bar, _bar, value);
  }
}

observeTest(name, testCase) => test(name, wrapMicrotask(testCase));

solo_observeTest(name, testCase) => solo_test(name, wrapMicrotask(testCase));

DivElement testDiv;

createTestHtml(s) {
  var div = new DivElement();
  div.setInnerHtml(s, treeSanitizer: new NullTreeSanitizer());
  testDiv.append(div);

  for (var node in div.queryAll('*')) {
    if (isSemanticTemplate(node)) TemplateBindExtension.decorate(node);
  }

  return div;
}

createShadowTestHtml(s) {
  var div = new DivElement();
  var root = div.createShadowRoot();
  root.setInnerHtml(s, treeSanitizer: new NullTreeSanitizer());
  testDiv.append(div);

  for (var node in root.querySelectorAll('*')) {
    if (isSemanticTemplate(node)) TemplateBindExtension.decorate(node);
  }

  return root;
}

/**
 * Sanitizer which does nothing.
 */
class NullTreeSanitizer implements NodeTreeSanitizer {
  void sanitizeTree(Node node) {}
}

unbindAll(node) {
  nodeBind(node).unbindAll();
  for (var child = node.firstChild; child != null; child = child.nextNode) {
    unbindAll(child);
  }
}
