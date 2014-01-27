/*
 * Copyright (c) 2012, the Dart project authors.
 * 
 * Licensed under the Eclipse Public License v1.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 * 
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */
package com.google.dart.engine.internal.element;

/**
 * The enumeration {@code Modifier} defines constants for all of the modifiers defined by the Dart
 * language and for a few additional flags that are useful.
 * 
 * @coverage dart.engine.element
 */
public enum Modifier {
  /**
   * Indicates that the modifier 'abstract' was applied to the element.
   */
  ABSTRACT,

  /**
   * Indicates that the modifier 'const' was applied to the element.
   */
  CONST,

  /**
   * Indicates that the modifier 'factory' was applied to the element.
   */
  FACTORY,

  /**
   * Indicates that the modifier 'final' was applied to the element.
   */
  FINAL,

  /**
   * Indicates that the pseudo-modifier 'get' was applied to the element.
   */
  GETTER,

  /**
   * A flag used for libraries indicating that the defining compilation unit contains at least one
   * import directive whose URI uses the "dart-ext" scheme.
   */
  HAS_EXT_URI,

  MIXIN,

  REFERENCES_SUPER,

  /**
   * Indicates that the pseudo-modifier 'set' was applied to the element.
   */
  SETTER,

  /**
   * Indicates that the modifier 'static' was applied to the element.
   */
  STATIC,

  /**
   * Indicates that the element does not appear in the source code but was implicitly created. For
   * example, if a class does not define any constructors, an implicit zero-argument constructor
   * will be created and it will be marked as being synthetic.
   */
  SYNTHETIC,

  TYPEDEF;
}
