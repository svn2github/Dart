// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// Test correctness of side effects tracking used by load to load forwarding.

import "package:expect/expect.dart";
import "dart:typed_data";

class A {
  var x, y;
  A(this.x, this.y);
}

foo(a) {
  var value1 = a.x;
  var value2 = a.y;
  for (var j = 1; j < 4; j++) {
    value1 |= a.x << (j * 8);
    a.y += 1;
    a.x += 1;
    value2 |= a.y << (j * 8);
  }
  return [value1, value2];
}

bar(a, mode) {
  var value1 = a.x;
  var value2 = a.y;
  for (var j = 1; j < 4; j++) {
    value1 |= a.x << (j * 8);
    a.y += 1;
    if (mode) a.x += 1;
    a.x += 1;
    value2 |= a.y << (j * 8);
  }
  return [value1, value2];
}

// Verify that immutable and mutable VM fields (array length in this case)
// are not confused by load forwarding even if the access the same offset
// in the object.
testImmutableVMFields(arr, immutable) {
  if (immutable) {
    return arr.length;  // Immutable length load.
  }

  if (arr.length < 2) {  // Mutable length load, should not be forwarded.
    arr.add(null);
  }

  return arr.length;
}


testPhiRepresentation(f, arr) {
  if (f) {
    arr[0] = arr[0] + arr[1];
  } else {
    arr[0] = arr[0] - arr[1];
  }
  return arr[0];
}


testPhiConvertions(f, arr) {
  if (f) {
    arr[0] = arr[1];
  } else {
    arr[0] = arr[2];
  }
  return arr[0];
}

class M {
  var x;
  M(this.x);
}

fakeAliasing(arr) {
  var a = new M(10);
  var b = new M(10);
  var c = arr.length;

  if (c * c != c * c) {
    arr[0] = a;  // Escape.
    arr[0] = b;
  }

  return c * c;  // Deopt point.
}

main() {
  final fixed = new List(10);
  final growable = [];
  testImmutableVMFields(fixed, true);
  testImmutableVMFields(growable, false);
  testImmutableVMFields(growable, false);

  final f64List = new Float64List(2);
  testPhiRepresentation(true, f64List);
  testPhiRepresentation(false, f64List);

  for (var i = 0; i < 2000; i++) {
    Expect.listEquals([0x02010000, 0x03020100], foo(new A(0, 0)));
    Expect.listEquals([0x02010000, 0x03020100], bar(new A(0, 0), false));
    Expect.listEquals([0x04020000, 0x03020100], bar(new A(0, 0), true));
    testImmutableVMFields(fixed, true);
    testPhiRepresentation(true, f64List);
  }

  Expect.equals(1, testImmutableVMFields([], false));
  Expect.equals(2, testImmutableVMFields([1], false));
  Expect.equals(2, testImmutableVMFields([1, 2], false));
  Expect.equals(3, testImmutableVMFields([1, 2, 3], false));

  final u32List = new Uint32List(3);
  u32List[0] = 0;
  u32List[1] = 0x3FFFFFFF;
  u32List[2] = 0x7FFFFFFF;

  for (var i = 0; i < 4000; i++) {
    testPhiConvertions(true, u32List);
    testPhiConvertions(false, u32List);
  }

  final escape = new List(1);
  for (var i = 0; i < 10000; i++) {
    fakeAliasing(escape);
  }
}
