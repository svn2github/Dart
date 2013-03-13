/*
 * Copyright (c) 2012, the Dart project authors.
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
package com.google.dart.tools.debug.core.dartium;

import com.google.dart.tools.core.NotYetImplementedException;
import com.google.dart.tools.debug.core.DartDebugCorePlugin;
import com.google.dart.tools.debug.core.DartDebugCorePlugin.BreakOnExceptions;
import com.google.dart.tools.debug.core.DartLaunchConfigWrapper;
import com.google.dart.tools.debug.core.DebugUIHelper;
import com.google.dart.tools.debug.core.breakpoints.DartBreakpoint;
import com.google.dart.tools.debug.core.util.IResourceResolver;
import com.google.dart.tools.debug.core.webkit.WebkitBreakpoint;
import com.google.dart.tools.debug.core.webkit.WebkitCallFrame;
import com.google.dart.tools.debug.core.webkit.WebkitCallback;
import com.google.dart.tools.debug.core.webkit.WebkitConnection;
import com.google.dart.tools.debug.core.webkit.WebkitConnection.WebkitConnectionListener;
import com.google.dart.tools.debug.core.webkit.WebkitDebugger.DebuggerListenerAdapter;
import com.google.dart.tools.debug.core.webkit.WebkitDebugger.PauseOnExceptionsType;
import com.google.dart.tools.debug.core.webkit.WebkitDebugger.PausedReasonType;
import com.google.dart.tools.debug.core.webkit.WebkitDom.DomListener;
import com.google.dart.tools.debug.core.webkit.WebkitDom.InspectorListener;
import com.google.dart.tools.debug.core.webkit.WebkitPage;
import com.google.dart.tools.debug.core.webkit.WebkitRemoteObject;
import com.google.dart.tools.debug.core.webkit.WebkitResult;

import org.eclipse.core.resources.IMarkerDelta;
import org.eclipse.debug.core.DebugException;
import org.eclipse.debug.core.DebugPlugin;
import org.eclipse.debug.core.ILaunch;
import org.eclipse.debug.core.ILaunchConfiguration;
import org.eclipse.debug.core.model.IBreakpoint;
import org.eclipse.debug.core.model.IDebugTarget;
import org.eclipse.debug.core.model.IMemoryBlock;
import org.eclipse.debug.core.model.IProcess;
import org.eclipse.debug.core.model.IStreamMonitor;
import org.eclipse.debug.core.model.IThread;

import java.io.File;
import java.io.IOException;
import java.util.List;

/**
 * The IDebugTarget implementation for the Dartium debug elements.
 */
public class DartiumDebugTarget extends DartiumDebugElement implements IDebugTarget {
  private static DartiumDebugTarget activeTarget;

  public static DartiumDebugTarget getActiveTarget() {
    return activeTarget;
  }

  private static void setActiveTarget(DartiumDebugTarget target) {
    activeTarget = target;
  }

  private String debugTargetName;
  private WebkitConnection connection;
  private ILaunch launch;
  private DartiumProcess process;
  private IResourceResolver resourceResolver;
  private DartiumDebugThread debugThread;
  private DartiumStreamMonitor outputStreamMonitor;
  private BreakpointManager breakpointManager;
  private CssScriptManager cssScriptManager;
  private HtmlScriptManager htmlScriptManager;
  private DartCodeManager dartCodeManager;
  private boolean canSetScriptSource;
  private SourceMapManager sourceMapManager;

  /**
   * @param target
   */
  public DartiumDebugTarget(File executable, String debugTargetName, WebkitConnection connection,
      ILaunch launch, Process javaProcess, IResourceResolver resourceResolver,
      boolean enableBreakpoints) {
    super(null);

    setActiveTarget(this);

    this.debugTargetName = debugTargetName;
    this.connection = connection;
    this.launch = launch;
    this.resourceResolver = resourceResolver;

    debugThread = new DartiumDebugThread(this);
    process = new DartiumProcess(executable, this, javaProcess);
    outputStreamMonitor = new DartiumStreamMonitor();

    if (enableBreakpoints) {
      breakpointManager = new BreakpointManager(this, resourceResolver);
    }

    cssScriptManager = new CssScriptManager(this, resourceResolver);

    if (DartDebugCorePlugin.SEND_MODIFIED_HTML) {
      htmlScriptManager = new HtmlScriptManager(this, resourceResolver);
    }

    if (DartDebugCorePlugin.SEND_MODIFIED_DART) {
      dartCodeManager = new DartCodeManager(this, resourceResolver);
    }

    DartLaunchConfigWrapper wrapper = new DartLaunchConfigWrapper(launch.getLaunchConfiguration());
    sourceMapManager = new SourceMapManager(wrapper.getProject());
  }

  @Override
  public void breakpointAdded(IBreakpoint breakpoint) {
    throw new NotYetImplementedException();
  }

  @Override
  public void breakpointChanged(IBreakpoint breakpoint, IMarkerDelta delta) {
    throw new NotYetImplementedException();
  }

  @Override
  public void breakpointRemoved(IBreakpoint breakpoint, IMarkerDelta delta) {
    throw new NotYetImplementedException();
  }

  @Override
  public boolean canDisconnect() {
    return false;
  }

  @Override
  public boolean canResume() {
    return debugThread == null ? false : debugThread.canResume();
  }

  @Override
  public boolean canSuspend() {
    return debugThread == null ? false : debugThread.canSuspend();
  }

  @Override
  public boolean canTerminate() {
    return connection.isConnected();
  }

  @Override
  public void disconnect() throws DebugException {
    throw new UnsupportedOperationException("disconnect is not supported");
  }

  @Override
  public void fireTerminateEvent() {
    setActiveTarget(null);

    if (breakpointManager != null) {
      breakpointManager.dispose(false);
    }

    cssScriptManager.dispose();

    if (htmlScriptManager != null) {
      htmlScriptManager.dispose();
    }

    if (dartCodeManager != null) {
      dartCodeManager.dispose();
    }

    sourceMapManager.dispose();

    debugThread = null;

    // Check for null on system shutdown.
    if (DebugPlugin.getDefault() != null) {
      super.fireTerminateEvent();
    }
  }

  /**
   * @return the connection
   */
  @Override
  public WebkitConnection getConnection() {
    return connection;
  }

  @Override
  public IDebugTarget getDebugTarget() {
    return this;
  }

  @Override
  public ILaunch getLaunch() {
    return launch;
  }

  @Override
  public IMemoryBlock getMemoryBlock(long startAddress, long length) throws DebugException {
    return null;
  }

  @Override
  public String getName() {
    return debugTargetName;
  }

  @Override
  public IProcess getProcess() {
    return process;
  }

  @Override
  public IThread[] getThreads() throws DebugException {
    if (debugThread != null) {
      return new IThread[] {debugThread};
    } else {
      return new IThread[0];
    }
  }

  @Override
  public boolean hasThreads() throws DebugException {
    return true;
  }

  @Override
  public boolean isDisconnected() {
    return false;
  }

  @Override
  public boolean isSuspended() {
    return debugThread == null ? false : debugThread.isSuspended();
  }

  @Override
  public boolean isTerminated() {
    return process.isTerminated();
  }

  /**
   * Recycle the current Dartium debug connection; attempt to reset it to a fresh state beforehand.
   * 
   * @param url
   * @throws IOException
   */
  public void navigateToUrl(ILaunchConfiguration launchConfig, final String url,
      boolean enableBreakpoints) throws IOException {
    if (breakpointManager != null) {
      breakpointManager.dispose(true);
      breakpointManager = null;
    }

    if (enableBreakpoints) {
      connection.getDebugger().setPauseOnExceptions(
          getPauseType(),
          createNavigateWebkitCallback(url));
    } else {
      connection.getDebugger().setPauseOnExceptions(PauseOnExceptionsType.none);
    }

    if (enableBreakpoints) {
      breakpointManager = new BreakpointManager(this, resourceResolver);
      breakpointManager.connect();
    }

    getConnection().getPage().navigate(url);
  }

  public void openConnection(final String url) throws IOException {
    connection.addConnectionListener(new WebkitConnectionListener() {
      @Override
      public void connectionClosed(WebkitConnection connection) {
        fireTerminateEvent();
      }
    });

    connection.connect();

    connection.getConsole().addConsoleListener(outputStreamMonitor);
    connection.getConsole().enable();

    connection.getPage().addPageListener(new WebkitPage.PageListenerAdapter() {
      @Override
      public void loadEventFired(int timestamp) {
        cssScriptManager.handleLoadEventFired();

        if (htmlScriptManager != null) {
          htmlScriptManager.handleLoadEventFired();
        }
      }
    });
    connection.getPage().enable();

    connection.getCSS().enable();

    if (DartDebugCorePlugin.SEND_MODIFIED_HTML) {
      connection.getDom().addDomListener(new DomListener() {
        @Override
        public void documentUpdated() {
          if (htmlScriptManager != null) {
            htmlScriptManager.handleDocumentUpdated();
          }
        }
      });
    }

    connection.getDom().addInspectorListener(new InspectorListener() {
      @Override
      public void detached(String reason) {
        handleInspectorDetached(reason);
      }
    });

    connection.getDebugger().addDebuggerListener(new DebuggerListenerAdapter() {
      @Override
      public void debuggerBreakpointResolved(WebkitBreakpoint breakpoint) {
        if (breakpointManager != null) {
          breakpointManager.handleBreakpointResolved(breakpoint);
        }
      }

      @Override
      public void debuggerGlobalObjectCleared() {
        if (breakpointManager != null) {
          breakpointManager.handleGlobalObjectCleared();
        }
      }

      @Override
      public void debuggerPaused(PausedReasonType reason, List<WebkitCallFrame> frames,
          WebkitRemoteObject exception) {
        debugThread.handleDebuggerSuspended(reason, frames, exception);
      }

      @Override
      public void debuggerResumed() {
        debugThread.handleDebuggerResumed();
      }
    });
    connection.getDebugger().enable();

    connection.getDebugger().canSetScriptSource(new WebkitCallback<Boolean>() {
      @Override
      public void handleResult(WebkitResult<Boolean> result) {
        if (!result.isError() && result.getResult() != null) {
          canSetScriptSource = result.getResult().booleanValue();
        }
      }
    });

    fireCreationEvent();
    process.fireCreationEvent();

    // Set our existing breakpoints and start listening for new breakpoints.
    if (breakpointManager != null) {
      breakpointManager.connect();
    }

    // TODO(devoncarew): listen for changes to DartDebugCorePlugin.PREFS_BREAK_ON_EXCEPTIONS
    if (breakpointManager != null) {
      connection.getDebugger().setPauseOnExceptions(
          getPauseType(),
          createNavigateWebkitCallback(url));
    } else {
      connection.getPage().navigate(url);
    }
  }

  @Override
  public void resume() throws DebugException {
    debugThread.resume();
  }

  @Override
  public boolean supportsBreakpoint(IBreakpoint breakpoint) {
    return breakpoint instanceof DartBreakpoint;
  }

  public boolean supportsSetScriptSource() {
    return canSetScriptSource;
  }

  @Override
  public boolean supportsStorageRetrieval() {
    return false;
  }

  @Override
  public void suspend() throws DebugException {
    debugThread.suspend();
  }

  @Override
  public void terminate() throws DebugException {
    process.terminate();
  }

  public void writeToStdout(String message) {
    outputStreamMonitor.messageAdded(message);
  }

  protected WebkitCallback<Boolean> createNavigateWebkitCallback(final String url) {
    return new WebkitCallback<Boolean>() {
      @Override
      public void handleResult(WebkitResult<Boolean> result) {
        // Once all other requests have been processed, then navigate to the given url.
        try {
          connection.getPage().navigate(url);
        } catch (IOException e) {
          DartDebugCorePlugin.logError(e);
        }
      }
    };
  }

  protected BreakpointManager getBreakpointManager() {
    return breakpointManager;
  }

  protected SourceMapManager getSourceMapManager() {
    return sourceMapManager;
  }

  protected WebkitConnection getWebkitConnection() {
    return connection;
  }

  protected void handleInspectorDetached(String reason) {
    // "replaced_with_devtools", "target_closed", ...

    final String replacedWithDevTools = "replaced_with_devtools";

    if (replacedWithDevTools.equalsIgnoreCase(reason)) {
      // When the user opens the Webkit inspector our debug connection is closed.
      // We warn the user when this happens, since it otherwise isn't apparent to them
      // when the debugger connection is closing.
      DebugUIHelper.getHelper().showError(
          "Debugger Connection Closed",
          "The debugger connection has been closed by the remote host.");
    }
  }

  protected boolean shouldUseSourceMapping() {
    return DartDebugCorePlugin.getPlugin().getUseSourceMaps();
  }

  IStreamMonitor getOutputStreamMonitor() {
    return outputStreamMonitor;
  }

  private PauseOnExceptionsType getPauseType() {
    final BreakOnExceptions boe = DartDebugCorePlugin.getPlugin().getBreakOnExceptions();
    PauseOnExceptionsType pauseType = PauseOnExceptionsType.none;

    if (boe == BreakOnExceptions.uncaught) {
      pauseType = PauseOnExceptionsType.uncaught;
    } else if (boe == BreakOnExceptions.all) {
      pauseType = PauseOnExceptionsType.all;
    }

    return pauseType;
  }

}
