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
package com.google.dart.tools.debug.core.configs;

import com.google.dart.compiler.util.apache.ObjectUtils;
import com.google.dart.tools.core.DartCore;
import com.google.dart.tools.core.model.DartSdkManager;
import com.google.dart.tools.debug.core.DartDebugCorePlugin;
import com.google.dart.tools.debug.core.DartLaunchConfigWrapper;
import com.google.dart.tools.debug.core.DartLaunchConfigurationDelegate;
import com.google.dart.tools.debug.core.server.ServerDebugTarget;
import com.google.dart.tools.debug.core.util.NetUtils;

import org.eclipse.core.resources.IResource;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Path;
import org.eclipse.core.runtime.Status;
import org.eclipse.debug.core.DebugException;
import org.eclipse.debug.core.DebugPlugin;
import org.eclipse.debug.core.ILaunch;
import org.eclipse.debug.core.ILaunchConfiguration;
import org.eclipse.debug.core.ILaunchManager;
import org.eclipse.debug.core.model.IProcess;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.SequenceInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The Dart Server Application launch configuration.
 */
public class DartServerLaunchConfigurationDelegate extends DartLaunchConfigurationDelegate {
  private static final int DEFAULT_PORT_NUMBER = 5858;

  /**
   * Create a new DartServerLaunchConfigurationDelegate.
   */
  public DartServerLaunchConfigurationDelegate() {

  }

  @Override
  public boolean buildForLaunch(ILaunchConfiguration configuration, String mode,
      IProgressMonitor monitor) throws CoreException {
    return false;
  }

  @Override
  public void launch(ILaunchConfiguration configuration, String mode, ILaunch launch,
      IProgressMonitor monitor) throws CoreException {
    DartLaunchConfigWrapper launchConfig = new DartLaunchConfigWrapper(configuration);

    launchConfig.markAsLaunched();

    boolean enableDebugging = launchConfig.getEnableDebugging()
        && ILaunchManager.DEBUG_MODE.equals(mode);

    terminateSameLaunches(launch);

    launchVM(launch, launchConfig, enableDebugging, monitor);
  }

  protected void launchVM(ILaunch launch, DartLaunchConfigWrapper launchConfig,
      boolean enableDebugging, IProgressMonitor monitor) throws CoreException {
    // Usage: dart [options] script.dart [arguments]

    File currentWorkingDirectory = getCurrentWorkingDirectory(launchConfig);

    String scriptPath = launchConfig.getApplicationName();

    scriptPath = translateToFilePath(currentWorkingDirectory, scriptPath);

    String vmExecPath = "";

    if (DartSdkManager.getManager().hasSdk()) {
      File vmExec = DartSdkManager.getManager().getSdk().getVmExecutable();

      if (vmExec != null) {
        vmExecPath = vmExec.getAbsolutePath().toString();
      }
    } else {
      vmExecPath = DartDebugCorePlugin.getPlugin().getDartVmExecutablePath();
    }

    if (vmExecPath.length() == 0) {
      throw new CoreException(
          DartDebugCorePlugin.createErrorStatus("The executable path for the Dart VM has not been set."));
    }

    List<String> commandsList = new ArrayList<String>();

    int connectionPort = NetUtils.findUnusedPort(DEFAULT_PORT_NUMBER);

    commandsList.add(vmExecPath);
    commandsList.addAll(Arrays.asList(launchConfig.getVmArgumentsAsArray()));

    if (enableDebugging && !DartCore.isWindows()) {
      commandsList.add("--debug:" + connectionPort);
    }

    String packageRoot = DartCore.getPlugin().getPackageRootPref();
    if (packageRoot != null) {
      packageRoot = new Path(packageRoot).makeAbsolute().toOSString();
      String fileSeparator = System.getProperty("file.separator");
      if (!packageRoot.endsWith(fileSeparator)) {
        packageRoot += fileSeparator;
      }
      commandsList.add("--package-root=" + packageRoot);
    }

    commandsList.add(scriptPath);
    commandsList.addAll(Arrays.asList(launchConfig.getArgumentsAsArray()));
    String[] commands = commandsList.toArray(new String[commandsList.size()]);
    ProcessBuilder processBuilder = new ProcessBuilder(commands);

    if (currentWorkingDirectory != null) {
      processBuilder.directory(currentWorkingDirectory);
    }

    Process runtimeProcess = null;

    try {
      runtimeProcess = processBuilder.start();
    } catch (IOException ioe) {
      throw new CoreException(new Status(
          IStatus.ERROR,
          DartDebugCorePlugin.PLUGIN_ID,
          ioe.getMessage(),
          ioe));
    }

    IProcess eclipseProcess = null;

    Map<String, String> processAttributes = new HashMap<String, String>();

    String programName = "dart";
    processAttributes.put(IProcess.ATTR_PROCESS_TYPE, programName);

    if (runtimeProcess != null) {
      monitor.beginTask("Dart", IProgressMonitor.UNKNOWN);

      eclipseProcess = DebugPlugin.newProcess(
          launch,
          wrapProcess(runtimeProcess, processBuilder),
          launchConfig.getApplicationName(),
          processAttributes);
    }

    if (runtimeProcess == null || eclipseProcess == null) {
      if (runtimeProcess != null) {
        runtimeProcess.destroy();
      }

      throw new CoreException(
          DartDebugCorePlugin.createErrorStatus("Error starting Dart VM process"));
    }

    eclipseProcess.setAttribute(IProcess.ATTR_CMDLINE, generateCommandLine(commands));

    if (enableDebugging && !DartCore.isWindows()) {
      ServerDebugTarget debugTarget = new ServerDebugTarget(launch, eclipseProcess, connectionPort);

      try {
        debugTarget.connect();

        launch.addDebugTarget(debugTarget);
      } catch (DebugException ex) {
        // We don't throw an exception if the process died before we could connect.
        if (!isProcessDead(runtimeProcess)) {
          throw ex;
        }
      }
    }

    monitor.done();
  }

  private String describe(ProcessBuilder processBuilder) {
    StringBuilder builder = new StringBuilder();

    for (String arg : processBuilder.command()) {
      // Showing the --debug option doesn't provide a lot of value.
      if (arg.startsWith("--debug")) {
        continue;
      }

      // Shorten the long path to the dart vm - just show "dart".
      if (arg.endsWith(File.separator
          + DartSdkManager.getManager().getSdk().getVmExecutable().getName())) {
        builder.append("dart");
      } else {
        builder.append(arg);
      }

      builder.append(" ");
    }

    return builder.toString().trim() + "\n\n";
  }

  private String generateCommandLine(String[] commands) {
    StringBuilder builder = new StringBuilder();

    for (String str : commands) {
      if (builder.length() > 0) {
        builder.append(" ");
      }

      builder.append(str);
    }

    return builder.toString();
  }

  private File getCurrentWorkingDirectory(DartLaunchConfigWrapper launchConfig) {
    IResource resource = launchConfig.getApplicationResource();

    if (resource == null) {
      if (launchConfig.getProject() != null) {
        return launchConfig.getProject().getLocation().toFile();
      }
    } else {
      if (resource.isLinked()) {
        // If the resource is linked, set the cwd to the parent directory of the resolved resource.
        return resource.getLocation().toFile().getParentFile();
      } else {
        // If the resource is not linked, set the cwd to the project's directory.
        return resource.getProject().getLocation().toFile();
      }
    }

    return null;
  }

  private boolean isProcessDead(Process process) {
    try {
      process.exitValue();

      return true;
    } catch (IllegalThreadStateException ex) {
      return false;
    }
  }

  private void sleep(int millis) {
    try {
      Thread.sleep(millis);
    } catch (InterruptedException e) {

    }
  }

  private void terminateSameLaunches(ILaunch currentLaunch) {
    ILaunchManager manager = DebugPlugin.getDefault().getLaunchManager();

    boolean launchTerminated = false;

    for (ILaunch launch : manager.getLaunches()) {
      if (ObjectUtils.equals(
          launch.getLaunchConfiguration(),
          currentLaunch.getLaunchConfiguration())) {
        try {
          launchTerminated = true;
          launch.terminate();
        } catch (DebugException e) {
          DartDebugCorePlugin.logError(e);
        }
      }
    }

    if (launchTerminated) {
      // Wait a while for processes to shutdown.
      sleep(100);
    }
  }

  /**
   * Return either a path relative to the cwd, if possible, or an absolute path to the given script.
   * 
   * @param cwd the current working directory for the launch
   * @param scriptPath the path to the script (a workspace path)
   * @return either a cwd relative path or an absolute path
   */
  private String translateToFilePath(File cwd, String scriptPath) {
    IResource resource = ResourcesPlugin.getWorkspace().getRoot().findMember(scriptPath);

    if (resource != null) {
      String path = resource.getLocation().toFile().getAbsolutePath();

      if (cwd != null) {
        String cwdPath = cwd.getAbsolutePath();

        if (!cwdPath.endsWith(File.separator)) {
          cwdPath = cwdPath + File.separator;
        }

        if (path.startsWith(cwdPath)) {
          path = path.substring(cwdPath.length());
        }
      }

      return path;
    } else {
      return scriptPath;
    }
  }

  private Process wrapProcess(final Process process, final ProcessBuilder processBuilder) {
    return new Process() {
      private InputStream in;

      @Override
      public void destroy() {
        process.destroy();
      }

      @Override
      public int exitValue() {
        return process.exitValue();
      }

      @Override
      public InputStream getErrorStream() {
        return process.getErrorStream();
      }

      @Override
      public InputStream getInputStream() {
        if (in == null) {
          in = new SequenceInputStream(
              new ByteArrayInputStream(describe(processBuilder).getBytes()),
              process.getInputStream());
        }

        return in;
      }

      @Override
      public OutputStream getOutputStream() {
        return process.getOutputStream();
      }

      @Override
      public int waitFor() throws InterruptedException {
        return process.waitFor();
      }
    };
  }
}
