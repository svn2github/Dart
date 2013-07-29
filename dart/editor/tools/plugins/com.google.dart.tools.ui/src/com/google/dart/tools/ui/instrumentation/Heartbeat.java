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
package com.google.dart.tools.ui.instrumentation;

import com.google.dart.engine.utilities.instrumentation.Instrumentation;
import com.google.dart.engine.utilities.instrumentation.InstrumentationBuilder;
import com.google.dart.tools.ui.feedback.FeedbackUtils;
import com.google.dart.tools.ui.instrumentation.util.Base64;

import org.eclipse.jface.text.IDocument;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.IEditorReference;
import org.eclipse.ui.IViewReference;
import org.eclipse.ui.IWorkbench;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.IWorkbenchWindow;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.texteditor.IDocumentProvider;
import org.eclipse.ui.texteditor.ITextEditor;

import java.lang.management.ManagementFactory;
import java.lang.management.ThreadInfo;

/**
 * {@code Heartbeat} provides utility methods that an external instrumentation plugin can call to
 * get periodic information about the health of the development environment.
 */
public class Heartbeat {

  private static final Heartbeat INSTANCE = new Heartbeat();

  public static Heartbeat getInstance() {
    return INSTANCE;
  }

  /**
   * This method logs information about the health of the development environment. It is imperative
   * that this method execute quickly as this is typically called once per minute.
   * 
   * @param instrumentation the instrumentation used to log information (not {@code null})
   */
  public void heartbeat(InstrumentationBuilder instrumentation) {

    //@TODO(lukechurch): Add tests
    instrumentation.metric("MexMemory-FeedbackUtils", FeedbackUtils.getMaxMem());
    instrumentation.metric("TotalMemory", Runtime.getRuntime().totalMemory());
    instrumentation.metric("FreeMemory", Runtime.getRuntime().freeMemory());

    logThreads(instrumentation);
    logWindowsPagesAndTabs(instrumentation);
  }

  private void logThreads(InstrumentationBuilder instrumentation) {
    java.lang.management.ThreadMXBean th = ManagementFactory.getThreadMXBean();
    ThreadInfo[] thInfos = th.getThreadInfo(th.getAllThreadIds());

    instrumentation.metric("threads-count", thInfos.length);

    for (ThreadInfo thInfo : thInfos) {
      instrumentation.metric("Thread-Name", thInfo.getThreadName());
      instrumentation.metric("Thread-ID", thInfo.getThreadId());
      instrumentation.metric("Thread-State", thInfo.getThreadState().toString());

      instrumentation.metric("Blocked-Count", thInfo.getBlockedCount());
      instrumentation.metric("Blocked-Time", thInfo.getBlockedTime());

      instrumentation.metric("Waited-Count", thInfo.getWaitedCount());
      instrumentation.metric("Waited-Time", thInfo.getWaitedTime());

      instrumentation.data(
          "Thread-ST",
          Base64.encodeBytes(thInfo.getStackTrace().toString().getBytes()));

    }

  }

  private void logWindowsPagesAndTabs(InstrumentationBuilder instrumentation) {
    IWorkbench workbench = PlatformUI.getWorkbench();
    IWorkbenchWindow activeWindow = workbench.getActiveWorkbenchWindow();
    IWorkbenchWindow[] allWindows = workbench.getWorkbenchWindows();
    instrumentation.metric("OpenWindowsCount", allWindows.length);

    for (int windowIndex = 0; windowIndex < allWindows.length; windowIndex++) {
      IWorkbenchWindow window = allWindows[windowIndex];
      if (window == activeWindow) {
        instrumentation.metric("ActiveWindow", windowIndex);
      }
      String windowKey = "Window-" + windowIndex;

      IWorkbenchPage activePage = window.getActivePage();
      IWorkbenchPage[] allPages = window.getPages();
      instrumentation.metric(windowKey + "-OpenPageCount", allPages.length);

      for (int pageIndex = 0; pageIndex < allPages.length; pageIndex++) {
        IWorkbenchPage page = allPages[pageIndex];
        if (page == activePage) {
          instrumentation.metric(windowKey + "-ActivePage", pageIndex);
        }
        String pageKey = windowKey + "-Page-" + pageIndex;

        IWorkbenchPart activePart = page.getActivePart();
        IViewReference[] allViews = page.getViewReferences();
        IEditorReference[] allEditors = page.getEditorReferences();
        instrumentation.metric(pageKey + "-OpenViewCount", allViews.length);

        for (int viewIndex = 0; viewIndex < allViews.length; viewIndex++) {
          IViewReference view = allViews[viewIndex];
          if (view == activePart) {
            instrumentation.metric(pageKey + "-ActiveView", viewIndex);
          }
          String viewKey = pageKey + "-View-" + viewIndex;

          instrumentation.metric(viewKey + "-Id", view.getId());
        }

        for (int editorIndex = 0; editorIndex < allEditors.length; editorIndex++) {
          IEditorReference editor = allEditors[editorIndex];
          if (editor == activePart) {
            instrumentation.metric(pageKey + "-ActiveEditorTab", editorIndex);
          }
          String editorKey = pageKey + "-Editor-" + editorIndex;

          instrumentation.metric(editorKey + "-Id", editor.getId());
          instrumentation.metric(editorKey + "-Dirty", editor.isDirty());
          instrumentation.data(editorKey + "-Name", editor.getTitle());

          InstrumentationBuilder srcInstr = Instrumentation.builder("Editor-src-HB");
          try {

            IEditorPart part = editor.getEditor(false);

            srcInstr.metric(editorKey + "-Id", editor.getId());
            srcInstr.metric(editorKey + "-Dirty", editor.isDirty());
            srcInstr.data(editorKey + "-Name", editor.getTitle());

            if (part instanceof ITextEditor) {
              ITextEditor textEditor = (ITextEditor) part;
              IDocumentProvider provider = textEditor.getDocumentProvider();
              if (provider != null) {
                IDocument document = provider.getDocument(textEditor.getEditorInput());
                if (document != null) {
                  String docSrc = document.get();

                  //TODO(lukechurch): Add a Java+Python compatible compressor here
                  String docSrcb64 = Base64.encodeBytes(docSrc.getBytes());

                  srcInstr.data(editorKey + "-src", docSrcb64);

                }
              }
            }

          } catch (Exception e) {
            srcInstr.record(e);

          } finally {
            srcInstr.log();
          }
        }
      }
    }
  }
}
