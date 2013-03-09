// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

class A<T> {
  foo() => this is A<int>;
}

main() {
  Expect.isTrue(new A().foo());
  Expect.isTrue(new A<int>().foo());
  Expect.isFalse(new A<String>().foo());
}
