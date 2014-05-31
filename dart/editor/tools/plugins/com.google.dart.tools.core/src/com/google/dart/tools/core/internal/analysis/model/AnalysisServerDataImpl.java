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

package com.google.dart.tools.core.internal.analysis.model;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.google.dart.engine.error.ErrorCode;
import com.google.dart.server.AnalysisError;
import com.google.dart.server.AnalysisServer;
import com.google.dart.server.AnalysisService;
import com.google.dart.server.HighlightRegion;
import com.google.dart.server.NavigationRegion;
import com.google.dart.server.Outline;
import com.google.dart.tools.core.analysis.model.AnalysisServerData;
import com.google.dart.tools.core.analysis.model.AnalysisServerHighlightsListener;
import com.google.dart.tools.core.analysis.model.AnalysisServerOutlineListener;

import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Instances of {@code AnalysisServerData} manage and provide access to analysis results reported by
 * {@link AnalysisServer}.
 * 
 * @coverage dart.tools.core.model
 */
public class AnalysisServerDataImpl implements AnalysisServerData {
  // TODO(scheglov) restore or remove for the new API
//  private final Map<String, Set<Source>> navigationSubscriptions = Maps.newHashMap();
  private final Map<String, Set<AnalysisServerHighlightsListener>> highlightsSubscriptions = Maps.newHashMap();
  private final Map<String, Set<AnalysisServerOutlineListener>> outlineSubscriptions = Maps.newHashMap();
  private final Map<String, AnalysisError[]> errorData = Maps.newHashMap();
  private final Map<String, NavigationRegion[]> navigationData = Maps.newHashMap();
  private final Map<AnalysisService, List<String>> analysisSubscriptions = Maps.newHashMap();
  // TODO(scheglov) restore or remove for the new API
//  private final Map<String, Set<ErrorCode>> fixableErrorCodesData = Maps.newHashMap();

  private AnalysisServer server;

  @Override
  public AnalysisError[] getErrors(String file) {
    AnalysisError[] errors = errorData.get(file);
    if (errors == null) {
      return AnalysisError.NO_ERRORS;
    }
    return errors;
  }

  @Override
  public NavigationRegion[] getNavigation(String file) {
    NavigationRegion[] sourceRegions = navigationData.get(file);
    if (sourceRegions == null) {
      return NavigationRegion.EMPTY_ARRAY;
    }
    return sourceRegions;
  }

  @Override
  public boolean isFixableErrorCode(String file, ErrorCode errorCode) {
    // TODO(scheglov) restore or remove for the new API
    return false;
//    Set<ErrorCode> fixableErrorCodes = fixableErrorCodesData.get(contextId);
//    if (fixableErrorCodes == null) {
//      return false;
//    }
//    return fixableErrorCodes.contains(errorCode);
  }

  /**
   * Sets the {@link AnalysisServer} to talk to.
   */
  public void setServer(AnalysisServer server) {
    this.server = server;
  }

  @Override
  public void subscribeHighlights(String file, AnalysisServerHighlightsListener listener) {
    Set<AnalysisServerHighlightsListener> subscriptions = highlightsSubscriptions.get(file);
    if (subscriptions == null) {
      subscriptions = Sets.newHashSet();
      highlightsSubscriptions.put(file, subscriptions);
    }
    if (subscriptions.add(listener)) {
      addAnalysisSubscription(AnalysisService.HIGHLIGHTS, file);
    }
  }

  @Override
  public void subscribeNavigation(String file) {
    // TODO(scheglov) restore or remove for the new API
//    Set<Source> sources = navigationSubscriptions.get(contextId);
//    if (sources == null) {
//      sources = Sets.newHashSet();
//      navigationSubscriptions.put(contextId, sources);
//    }
//    if (sources.add(source)) {
//      server.subscribe(
//          contextId,
//          ImmutableMap.of(NotificationKind.NAVIGATION, ListSourceSet.create(sources)));
//    }
  }

  @Override
  public void subscribeOutline(String file, AnalysisServerOutlineListener listener) {
    // TODO(scheglov) restore or remove for the new API
//    Map<Source, Set<AnalysisServerOutlineListener>> sourceSubscriptions = outlineSubscriptions.get(contextId);
//    if (sourceSubscriptions == null) {
//      sourceSubscriptions = Maps.newHashMap();
//      outlineSubscriptions.put(contextId, sourceSubscriptions);
//    }
//    Set<AnalysisServerOutlineListener> subscriptions = sourceSubscriptions.get(source);
//    if (subscriptions == null) {
//      subscriptions = Sets.newHashSet();
//      sourceSubscriptions.put(source, subscriptions);
//    }
//    if (subscriptions.add(listener)) {
//      Set<Source> sourceSet = sourceSubscriptions.keySet();
//      server.subscribe(
//          contextId,
//          ImmutableMap.of(NotificationKind.OUTLINE, ListSourceSet.create(sourceSet)));
//    }
  }

  @Override
  public void unsubscribeHighlights(String file, AnalysisServerHighlightsListener listener) {
    Set<AnalysisServerHighlightsListener> subscriptions = highlightsSubscriptions.get(file);
    if (subscriptions == null) {
      return;
    }
    if (subscriptions.remove(listener)) {
      if (subscriptions.isEmpty()) {
        removeAnalysisSubscription(AnalysisService.HIGHLIGHTS, file);
      }
    }
  }

  @Override
  public void unsubscribeNavigation(String file) {
    // TODO(scheglov) restore or remove for the new API
//    Set<Source> sources = navigationSubscriptions.get(contextId);
//    if (sources == null) {
//      return;
//    }
//    if (sources.remove(source)) {
//      server.subscribe(
//          contextId,
//          ImmutableMap.of(NotificationKind.NAVIGATION, ListSourceSet.create(sources)));
//    }
  }

  @Override
  public void unsubscribeOutline(String file, AnalysisServerOutlineListener listener) {
    // TODO(scheglov) restore or remove for the new API
//    Map<Source, Set<AnalysisServerOutlineListener>> sourceSubscriptions = outlineSubscriptions.get(contextId);
//    if (sourceSubscriptions == null) {
//      return;
//    }
//    Set<AnalysisServerOutlineListener> subscriptions = sourceSubscriptions.get(source);
//    if (subscriptions == null) {
//      return;
//    }
//    if (subscriptions.remove(listener)) {
//      if (subscriptions.isEmpty()) {
//        sourceSubscriptions.remove(source);
//        Set<Source> sourceSet = sourceSubscriptions.keySet();
//        server.subscribe(
//            contextId,
//            ImmutableMap.of(NotificationKind.OUTLINE, ListSourceSet.create(sourceSet)));
//      }
//    }
  }

  void internalComputedErrors(String file, AnalysisError[] errors) {
    errorData.put(file, errors);
  }

  void internalComputedHighlights(String file, HighlightRegion[] highlights) {
    Set<AnalysisServerHighlightsListener> subscriptions = highlightsSubscriptions.get(file);
    if (subscriptions == null) {
      return;
    }
    subscriptions = ImmutableSet.copyOf(subscriptions);
    for (AnalysisServerHighlightsListener listener : subscriptions) {
      listener.computedHighlights(file, highlights);
    }
  }

  void internalComputedNavigation(String file, NavigationRegion[] targets) {
    navigationData.put(file, targets);
  }

  void internalComputedOutline(String file, Outline outline) {
    Set<AnalysisServerOutlineListener> subscriptions = outlineSubscriptions.get(file);
    if (subscriptions == null) {
      return;
    }
    subscriptions = ImmutableSet.copyOf(subscriptions);
    for (AnalysisServerOutlineListener listener : subscriptions) {
      listener.computedOutline(file, outline);
    }
  }

  /**
   * Remembers the {@link ErrorCode} that may be fixed in the given context.
   */
  void internalSetFixableErrorCodes(String file, ErrorCode[] errorCodes) {
    // TODO(scheglov) restore or remove for the new API
//    fixableErrorCodesData.put(contextId, Sets.newHashSet(errorCodes));
  }

  /**
   * Adds the given file to the subscription list for the given {@link AnalysisService}.
   */
  private void addAnalysisSubscription(AnalysisService service, String file) {
    List<String> files = analysisSubscriptions.get(service);
    if (files == null) {
      files = Lists.newArrayList();
      analysisSubscriptions.put(service, files);
    }
    if (!files.contains(file)) {
      files.add(file);
      server.setAnalysisSubscriptions(analysisSubscriptions);
    }
  }

  /**
   * Removes the given file from the subscription list for the given {@link AnalysisService}.
   */
  private void removeAnalysisSubscription(AnalysisService service, String file) {
    List<String> files = analysisSubscriptions.get(service);
    if (files == null) {
      return;
    }
    if (files.remove(file)) {
      if (files.isEmpty()) {
        analysisSubscriptions.remove(service);
      }
      server.setAnalysisSubscriptions(analysisSubscriptions);
    }
  }
}
