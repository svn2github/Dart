/*
 * Copyright (c) 2014, the Dart project authors.
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

package com.google.dart.engine.internal.element.angular;

import com.google.dart.engine.element.angular.AngularSelectorElement;
import com.google.dart.engine.element.angular.AngularTagSelectorElement;
import com.google.dart.engine.html.ast.XmlTagNode;

/**
 * Implementation of {@link AngularSelectorElement} based on tag name.
 */
public class AngularTagSelectorElementImpl extends AngularSelectorElementImpl implements
    AngularTagSelectorElement {
  public AngularTagSelectorElementImpl(String name, int offset) {
    super(name, offset);
  }

  @Override
  public boolean apply(XmlTagNode node) {
    String tagName = getName();
    return node.getTag().equals(tagName);
  }

  @Override
  public AngularApplication getApplication() {
    return ((AngularElementImpl) getEnclosingElement()).getApplication();
  }
}
