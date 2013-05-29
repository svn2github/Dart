// Copyright (c) 2011, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

package com.google.dart.compiler.ast;

import com.google.dart.compiler.resolver.NodeElement;

/**
 * Base class for directives.
 */
public abstract class DartDirective extends DartNodeWithMetadata implements HasObsoleteMetadata {

  @Override
  public NodeElement getElement() {
    return null;
  }

  @Override
  public DartObsoleteMetadata getObsoleteMetadata() {
    return null;
  }

  @Override
  public void setObsoleteMetadata(DartObsoleteMetadata metadata) {

  }
}
