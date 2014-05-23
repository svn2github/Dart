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
package com.google.dart.tools.ui.actions;

import com.google.dart.tools.ui.instrumentation.UIInstrumentationBuilder;
import com.google.dart.tools.ui.internal.refactoring.RefactoringMessages;
import com.google.dart.tools.ui.internal.refactoring.ServerExtractLocalRefactoring;
import com.google.dart.tools.ui.internal.text.DartHelpContextIds;
import com.google.dart.tools.ui.internal.text.editor.DartEditor;
import com.google.dart.tools.ui.internal.text.editor.DartSelection;

import org.eclipse.jface.action.Action;
import org.eclipse.swt.widgets.Event;
import org.eclipse.ui.PlatformUI;

/**
 * {@link Action} for "Extract Local" refactoring.
 * 
 * @coverage dart.editor.ui.refactoring.ui
 */
public class ExtractLocalAction_NEW extends AbstractRefactoringAction {
  private ServerExtractLocalRefactoring refactoring;

  public ExtractLocalAction_NEW(DartEditor editor) {
    super(editor);
  }

  @Override
  public void selectionChanged(DartSelection selection) {
    // cannot operate on this editor
    if (!canOperateOn()) {
      setEnabled(false);
      return;
    }
    // empty selection
    if (selection.getLength() == 0) {
      setEnabled(false);
      return;
    }
//    // prepare context
//    AssistContext context = selection.getContext();
//    if (context == null) {
//      setEnabled(false);
//      return;
//    }
//    // prepare covered node
//    AstNode coveredNode = context.getCoveredNode();
//    if (coveredNode == null) {
//      setEnabled(false);
//      return;
//    }
//    // selection should be inside of executable node
//    if (coveredNode.getAncestor(Block.class) == null) {
//      setEnabled(false);
//      return;
//    }
    // OK
    setEnabled(true);
  }

  @Override
  protected void doRun(DartSelection selection, Event event,
      UIInstrumentationBuilder instrumentation) {
    // TODO(scheglov) restore or remove for the new API
//    final String contextId = selection.getEditor().getInputAnalysisContextId();
//    final Source source = selection.getEditor().getInputSource();
//    if (contextId == null || source == null) {
//      return;
//    }
//    final int offset = selection.getOffset();
//    final int length = selection.getLength();
//    // prepare refactoring
//    refactoring = null;
//    Control focusControl = Display.getCurrent().getFocusControl();
//    try {
//      IProgressService progressService = PlatformUI.getWorkbench().getProgressService();
//      progressService.busyCursorWhile(new IRunnableWithProgress() {
//        @Override
//        public void run(IProgressMonitor pm) throws InterruptedException {
//          final CountDownLatch latch = new CountDownLatch(1);
//          DartCore.getAnalysisServer().createRefactoringExtractLocal(
//              contextId,
//              source,
//              offset,
//              length,
//              new RefactoringExtractLocalConsumer() {
//                @Override
//                public void computed(String refactoringId, RefactoringStatus status,
//                    boolean hasSeveralOccurrences, String[] proposedNames) {
//                  refactoring = new ServerExtractLocalRefactoring(
//                      refactoringId,
//                      status,
//                      hasSeveralOccurrences,
//                      proposedNames);
//                  latch.countDown();
//                }
//              });
//          while (true) {
//            if (pm.isCanceled()) {
//              throw new InterruptedException();
//            }
//            if (Uninterruptibles.awaitUninterruptibly(latch, 10, TimeUnit.MILLISECONDS)) {
//              break;
//            }
//          }
//        }
//      });
//    } catch (Throwable e) {
//      return;
//    } finally {
//      if (focusControl != null) {
//        focusControl.setFocus();
//      }
//    }
//    if (refactoring == null) {
//      return;
//    }
//    // open dialog
//    try {
//      new RefactoringStarter().activate(
//          new ExtractLocalWizard_NEW(refactoring),
//          getShell(),
//          RefactoringMessages.ExtractLocalAction_dialog_title,
//          RefactoringSaveHelper.SAVE_NOTHING);
//    } catch (Throwable e) {
//      ExceptionHandler.handle(
//          e,
//          "Extract Local",
//          "Unexpected exception occurred. See the error log for more details.");
//    }
  }

  @Override
  protected void init() {
    setText(RefactoringMessages.ExtractLocalAction_label);
    {
      String id = DartEditorActionDefinitionIds.EXTRACT_LOCAL_VARIABLE;
      setId(id);
      setActionDefinitionId(id);
    }
    PlatformUI.getWorkbench().getHelpSystem().setHelp(this, DartHelpContextIds.EXTRACT_LOCAL_ACTION);
  }
}
