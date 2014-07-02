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
package com.google.dart.server;

/**
 * The interface {@code AssistsConsumer} defines the behavior of objects that consume assists
 * {@link SourceChange}s.
 * 
 * @coverage dart.server
 */
public interface AssistsConsumer extends Consumer {
  /**
   * A set of {@link SourceChange}s that have been computed.
   * 
   * @param proposals an array of computed {@link SourceChange}s
   * @param isLastResult is {@code true} if this is the last set of results
   */
  public void computedSourceChanges(SourceChange[] sourceChanges, boolean isLastResult);
}
