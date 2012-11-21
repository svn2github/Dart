// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// Dart test program for testing Interfaces.

interface Ai {
  int foo();
}

interface Bi extends Ai default InterfaceTest {
  Bi();
}

interface Simple extends Ai { }

interface Aai { }

interface Abi { }

interface Bar { }

interface Foo extends Bar { }

interface Baz extends Bar, Foo { }

class InterfaceTest implements Ai, Aai, Abi, Baz, Bi {
  var f;

  InterfaceTest() {}
  int foo() { return 1; }

  // intentionally unimplemented methods
  beta();  // Abstract.
  String beta1();  // Abstract.
  String beta2(double d);  // Abstract.
}

main() {
  // instantiate an abstract class
  var o = new Bi(); /// 00: static type warning
}
