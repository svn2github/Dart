// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "dart:async";

@lazy import "deferred_constraints_lib.dart" as lib;
import "deferred_constraints_lib.dart" as lib2; /// type_annotation_non_deferred: ok

const lazy = const DeferredLibrary('lib');

class F {}
class G2<T> {}

void main() {
  lib.C a = new lib.C(); /// type_annotation1: runtime error, static type warning
  lib.C a = null; /// type_annotation_null: static type warning

  // In this case we do not defer C.
  lib2.C a1 = new lib2.C(); /// type_annotation_non_deferred: continued
  lazy.load().then((_) {
    lib.C a2 = new lib.C(); /// type_annotation2: dynamic type error, static type warning
    lib.G<F> a3 = new lib.G<F>(); /// type_annotation_generic1: dynamic type error, static type warning
    G2<lib.C> a4 = new G2(); /// type_annotation_generic2: static type warning
    G2<lib.C> a5 = new G2<lib.C>(); /// type_annotation_generic3: static type warning
    lib.G<lib.C> a = new lib.G<lib.C>(); /// type_annotation_generic4: dynamic type error, static type warning
    var a6 = new lib.C(); /// new: ok
    var g1 = new lib.G<F>(); /// new_generic1: ok
    // new G2<lib.C>() does not give a dynamic type error because a malformed
    // type used as type-parameter is treated as dynamic.
    var g2 = new G2<lib.C>(); /// new_generic2: static type warning
    var g3 = new lib.G<lib.C>(); /// new_generic3: static type warning
    var instance = lib.constantInstance;
    bool a7 = instance is lib.Const; /// is_check: runtime error, static type warning
    instance as lib.Const; /// as_operation: runtime error, static type warning
    try { throw inst; } on lib.C {} /// catch_check: runtime error, static type warning
    int i = lib.C.staticMethod(); /// static_method: ok
  });
}

lib.C a9 = null; /// type_annotation_top_level: static type warning
