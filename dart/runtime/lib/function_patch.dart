// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

patch class Function {
  /* patch */ static apply(Function function,
                           List positionalArguments,
                           [Map<String,dynamic> namedArguments]) {
    throw new UnimplementedError('Function.apply not implemented');
  }
}
