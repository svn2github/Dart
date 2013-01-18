// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

class S { }
class M1 { }
class M2 { }
class M3 { }

typedef C = S with M1, M2, M3;

class S_M1 { }
class S_M1_M2 { }

main() {
  var c = new C();
  Expect.isTrue(c is C);
  Expect.isTrue(c is S);
  Expect.isFalse(c is S_M1);
  Expect.isFalse(c is S_M1_M2);

  var sm = new S_M1();
  Expect.isFalse(sm is C);
  Expect.isFalse(sm is S);
  Expect.isTrue(sm is S_M1);
  Expect.isFalse(sm is S_M1_M2);

  var smm = new S_M1_M2();
  Expect.isFalse(smm is C);
  Expect.isFalse(smm is S);
  Expect.isFalse(smm is S_M1);
  Expect.isTrue(smm is S_M1_M2);
}
