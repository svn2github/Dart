/*
 * Copyright (c) 2013, the Dart project authors.
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

package com.google.dart.engine.services.correction;

import junit.framework.TestCase;

import static org.fest.assertions.Assertions.assertThat;
import static org.mockito.Mockito.mock;

public class CorrectionProposalTest extends TestCase {
  public void test_access() throws Exception {
    CorrectionProposal proposal = new CorrectionProposal(
        CorrectionImage.IMG_CORRECTION_CHANGE,
        "test",
        42);
    assertSame(CorrectionImage.IMG_CORRECTION_CHANGE, proposal.getImage());
    assertEquals("test", proposal.getName());
    assertEquals(42, proposal.getRelevance());
  }

  public void test_changes() throws Exception {
    CorrectionProposal proposal = new CorrectionProposal(
        CorrectionImage.IMG_CORRECTION_CHANGE,
        "test",
        42);
    // empty
    assertThat(proposal.getChanges()).isEmpty();
    //
    SourceChange changeA = mock(SourceChange.class);
    SourceChange changeB = mock(SourceChange.class);
    proposal.addChange(changeA);
    proposal.addChange(changeB);
    assertThat(proposal.getChanges()).containsExactly(changeA, changeB);
  }
}
