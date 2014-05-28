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
package com.google.dart.server.internal.remote;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Maps;
import com.google.dart.engine.services.refactoring.Parameter;
import com.google.dart.server.AnalysisError;
import com.google.dart.server.AnalysisOptions;
import com.google.dart.server.AnalysisServer;
import com.google.dart.server.AnalysisServerListener;
import com.google.dart.server.AnalysisService;
import com.google.dart.server.CompletionSuggestionsConsumer;
import com.google.dart.server.Consumer;
import com.google.dart.server.ContentChange;
import com.google.dart.server.Element;
import com.google.dart.server.FixesConsumer;
import com.google.dart.server.MinorRefactoringsConsumer;
import com.google.dart.server.RefactoringApplyConsumer;
import com.google.dart.server.RefactoringExtractLocalConsumer;
import com.google.dart.server.RefactoringExtractMethodConsumer;
import com.google.dart.server.RefactoringExtractMethodOptionsValidationConsumer;
import com.google.dart.server.RefactoringOptionsValidationConsumer;
import com.google.dart.server.SearchResultsConsumer;
import com.google.dart.server.ServerService;
import com.google.dart.server.TypeHierarchyConsumer;
import com.google.dart.server.VersionConsumer;
import com.google.dart.server.internal.local.BroadcastAnalysisServerListener;
import com.google.dart.server.internal.remote.processor.NotificationErrorsProcessor;
import com.google.dart.server.internal.remote.utilities.RequestUtilities;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;

import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * This {@link AnalysisServer} calls out to the analysis server written in Dart and communicates
 * with the server over standard IO streams.
 * 
 * @coverage dart.server.remote
 */
public class RemoteAnalysisServerImpl implements AnalysisServer {
  /**
   * For requests that do not have a {@link Consumer}, this object is created as a place holder so
   * that if an error occurs after the request, an error can be reported.
   */
  public static class LocalConsumer implements Consumer {
    private final JsonObject request;

    public LocalConsumer(JsonObject request) {
      this.request = request;
    }

    @SuppressWarnings("unused")
    private JsonObject getRequest() {
      return request;
    }

  }

  /**
   * A thread which reads responses from the {@link ResponseStream} and calls the associated
   * {@link Consumer}s from {@link RemoteAnalysisServerImpl#consumerMap}.
   */
  public class ServerResponseReaderThread extends Thread {
    public ServerResponseReaderThread() {
      setDaemon(true);
      setName("ServerResponseReaderThread");
    }

    @Override
    public void run() {
      while (true) {
        try {
          JsonObject response = responseStream.take();
          try {
            processResponse(response);
          } finally {
            responseStream.lastRequestProcessed();
          }
        } catch (Throwable e) {
          // TODO(scheglov) decide how to handle exceptions
          e.printStackTrace();
        }
      }
    }
  }

  private static final String ANALYSIS_NOTIFICATION_ERRORS = "analysis.errors";

  private final RequestSink requestSink;

  private final ResponseStream responseStream;

  /**
   * The listener that will receive notification when new analysis results become available.
   */
  private final BroadcastAnalysisServerListener listener = new BroadcastAnalysisServerListener();

  /**
   * A mapping between {@link String} ids' and the associated {@link Consumer} that was passed when
   * the request was made.
   */
  private final Map<String, Consumer> consumerMap = Maps.newHashMap();

  /**
   * The object used to synchronize access to {@link #consumerMap}.
   */
  private final Object consumerMapLock = new Object();

  /**
   * The unique ID for the next request.
   */
  private final AtomicInteger nextId = new AtomicInteger();

  public RemoteAnalysisServerImpl(RequestSink requestSink, ResponseStream responseStream) {
    this.requestSink = requestSink;
    this.responseStream = responseStream;
    new ServerResponseReaderThread().start();
  }

  @Override
  public void addAnalysisServerListener(AnalysisServerListener listener) {
    this.listener.addListener(listener);
  }

  @Override
  public void applyRefactoring(String refactoringId, RefactoringApplyConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void createRefactoringExtractLocal(String file, int offset, int length,
      RefactoringExtractLocalConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void createRefactoringExtractMethod(String file, int offset, int length,
      RefactoringExtractMethodConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void deleteRefactoring(String refactoringId) {
    // TODO(scheglov) implement
  }

  @Override
  public void getCompletionSuggestions(String file, int offset,
      CompletionSuggestionsConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void getFixes(List<AnalysisError> errors, FixesConsumer consumer) {
    String id = generateUniqueId();
    sendRequestToServer(id, RequestUtilities.generateAnalysisGetFixes(id, errors), consumer);
  }

  @Override
  public void getMinorRefactorings(String file, int offset, int length,
      MinorRefactoringsConsumer consumer) {
    String id = generateUniqueId();
    sendRequestToServer(
        id,
        RequestUtilities.generateAnalysisGetMinorRefactoringsRequest(id, file, offset, length));
  }

  @Override
  public void getTypeHierarchy(Element element, TypeHierarchyConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void getVersion(VersionConsumer consumer) {
    String id = generateUniqueId();
    sendRequestToServer(id, RequestUtilities.generateServerGetVersionRequest(id), consumer);
  }

  @Override
  public void removeAnalysisServerListener(AnalysisServerListener listener) {
    this.listener.removeListener(listener);
  }

  @Override
  public void searchClassMemberDeclarations(String name, SearchResultsConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void searchClassMemberReferences(String name, SearchResultsConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void searchElementReferences(Element element, boolean withPotential,
      SearchResultsConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void searchTopLevelDeclarations(String pattern, SearchResultsConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void setAnalysisRoots(List<String> includedPaths, List<String> excludedPaths) {
    String id = generateUniqueId();
    sendRequestToServer(
        id,
        RequestUtilities.generateAnalysisSetAnalysisRoots(id, includedPaths, excludedPaths));
  }

  @Override
  public void setAnalysisSubscriptions(Map<AnalysisService, List<String>> subscriptions) {
    // TODO(scheglov) implement
  }

  @Override
  public void setPriorityFiles(List<String> files) {
    // TODO(scheglov) implement
  }

  @Override
  public void setRefactoringExtractLocalOptions(String refactoringId, boolean allOccurrences,
      String name, RefactoringOptionsValidationConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void setRefactoringExtractMethodOptions(String refactoringId, String name,
      boolean asGetter, boolean allOccurrences, Parameter[] parameters,
      RefactoringExtractMethodOptionsValidationConsumer consumer) {
    // TODO(scheglov) implement
  }

  @Override
  public void setServerSubscriptions(List<ServerService> subscriptions) {
    String id = generateUniqueId();
    sendRequestToServer(id, RequestUtilities.generateServerSetSubscriptions(id, subscriptions));
  }

  @Override
  public void shutdown() {
    String id = generateUniqueId();
    sendRequestToServer(id, RequestUtilities.generateServerShutdownRequest(id), null);
  }

  @VisibleForTesting
  public void test_waitForWorkerComplete() {
    while (!consumerMap.isEmpty()) {
      Thread.yield();
    }
  }

  @Override
  public void updateAnalysisOptions(AnalysisOptions options) {
    // TODO(scheglov) implement
  }

  @Override
  public void updateContent(Map<String, ContentChange> files) {
    String id = generateUniqueId();
    sendRequestToServer(id, RequestUtilities.generateAnalysisUpdateContent(id, files));
  }

  @Override
  public void updateSdks(List<String> added, List<String> removed, String defaultSdk) {
    // TODO(scheglov) implement
  }

  /**
   * Generate and return a unique {@link String} id to be used in the requests sent to the analysis
   * server.
   * 
   * @return a unique {@link String} id to be used in the requests sent to the analysis server
   */
  private String generateUniqueId() {
    return Integer.toString(nextId.getAndIncrement());
  }

  /**
   * Attempts to handle the given {@link JsonObject} as a notification.
   * 
   * @return {@code true} if it was handled or {@code false} otherwise
   */
  private boolean processNotification(JsonObject response) throws Exception {
    // prepare notification kind
    String event;
    {
      JsonElement eventElement = response.get("event");
      if (eventElement == null || !eventElement.isJsonPrimitive()) {
        return false;
      }
      event = eventElement.getAsString();
    }
    // handle each supported notification kind
    if (event.equals(ANALYSIS_NOTIFICATION_ERRORS)) {
      new NotificationErrorsProcessor(listener).process(response);
      return true;
    }
    // it is a notification, even if we did not handle it
    return true;
  }

  private void processResponse(JsonObject response) throws Exception {
    // handle notification
    if (processNotification(response)) {
      return;
    }
    // prepare ID
    JsonPrimitive idJsonPrimitive = (JsonPrimitive) response.get("id");
    if (idJsonPrimitive == null) {
      // TODO (jwren) handle this case
      return;
    }
    String idString = idJsonPrimitive.getAsString();
    // prepare consumer
    Consumer consumer = null;
    synchronized (consumerMapLock) {
      consumer = consumerMap.get(idString);
    }
    // TODO(jwren) handle error
    @SuppressWarnings("unused")
    JsonObject errorObject = (JsonObject) response.get("error");
    // handle result
    JsonObject resultObject = (JsonObject) response.get("result");
    if (consumer instanceof VersionConsumer) {
      processVersionConsumer((VersionConsumer) consumer, resultObject);
    }
    synchronized (consumerMapLock) {
      consumerMap.remove(idString);
    }
  }

  private void processVersionConsumer(VersionConsumer consumer, JsonObject resultObject) {
    String version = resultObject.get("version").getAsString();
    consumer.computedVersion(version);
  }

  /**
   * Sends the request, and associates the request with a {@link LocalConsumer}, a simple consumer
   * which only holds onto the the request {@link JsonObject}, for the purposes of error reporting.
   * 
   * @param id the identifier of the request
   * @param request the request to send
   */
  private void sendRequestToServer(String id, JsonObject request) {
    sendRequestToServer(id, request, new LocalConsumer(request));
  }

  /**
   * Sends the request and associates the request with the passed {@link Consumer}.
   * 
   * @param id the identifier of the request
   * @param request the request to send
   * @param consumer the {@link Consumer} to process a response
   */
  private void sendRequestToServer(String id, JsonObject request, Consumer consumer) {
    synchronized (consumerMapLock) {
      consumerMap.put(id, consumer);
    }
    requestSink.add(request);
  }

}
