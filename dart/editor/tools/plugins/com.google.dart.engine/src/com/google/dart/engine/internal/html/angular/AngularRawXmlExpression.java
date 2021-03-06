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

package com.google.dart.engine.internal.html.angular;

/**
 * Implementation of {@link AngularXmlExpression} for an {@link AngularExpression} embedded without
 * any wrapping characters.
 */
public class AngularRawXmlExpression extends AngularXmlExpression {
  public AngularRawXmlExpression(AngularExpression expression) {
    super(expression);
  }

  @Override
  public int getEnd() {
    return expression.getEnd();
  }

  @Override
  public int getLength() {
    return expression.getLength();
  }

  @Override
  public int getOffset() {
    return expression.getOffset();
  }
}
