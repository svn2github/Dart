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
package com.google.dart.server.internal.local.operation;

import com.google.dart.engine.context.AnalysisDelta;
import com.google.dart.server.internal.local.ApplyAnalysisDeltaOperation;
import com.google.dart.server.internal.local.LocalAnalysisServerImpl;

import junit.framework.TestCase;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

public class ApplyAnalysisDeltaOperationTest extends TestCase {
  private LocalAnalysisServerImpl server = mock(LocalAnalysisServerImpl.class);

  public void testPerformOperation() throws Exception {
    AnalysisDelta delta = new AnalysisDelta();
    ApplyAnalysisDeltaOperation operation = new ApplyAnalysisDeltaOperation("id", delta);
    assertEquals("id", operation.getContextId());
    assertSame(ServerOperationPriority.CONTEXT_CHANGE, operation.getPriority());
    // perform
    operation.performOperation(server);
    verify(server, times(1)).internalApplyAnalysisDelta("id", delta);
  }
}
