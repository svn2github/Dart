// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.hierarchy_invariants_test;

import 'dart:mirrors';

import 'package:expect/expect.dart';

isAnonymousMixinApplication(classMirror) {
  return MirrorSystem.getName(classMirror.simpleName).contains(' with ');
}

check(classMirror) {
  if (classMirror is TypedefMirror) return;

  Expect.isTrue(classMirror.simpleName is Symbol);
  Expect.notEquals(null, classMirror.owner);
  Expect.isTrue(classMirror.owner is LibraryMirror);
  if (!isAnonymousMixinApplication(classMirror)) {
    Expect.equals(classMirror.originalDeclaration,
                  classMirror.owner.declarations[classMirror.simpleName]);
  } else {
    Expect.isNull(classMirror.owner.declarations[classMirror.simpleName]);
  }
  Expect.isTrue(classMirror.superinterfaces is List);
  if (classMirror.superclass == null) {
    Expect.equals(reflectClass(Object), classMirror);
  } else {
    check(classMirror.superclass);
  }
}

main() {
  currentMirrorSystem().libraries.values.forEach((libraryMirror) {
    libraryMirror.declarations.values.forEach((declaration) {
      if (declaration is ClassMirror) check(declaration);
    });
  });

  Expect.throws(() => reflectClass(dynamic),
                (e) => e is ArgumentError,
                'dynamic is not a class');
}
