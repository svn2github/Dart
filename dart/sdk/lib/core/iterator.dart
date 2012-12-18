// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

/**
 * The [Iterator] class provides methods to iterate over an object. It
 * is transparently used by the for-in construct to test for the end
 * of the iteration, and to get the elements.
 *
 * If the object iterated over is changed during the iteration, the
 * behavior is unspecified.
 */
abstract class Iterator<E> {
  /**
   * Gets the next element in the iteration. Throws a
   * [StateError] if no element is left.
   */
  E next();

  /**
   * Returns whether the [Iterator] has elements left.
   */
  bool get hasNext;
}
