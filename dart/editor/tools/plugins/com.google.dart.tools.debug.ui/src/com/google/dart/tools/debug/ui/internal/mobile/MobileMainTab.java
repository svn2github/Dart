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
package com.google.dart.tools.debug.ui.internal.mobile;

import com.google.dart.tools.core.mobile.AndroidDebugBridge;
import com.google.dart.tools.core.model.DartSdkManager;
import com.google.dart.tools.debug.core.DartLaunchConfigWrapper;
import com.google.dart.tools.debug.ui.internal.DartDebugUIPlugin;
import com.google.dart.tools.debug.ui.internal.util.LaunchTargetComposite;

import org.eclipse.debug.core.ILaunchConfiguration;
import org.eclipse.debug.core.ILaunchConfigurationWorkingCopy;
import org.eclipse.debug.ui.AbstractLaunchConfigurationTab;
import org.eclipse.jface.layout.GridDataFactory;
import org.eclipse.jface.layout.GridLayoutFactory;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Listener;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Main tab for Mobile launch configurations
 */
public class MobileMainTab extends AbstractLaunchConfigurationTab {

  private static final String MOBILE_STATUS_PREFIX = "Mobile status: ";
  private static final String DEVICE_CONNECTED = "Connected";
  private static final String DEVICE_NOT_FOUND = "No mobile found or USB development not enabled on mobile";

  private LaunchTargetComposite launchTargetGroup;
  private Button dartBrowserButton;
  private Button installDartBrowserButton;
  private Label statusLabel;

  private boolean deviceConnected = false;
  private final AtomicBoolean monitorDeviceConnection = new AtomicBoolean(false);

  @Override
  public void activated(ILaunchConfigurationWorkingCopy workingCopy) {
    startMonitorDeviceConnectionInBackground(statusLabel.getDisplay());
    super.activated(workingCopy);
  }

  @Override
  public void createControl(Composite parent) {
    Composite composite = new Composite(parent, SWT.NONE);
    GridLayoutFactory.swtDefaults().spacing(1, 3).applyTo(composite);

    launchTargetGroup = new LaunchTargetComposite(composite, SWT.NONE);
    launchTargetGroup.addListener(SWT.Modify, new Listener() {

      @Override
      public void handleEvent(Event event) {
        notifyPanelChanged();
      }
    });

    // Browser selection group
    Group group = new Group(composite, SWT.NONE);
    group.setText("Browser settings");
    GridDataFactory.fillDefaults().grab(true, false).applyTo(group);
    GridLayoutFactory.swtDefaults().numColumns(3).applyTo(group);
    ((GridLayout) group.getLayout()).marginBottom = 4;

    dartBrowserButton = new Button(group, SWT.CHECK);
    dartBrowserButton.setText("Launch in a Dart Content Shell Browser");
    dartBrowserButton.addSelectionListener(new SelectionAdapter() {
      @Override
      public void widgetSelected(SelectionEvent e) {
        notifyPanelChanged();
        installDartBrowserButton.setEnabled(dartBrowserButton.getSelection());
        installDartBrowserButton.setSelection(dartBrowserButton.getSelection());
      }
    });
    GridDataFactory.swtDefaults().span(2, 1).grab(true, false).applyTo(dartBrowserButton);

    installDartBrowserButton = new Button(group, SWT.CHECK);
    installDartBrowserButton.setText("Automatically install Dart Content Shell Browser on first mobile launch");
    GridDataFactory.swtDefaults().span(2, 1).grab(true, false).indent(20, 0).applyTo(
        installDartBrowserButton);

    // Status and setup group
    group = new Group(composite, SWT.NONE);
    group.setText("Status and first time setup");
    GridDataFactory.fillDefaults().grab(true, false).applyTo(group);
    GridLayoutFactory.swtDefaults().numColumns(1).applyTo(group);
    ((GridLayout) group.getLayout()).marginBottom = 4;

    statusLabel = new Label(group, SWT.NONE);
    GridDataFactory.fillDefaults().grab(true, false).applyTo(statusLabel);
    statusLabel.setText(MOBILE_STATUS_PREFIX);
    statusLabel.addDisposeListener(new DisposeListener() {
      @Override
      public void widgetDisposed(DisposeEvent e) {
        stopMonitorDeviceConnectionInBackground();
      }
    });

    setControl(composite);
  }

  @Override
  public void dispose() {
    Control control = getControl();

    if (control != null) {
      control.dispose();
      setControl(null);
    }

  }

  @Override
  public String getErrorMessage() {
    if (performSdkCheck() != null) {
      return performSdkCheck();
    }
    if (!deviceConnected) {
      return DEVICE_NOT_FOUND;
    }
    return launchTargetGroup.getErrorMessage();
  }

  /**
   * Answer the image to show in the configuration tab or <code>null</code> if none
   */
  @Override
  public Image getImage() {
    return DartDebugUIPlugin.getImage("android.png"); //$NON-NLS-1$
  }

  @Override
  public String getMessage() {
    return "Create a configuration to launch a Dart application on a device."
        + "This installs a browser with the Dart VM on the device and launches the app in it.";
  }

  /**
   * Answer the name to show in the configuration tab
   */
  @Override
  public String getName() {
    return "Main";
  }

  /**
   * Initialize the UI from the specified configuration
   */
  @Override
  public void initializeFrom(ILaunchConfiguration config) {

    DartLaunchConfigWrapper wrapper = new DartLaunchConfigWrapper(config);
    launchTargetGroup.setHtmlTextValue(wrapper.appendQueryParams(wrapper.getApplicationName()));
    launchTargetGroup.setUrlTextValue(wrapper.getUrl());

    launchTargetGroup.setSourceDirectoryTextValue(wrapper.getSourceDirectoryName());

    if (wrapper.getShouldLaunchFile()) {
      launchTargetGroup.setHtmlButtonSelection(true);
    } else {
      launchTargetGroup.setHtmlButtonSelection(false);
    }

    dartBrowserButton.setSelection(wrapper.getLaunchContentShell());
    installDartBrowserButton.setSelection(wrapper.getInstallContentShell());
  }

  /**
   * Store the value specified in the UI into the launch configuration
   */
  @Override
  public void performApply(ILaunchConfigurationWorkingCopy config) {

    DartLaunchConfigWrapper wrapper = new DartLaunchConfigWrapper(config);
    wrapper.setShouldLaunchFile(launchTargetGroup.getHtmlButtonSelection());

    String fileUrl = launchTargetGroup.getHtmlFileName();

    if (fileUrl.indexOf('?') == -1) {
      wrapper.setApplicationName(fileUrl);
      wrapper.setUrlQueryParams("");
    } else {
      int index = fileUrl.indexOf('?');

      wrapper.setApplicationName(fileUrl.substring(0, index));
      wrapper.setUrlQueryParams(fileUrl.substring(index + 1));
    }

    wrapper.setUrl(launchTargetGroup.getUrlString());
    wrapper.setSourceDirectoryName(launchTargetGroup.getSourceDirectory());
    wrapper.setLaunchContentShell(dartBrowserButton.getSelection());
    wrapper.setInstallContentShell(installDartBrowserButton.getSelection());
  }

  @Override
  public void setDefaults(ILaunchConfigurationWorkingCopy configuration) {
    DartLaunchConfigWrapper wrapper = new DartLaunchConfigWrapper(configuration);
    wrapper.setShouldLaunchFile(true);
    wrapper.setApplicationName(""); //$NON-NLS-1$
    wrapper.setLaunchContentShell(true);
  }

  private void notifyPanelChanged() {
    setDirty(true);
    updateLaunchConfigurationDialog();
  }

  private String performSdkCheck() {
    if (!DartSdkManager.getManager().hasSdk()) {
      return "Dart SDK is not installed ("
          + DartSdkManager.getManager().getSdk().getDart2JsExecutable() + ")";
    } else {
      return null;
    }
  }

  /**
   * Start the background process that monitors device connection via ADB
   */
  private void startMonitorDeviceConnectionInBackground(final Display display) {
    if (!monitorDeviceConnection.get()) {
      monitorDeviceConnection.set(true);
      Thread thread = new Thread("Monitor mobile connection") {
        @Override
        public void run() {

          AndroidDebugBridge devBridge = new AndroidDebugBridge();
          boolean wasDeviceConnected = devBridge.getConnectedDevice() != null;
          update(wasDeviceConnected);

          while (monitorDeviceConnection.get()) {
            final boolean isDeviceConnected = devBridge.getConnectedDevice() != null;
            if (wasDeviceConnected != isDeviceConnected) {
              wasDeviceConnected = isDeviceConnected;
              update(isDeviceConnected);
            }
            try {
              Thread.sleep(1000);
            } catch (InterruptedException e) {
              //$FALL-THROUGH$
            }
          }
        }

        private void update(final boolean isDeviceConnected) {
          display.asyncExec(new Runnable() {
            @Override
            public void run() {
              updateMobileStatus(isDeviceConnected);
            }
          });
        }
      };
      thread.setDaemon(true);
      thread.start();
    }
  }

  /**
   * Stop the background process that monitors device connection via ADB
   */
  private void stopMonitorDeviceConnectionInBackground() {
    monitorDeviceConnection.set(false);
  }

  /**
   * Update the mobile status. Must be called on the UI thread.
   * 
   * @param isDeviceConnected {@code true} if a mobile device is currently connected
   */
  private void updateMobileStatus(boolean isDeviceConnected) {
    deviceConnected = isDeviceConnected;
    if (statusLabel != null && !statusLabel.isDisposed()) {
      statusLabel.setText(MOBILE_STATUS_PREFIX
          + (deviceConnected ? DEVICE_CONNECTED : DEVICE_NOT_FOUND));
      updateLaunchConfigurationDialog();
    }
  }
}
