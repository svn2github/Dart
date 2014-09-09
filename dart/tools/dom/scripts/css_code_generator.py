#!/usr/bin/python
#
# Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
# for details. All rights reserved. Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

"""Generates CSSStyleDeclaration template file from css property definitions
defined in WebKit."""

import tempfile, os

COMMENT_LINE_PREFIX = '   * '
# TODO(efortuna): Pull from DEPS so that we have latest css *in sync* with our
# Dartium. Then remove the checked in CSSPropertyNames.in.
SOURCE_PATH = 'CSSPropertyNames.in'
#SOURCE_PATH = 'Source/WebCore/css/CSSPropertyNames.in'
TEMPLATE_FILE = '../templates/html/impl/impl_CSSStyleDeclaration.darttemplate'

# Supported annotations for any specific CSS properties.
annotated = {
  'transition': '''@SupportedBrowser(SupportedBrowser.CHROME)
  @SupportedBrowser(SupportedBrowser.FIREFOX)
  @SupportedBrowser(SupportedBrowser.IE, '10')
  @SupportedBrowser(SupportedBrowser.SAFARI)'''
}

def camelCaseName(name):
  """Convert a CSS property name to a lowerCamelCase name."""
  name = name.replace('-webkit-', '')
  words = []
  for word in name.split('-'):
    if words:
      words.append(word.title())
    else:
      words.append(word)
  return ''.join(words)

def GenerateCssTemplateFile():
  data = open(SOURCE_PATH).readlines()

  # filter CSSPropertyNames.in to only the properties
  # TODO(efortuna): do we also want CSSPropertyNames.in?
  data = [d[:-1] for d in data
          if len(d) > 1
          and not d.startswith('#')
          and not d.startswith('//')
          and not '=' in d]

  class_file = open(TEMPLATE_FILE, 'w')

  class_file.write("""
// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// WARNING: DO NOT EDIT THIS TEMPLATE FILE.
// The template file was generated by scripts/css_code_generator.py

// Source of CSS properties:
//   %s

part of $LIBRARYNAME;

$(ANNOTATIONS)$(NATIVESPEC)$(CLASS_MODIFIERS) class $CLASSNAME $EXTENDS with
    $(CLASSNAME)Base $IMPLEMENTS {
  factory $CLASSNAME() => new CssStyleDeclaration.css('');

  factory $CLASSNAME.css(String css) {
    final style = new Element.tag('div').style;
    style.cssText = css;
    return style;
  }

  String getPropertyValue(String propertyName) {
    var propValue = _getPropertyValueHelper(propertyName);
    return propValue != null ? propValue : '';
  }

  String _getPropertyValueHelper(String propertyName) {
    if (_supportsProperty(_camelCase(propertyName))) {
      return _getPropertyValue(propertyName);
    } else {
      return _getPropertyValue(Device.cssPrefix + propertyName);
    }
  }

  /**
   * Returns true if the provided *CSS* property name is supported on this
   * element.
   *
   * Please note the property name camelCase, not-hyphens. This
   * method returns true if the property is accessible via an unprefixed _or_
   * prefixed property.
   */
  bool supportsProperty(String propertyName) {
    return _supportsProperty(propertyName) ||
        _supportsProperty(_camelCase(Device.cssPrefix + propertyName));
  }

  bool _supportsProperty(String propertyName) {
$if DART2JS
    return JS('bool', '# in #', propertyName, this);
$else
    // You can't just check the value of a property, because there is no way
    // to distinguish between property not being present in the browser and
    // not having a value at all. (Ultimately we'll want the native method to
    // return null if the property doesn't exist and empty string if it's
    // defined but just doesn't have a value.
    return _hasProperty(propertyName);
$endif
  }
$if DARTIUM

  bool _hasProperty(String propertyName) =>
      _blink.BlinkCSSStyleDeclaration.$__propertyQuery___Callback_DOMString(this, propertyName);
$endif

  @DomName('CSSStyleDeclaration.setProperty')
  void setProperty(String propertyName, String value, [String priority]) {
    if (_supportsProperty(_camelCase(propertyName))) {
      return _setPropertyHelper(propertyName, value, priority);
    } else {
      return _setPropertyHelper(Device.cssPrefix + propertyName, value,
          priority);
    }
  }

  static String _camelCase(String hyphenated) {
$if DART2JS
    var replacedMs = JS('String', r'#.replace(/^-ms-/, "ms-")', hyphenated);

    var fToUpper = const JS_CONST(
        r'function(_, letter) { return letter.toUpperCase(); }');
    return JS('String', r'#.replace(/-([\da-z])/ig, #)', replacedMs, fToUpper);
$else
    // The "ms" prefix is always lowercased.
    return hyphenated.replaceFirst(new RegExp('^-ms-'), 'ms-').replaceAllMapped(
        new RegExp('-([a-z]+)', caseSensitive: false),
        (match) => match[0][1].toUpperCase() + match[0].substring(2));
$endif
  }

$if DART2JS
  void _setPropertyHelper(String propertyName, String value, [String priority]) {
    // try/catch for IE9 which throws on unsupported values.
    try {
      if (value == null) value = '';
      if (priority == null) {
        priority = '';
      }
      JS('void', '#.setProperty(#, #, #)', this, propertyName, value, priority);
      // Bug #2772, IE9 requires a poke to actually apply the value.
      if (JS('bool', '!!#.setAttribute', this)) {
        JS('void', '#.setAttribute(#, #)', this, propertyName, value);
      }
    } catch (e) {}
  }

  /**
   * Checks to see if CSS Transitions are supported.
   */
  static bool get supportsTransitions {
    return document.body.style.supportsProperty('transition');
  }
$else
  void _setPropertyHelper(String propertyName, String value, [String priority]) {
    if (priority == null) {
      priority = '';
    }
    _setProperty(propertyName, value, priority);
  }

  /**
   * Checks to see if CSS Transitions are supported.
   */
  static bool get supportsTransitions => true;
$endif
$!MEMBERS
}

class _CssStyleDeclarationSet extends Object with CssStyleDeclarationBase {
  final Iterable<Element> _elementIterable;
  Iterable<CssStyleDeclaration> _elementCssStyleDeclarationSetIterable;

  _CssStyleDeclarationSet(this._elementIterable) {
    _elementCssStyleDeclarationSetIterable = new List.from(
        _elementIterable).map((e) => e.style);
  }

  String getPropertyValue(String propertyName) =>
      _elementCssStyleDeclarationSetIterable.first.getPropertyValue(
          propertyName);

  void setProperty(String propertyName, String value, [String priority]) {
    _elementCssStyleDeclarationSetIterable.forEach((e) =>
        e.setProperty(propertyName, value, priority));
  }
  // Important note: CssStyleDeclarationSet does NOT implement every method
  // available in CssStyleDeclaration. Some of the methods don't make so much
  // sense in terms of having a resonable value to return when you're
  // considering a list of Elements. You will need to manually add any of the
  // items in the MEMBERS set if you want that functionality.
}

abstract class CssStyleDeclarationBase {
  String getPropertyValue(String propertyName);
  void setProperty(String propertyName, String value, [String priority]);
""" % SOURCE_PATH)

  class_lines = [];

  seen = set()
  for prop in sorted(data, key=lambda p: camelCaseName(p)):
    camel_case_name = camelCaseName(prop)
    upper_camel_case_name = camel_case_name[0].upper() + camel_case_name[1:];
    css_name = prop.replace('-webkit-', '')
    base_css_name = prop.replace('-webkit-', '')

    if base_css_name in seen or base_css_name.startswith('-internal'):
      continue
    seen.add(base_css_name)

    comment = '  /** %s the value of "' + base_css_name + '" */'
    class_lines.append('\n');
    class_lines.append(comment % 'Gets')
    if base_css_name in annotated:
      class_lines.append(annotated[base_css_name])
    class_lines.append("""
  String get %s =>
    getPropertyValue('%s');

""" % (camel_case_name, css_name))

    class_lines.append(comment % 'Sets')
    if base_css_name in annotated:
      class_lines.append(annotated[base_css_name])
    class_lines.append("""
  void set %s(String value) {
    setProperty('%s', value, '');
  }
""" % (camel_case_name, css_name))

  class_file.write(''.join(class_lines));
  class_file.write('}\n')
  class_file.close()
