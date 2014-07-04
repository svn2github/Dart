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
package com.google.dart.server.internal.remote.processor;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Lists;
import com.google.dart.server.AnalysisServerListener;
import com.google.dart.server.Element;
import com.google.dart.server.ElementKind;
import com.google.dart.server.Location;
import com.google.dart.server.internal.ElementImpl;
import com.google.dart.server.internal.LocationImpl;
import com.google.dart.server.utilities.general.StringUtilities;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;

import java.util.Iterator;
import java.util.List;

/**
 * Abstract processor class which holds the {@link AnalysisServerListener} for all processors.
 * 
 * @coverage dart.server.remote
 */
public abstract class NotificationProcessor extends JsonProcessor {
  /**
   * Return the {@link ElementKind} code for the given name. If the passed name cannot be found, an
   * {@link IllegalArgumentException} is thrown.
   */
  @VisibleForTesting
  public static ElementKind getElementKind(String kindName) {
    return ElementKind.valueOf(kindName);
  }

  private final AnalysisServerListener listener;

  public NotificationProcessor(AnalysisServerListener listener) {
    this.listener = listener;
  }

  /**
   * Process the given {@link JsonObject} notification and notify {@link #listener}.
   */
  public abstract void process(JsonObject response) throws Exception;

  protected Element constructElement(JsonObject elementObject) {
    ElementKind kind = getElementKind(elementObject.get("kind").getAsString());
    String name = elementObject.get("name").getAsString();
    Location location = constructLocation(elementObject.get("location").getAsJsonObject());
    int flags = elementObject.get("flags").getAsInt();
    String parameters = safelyGetAsString(elementObject, "parameters");
    String returnType = safelyGetAsString(elementObject, "returnType");
    return new ElementImpl(kind, name, location, flags, parameters, returnType);
  }

  /**
   * Given some {@link JsonArray} and of {@code int} primitives, return the {@code int[]}.
   * 
   * @param intJsonArray some {@link JsonArray} of {@code int}s
   * @return the {@code int[]}
   */
  protected int[] constructIntArray(JsonArray intJsonArray) {
    if (intJsonArray == null) {
      return new int[] {};
    }
    int i = 0;
    int[] ints = new int[intJsonArray.size()];
    Iterator<JsonElement> iterator = intJsonArray.iterator();
    while (iterator.hasNext()) {
      ints[i] = iterator.next().getAsInt();
      i++;
    }
    return ints;
  }

  protected Location constructLocation(JsonObject locationObject) {
    String file = locationObject.get("file").getAsString();
    int offset = locationObject.get("offset").getAsInt();
    int length = locationObject.get("length").getAsInt();
    int startLine = locationObject.get("startLine").getAsInt();
    int startColumn = locationObject.get("startColumn").getAsInt();
    return new LocationImpl(file, offset, length, startLine, startColumn);
  }

  /**
   * Given some {@link JsonArray} and of string primitives, return the {@link String} array.
   * 
   * @param strJsonArray some {@link JsonArray} of {@link String}s
   * @return the {@link String} array
   */
  protected String[] constructStringArray(JsonArray strJsonArray) {
    if (strJsonArray == null) {
      return StringUtilities.EMPTY_ARRAY;
    }
    List<String> strings = Lists.newArrayList();
    Iterator<JsonElement> iterator = strJsonArray.iterator();
    while (iterator.hasNext()) {
      strings.add(iterator.next().getAsString());
    }
    return strings.toArray(new String[strings.size()]);
  }

  protected AnalysisServerListener getListener() {
    return listener;
  }
}
