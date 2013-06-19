/*
 * Copyright 2013 Dart project authors.
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
package com.google.dart.tools.core.internal.builder;

import com.google.dart.engine.context.AnalysisContext;
import com.google.dart.engine.context.ChangeSet;
import com.google.dart.engine.sdk.DartSdk;
import com.google.dart.engine.sdk.DirectoryBasedDartSdk;
import com.google.dart.engine.source.FileBasedSource;
import com.google.dart.tools.core.DartCore;
import com.google.dart.tools.core.analysis.model.AnalysisEvent;
import com.google.dart.tools.core.analysis.model.AnalysisListener;
import com.google.dart.tools.core.analysis.model.Project;
import com.google.dart.tools.core.analysis.model.ProjectEvent;
import com.google.dart.tools.core.analysis.model.ProjectListener;
import com.google.dart.tools.core.analysis.model.ProjectManager;
import com.google.dart.tools.core.analysis.model.ResolvedEvent;
import com.google.dart.tools.core.internal.analysis.model.ProjectManagerImpl;
import com.google.dart.tools.core.internal.model.DartIgnoreManager;
import com.google.dart.tools.core.mock.MockFile;
import com.google.dart.tools.core.mock.MockProject;
import com.google.dart.tools.core.mock.MockWorkspace;
import com.google.dart.tools.core.mock.MockWorkspaceRoot;

import junit.framework.TestCase;

import java.io.File;
import java.util.ArrayList;

public class AnalysisWorkerTest extends TestCase {

  private final class Listener implements AnalysisListener {
    final Object lock = new Object();

    @Override
    public void complete(AnalysisEvent event) {
      if (event.getContext() == context) {
        synchronized (lock) {
          completeCalled = true;
          lock.notifyAll();
        }
      }
    }

    @Override
    public void resolved(ResolvedEvent event) {
      if (event.getContext() == context) {
        synchronized (lock) {
          if (!resolvedCalled) {
            resolvedCalled = true;
            resolvedCalledBeforeComplete = !completeCalled;
          }
        }
      }
    }

    /**
     * Wait up to the specified number of milliseconds for analysis to complete.
     * 
     * @param milliseconds the number of milliseconds to wait
     * @return {@code true} if analysis was completed, else {@code false}
     */
    boolean waitForComplete(long milliseconds) {
      synchronized (lock) {
        long end = System.currentTimeMillis() + milliseconds;
        while (!completeCalled) {
          long delta = end - System.currentTimeMillis();
          if (delta <= 0) {
            return false;
          }
          try {
            lock.wait(delta);
          } catch (InterruptedException e) {
            //$FALL-THROUGH$
          }
        }
        return true;
      }
    }
  }

  private MockWorkspace workspace;
  private MockWorkspaceRoot rootRes;
  private MockProject projectRes;
  private DartSdk sdk;
  private ProjectManager manager;
  private AnalysisContext context;
  private Project project;
  private AnalysisMarkerManager markerManager;
  private AnalysisWorker worker;
  private final ArrayList<Project> analyzedProjects = new ArrayList<Project>();

  private boolean resolvedCalled = false;
  private boolean completeCalled = false;
  private boolean resolvedCalledBeforeComplete = false;
  private final Listener listener = new Listener();

  public void test_performAnalysis() throws Exception {
    worker = new AnalysisWorker(project, context, manager, markerManager);

    // Perform the analysis and wait for the results to flow through the marker manager
    MockFile fileRes = addLibrary();
    worker.performAnalysis();
    markerManager.waitForMarkers(10000);
    assertTrue(listener.waitForComplete(10000));

    fileRes.assertMarkersDeleted();
    assertTrue(fileRes.getMarkers().size() > 0);
    assertTrue(resolvedCalled);
    assertTrue(completeCalled);
    assertTrue(resolvedCalledBeforeComplete);
    assertEquals(1, analyzedProjects.size());
    assertEquals(project, analyzedProjects.get(0));
    // TODO (danrubel): Assert no log entries once context only returns errors for added sources
  }

  public void test_performAnalysis_ignoredResource() throws Exception {
    worker = new AnalysisWorker(project, context, manager, markerManager);

    // Perform the analysis and wait for the results to flow through the marker manager
    MockFile fileRes = addLibrary();
    DartCore.addToIgnores(fileRes);
    try {
      worker.performAnalysis();
      markerManager.waitForMarkers(10000);
      assertTrue(listener.waitForComplete(10000));

      fileRes.assertMarkersDeleted();
      assertTrue(fileRes.getMarkers().size() == 0);
      assertTrue(resolvedCalled);
      assertTrue(completeCalled);
      assertTrue(resolvedCalledBeforeComplete);
    } finally {
      DartCore.removeFromIgnores(fileRes);
    }
  }

  public void test_performAnalysisInBackground() throws Exception {
    worker = new AnalysisWorker(project, context, manager, markerManager);

    // Perform the analysis and wait for the results to flow through the marker manager
    MockFile fileRes = addLibrary();
    worker.performAnalysisInBackground();
    AnalysisWorker.waitForBackgroundAnalysis(10000);
    markerManager.waitForMarkers(10000);
    assertTrue(listener.waitForComplete(10000));

    fileRes.assertMarkersDeleted();
    assertTrue(fileRes.getMarkers().size() > 0);
    assertTrue(resolvedCalled);
    assertTrue(completeCalled);
    assertTrue(resolvedCalledBeforeComplete);
    assertEquals(1, analyzedProjects.size());
    assertEquals(project, analyzedProjects.get(0));
  }

  public void test_stop() throws Exception {
    worker = new AnalysisWorker(project, context, manager, markerManager);

    // Perform the analysis and wait for the results to flow through the marker manager
    MockFile fileRes = addLibrary();
    worker.stop();
    worker.performAnalysis();
    markerManager.waitForMarkers(50);

    fileRes.assertMarkersNotDeleted();
    assertTrue(fileRes.getMarkers().size() == 0);
    assertFalse(resolvedCalled);
    assertFalse(completeCalled);
    assertFalse(resolvedCalledBeforeComplete);
    assertEquals(0, analyzedProjects.size());
  }

  @Override
  protected void setUp() {
    workspace = new MockWorkspace();
    rootRes = workspace.getRoot();
    projectRes = rootRes.add(new MockProject(rootRes, getClass().getSimpleName()));

//    sdk = mock(DartSdk.class);
    sdk = DirectoryBasedDartSdk.getDefaultSdk();
    manager = new ProjectManagerImpl(rootRes, sdk, new DartIgnoreManager());
    manager.addProjectListener(new ProjectListener() {
      @Override
      public void projectAnalyzed(ProjectEvent event) {
        analyzedProjects.add(event.getProject());
      }
    });
    project = manager.getProject(projectRes);
    context = project.getDefaultContext();

    markerManager = new AnalysisMarkerManager(workspace);
    AnalysisWorker.addListener(listener);
  }

  @Override
  protected void tearDown() throws Exception {
    AnalysisWorker.removeListener(listener);
    // Ensure worker is not analyzing
    worker.stop();
  }

  private MockFile addLibrary() {
    MockFile fileRes = projectRes.add(new MockFile(projectRes, "a.dart", "library a;#"));
    File file = fileRes.getLocation().toFile();
    FileBasedSource source = new FileBasedSource(context.getSourceFactory().getContentCache(), file);
    context.getSourceFactory().setContents(source, fileRes.getContentsAsString());
    ChangeSet changes = new ChangeSet();
    changes.added(source);
    context.applyChanges(changes);
    return fileRes;
  }
}
