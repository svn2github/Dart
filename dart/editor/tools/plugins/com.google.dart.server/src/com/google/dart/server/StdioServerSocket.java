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

import com.google.common.base.Preconditions;
import com.google.dart.server.internal.remote.ByteLineReaderStream;
import com.google.dart.server.internal.remote.ByteRequestSink;
import com.google.dart.server.internal.remote.ByteResponseStream;
import com.google.dart.server.internal.remote.RequestSink;
import com.google.dart.server.internal.remote.ResponseStream;

import java.io.IOException;
import java.io.PrintStream;
import java.net.ServerSocket;
import java.util.ArrayList;
import java.util.List;

/**
 * A remote server socket over standard input and output.
 * 
 * @coverage dart.server.remote
 */
public class StdioServerSocket {
  /**
   * Find and return an unused server socket port.
   */
  public static int findUnusedPort() {
    try {
      ServerSocket ss = new ServerSocket(0);
      int port = ss.getLocalPort();
      ss.close();
      return port;
    } catch (IOException ioe) {
      //$FALL-THROUGH$
    }
    return -1;
  }

  private final String runtimePath;
  private final String analysisServerPath;
  private final PrintStream debugStream;
  private RequestSink requestSink;
  private ResponseStream responseStream;
  private ByteLineReaderStream errorStream;
  private Process process;

  public StdioServerSocket(String runtimePath, String analysisServerPath, PrintStream debugStream) {
    this.runtimePath = runtimePath;
    this.analysisServerPath = analysisServerPath;
    this.debugStream = debugStream;
  }

  /**
   * Return the error stream.
   */
  public ByteLineReaderStream getErrorStream() {
    Preconditions.checkNotNull(errorStream, "Server is not started.");
    return errorStream;
  }

  /**
   * Return the request sink.
   */
  public RequestSink getRequestSink() {
    Preconditions.checkNotNull(requestSink, "Server is not started.");
    return requestSink;
  }

  /**
   * Return the response stream.
   */
  public ResponseStream getResponseStream() {
    Preconditions.checkNotNull(responseStream, "Server is not started.");
    return responseStream;
  }

  /**
   * Start the remote server and initialize request sink and response stream.
   */
  public void start(boolean debug) throws Exception {
    int debugPort = findUnusedPort();
    List<String> args = new ArrayList<String>();
    args.add(runtimePath);
    if (debug) {
      args.add("--debug:" + debugPort);
    }
    args.add(analysisServerPath);
    ProcessBuilder processBuilder = new ProcessBuilder(args.toArray(new String[args.size()]));
    process = processBuilder.start();
    requestSink = new ByteRequestSink(process.getOutputStream(), debugStream);
    responseStream = new ByteResponseStream(process.getInputStream(), debugStream);
    errorStream = new ByteLineReaderStream(process.getErrorStream());
    if (debug) {
      System.out.println("Analysis server debug port " + debugPort);
    }
  }

  /**
   * Wait up to 5 seconds for process to gracefully exit, then forcibly terminate the process if it
   * is still running.
   */
  public void stop() {
    if (process == null) {
      return;
    }
    final Process processToStop = process;
    process = null;
    long endTime = System.currentTimeMillis() + 5000;
    while (System.currentTimeMillis() < endTime) {
      try {
        int exit = processToStop.exitValue();
        if (exit != 0) {
          System.out.println("Non-zero exit code: " + exit + " for\n   " + analysisServerPath);
        }
        return;
      } catch (IllegalThreadStateException e) {
        //$FALL-THROUGH$
      }
      try {
        Thread.sleep(20);
      } catch (InterruptedException e) {
        //$FALL-THROUGH$
      }
    }
    processToStop.destroy();
    System.out.println("Terminated " + analysisServerPath);
  }
}
