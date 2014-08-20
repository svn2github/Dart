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
 *
 * This file has been automatically generated.  Please do not edit it manually.
 * To regenerate the file, use the script "pkg/analysis_server/spec/generate_files".
 */
package com.google.dart.server.generated.types;

import java.util.Arrays;
import java.util.List;
import java.util.Map;
import com.google.dart.server.utilities.general.ObjectUtilities;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import org.apache.commons.lang3.StringUtils;

/**
 * The hover information associated with a specific location.
 *
 * @coverage dart.server.generated.types
 */
@SuppressWarnings("unused")
public class HoverInformation {

  /**
   * An empty array of {@link HoverInformation}s.
   */
  public static final HoverInformation[] EMPTY_ARRAY = new HoverInformation[0];

  /**
   * The offset of the range of characters that encompases the cursor position and has the same hover
   * information as the cursor position.
   */
  private final Integer offset;

  /**
   * The length of the range of characters that encompases the cursor position and has the same hover
   * information as the cursor position.
   */
  private final Integer length;

  /**
   * The path to the defining compilation unit of the library in which the referenced element is
   * declared. This data is omitted if there is no referenced element.
   */
  private final String containingLibraryPath;

  /**
   * The name of the library in which the referenced element is declared. This data is omitted if
   * there is no referenced element.
   */
  private final String containingLibraryName;

  /**
   * The dartdoc associated with the referenced element. Other than the removal of the comment
   * delimiters, including leading asterisks in the case of a block comment, the dartdoc is
   * unprocessed markdown. This data is omitted if there is no referenced element.
   */
  private final String dartdoc;

  /**
   * A human-readable description of the element being referenced. This data is omitted if there is
   * no referenced element.
   */
  private final String elementDescription;

  /**
   * A human-readable description of the kind of element being referenced (such as “class” or
   * “function type alias”). This data is omitted if there is no referenced element.
   */
  private final String elementKind;

  /**
   * A human-readable description of the parameter corresponding to the expression being hovered
   * over. This data is omitted if the location is not in an argument to a function.
   */
  private final String parameter;

  /**
   * The name of the propagated type of the expression. This data is omitted if the location does not
   * correspond to an expression or if there is no propagated type information.
   */
  private final String propagatedType;

  /**
   * The name of the static type of the expression. This data is omitted if the location does not
   * correspond to an expression.
   */
  private final String staticType;

  /**
   * Constructor for {@link HoverInformation}.
   */
  public HoverInformation(Integer offset, Integer length, String containingLibraryPath, String containingLibraryName, String dartdoc, String elementDescription, String elementKind, String parameter, String propagatedType, String staticType) {
    this.offset = offset;
    this.length = length;
    this.containingLibraryPath = containingLibraryPath;
    this.containingLibraryName = containingLibraryName;
    this.dartdoc = dartdoc;
    this.elementDescription = elementDescription;
    this.elementKind = elementKind;
    this.parameter = parameter;
    this.propagatedType = propagatedType;
    this.staticType = staticType;
  }

  @Override
  public boolean equals(Object obj) {
    if (obj instanceof HoverInformation) {
      HoverInformation other = (HoverInformation) obj;
      return
        other.offset == offset &&
        other.length == length &&
        ObjectUtilities.equals(other.containingLibraryPath, containingLibraryPath) &&
        ObjectUtilities.equals(other.containingLibraryName, containingLibraryName) &&
        ObjectUtilities.equals(other.dartdoc, dartdoc) &&
        ObjectUtilities.equals(other.elementDescription, elementDescription) &&
        ObjectUtilities.equals(other.elementKind, elementKind) &&
        ObjectUtilities.equals(other.parameter, parameter) &&
        ObjectUtilities.equals(other.propagatedType, propagatedType) &&
        ObjectUtilities.equals(other.staticType, staticType);
    }
    return false;
  }

  /**
   * The name of the library in which the referenced element is declared. This data is omitted if
   * there is no referenced element.
   */
  public String getContainingLibraryName() {
    return containingLibraryName;
  }

  /**
   * The path to the defining compilation unit of the library in which the referenced element is
   * declared. This data is omitted if there is no referenced element.
   */
  public String getContainingLibraryPath() {
    return containingLibraryPath;
  }

  /**
   * The dartdoc associated with the referenced element. Other than the removal of the comment
   * delimiters, including leading asterisks in the case of a block comment, the dartdoc is
   * unprocessed markdown. This data is omitted if there is no referenced element.
   */
  public String getDartdoc() {
    return dartdoc;
  }

  /**
   * A human-readable description of the element being referenced. This data is omitted if there is
   * no referenced element.
   */
  public String getElementDescription() {
    return elementDescription;
  }

  /**
   * A human-readable description of the kind of element being referenced (such as “class” or
   * “function type alias”). This data is omitted if there is no referenced element.
   */
  public String getElementKind() {
    return elementKind;
  }

  /**
   * The length of the range of characters that encompases the cursor position and has the same hover
   * information as the cursor position.
   */
  public Integer getLength() {
    return length;
  }

  /**
   * The offset of the range of characters that encompases the cursor position and has the same hover
   * information as the cursor position.
   */
  public Integer getOffset() {
    return offset;
  }

  /**
   * A human-readable description of the parameter corresponding to the expression being hovered
   * over. This data is omitted if the location is not in an argument to a function.
   */
  public String getParameter() {
    return parameter;
  }

  /**
   * The name of the propagated type of the expression. This data is omitted if the location does not
   * correspond to an expression or if there is no propagated type information.
   */
  public String getPropagatedType() {
    return propagatedType;
  }

  /**
   * The name of the static type of the expression. This data is omitted if the location does not
   * correspond to an expression.
   */
  public String getStaticType() {
    return staticType;
  }

  public JsonObject toJson() {
    JsonObject jsonObject = new JsonObject();
    jsonObject.addProperty("offset", offset);
    jsonObject.addProperty("length", length);
    if (containingLibraryPath != null) {
      jsonObject.addProperty("containingLibraryPath", containingLibraryPath);
    }
    if (containingLibraryName != null) {
      jsonObject.addProperty("containingLibraryName", containingLibraryName);
    }
    if (dartdoc != null) {
      jsonObject.addProperty("dartdoc", dartdoc);
    }
    if (elementDescription != null) {
      jsonObject.addProperty("elementDescription", elementDescription);
    }
    if (elementKind != null) {
      jsonObject.addProperty("elementKind", elementKind);
    }
    if (parameter != null) {
      jsonObject.addProperty("parameter", parameter);
    }
    if (propagatedType != null) {
      jsonObject.addProperty("propagatedType", propagatedType);
    }
    if (staticType != null) {
      jsonObject.addProperty("staticType", staticType);
    }
    return jsonObject;
  }

  @Override
  public String toString() {
    StringBuilder builder = new StringBuilder();
    builder.append("[");
    builder.append("offset=");
    builder.append(offset + ", ");
    builder.append("length=");
    builder.append(length + ", ");
    builder.append("containingLibraryPath=");
    builder.append(containingLibraryPath + ", ");
    builder.append("containingLibraryName=");
    builder.append(containingLibraryName + ", ");
    builder.append("dartdoc=");
    builder.append(dartdoc + ", ");
    builder.append("elementDescription=");
    builder.append(elementDescription + ", ");
    builder.append("elementKind=");
    builder.append(elementKind + ", ");
    builder.append("parameter=");
    builder.append(parameter + ", ");
    builder.append("propagatedType=");
    builder.append(propagatedType + ", ");
    builder.append("staticType=");
    builder.append(staticType);
    builder.append("]");
    return builder.toString();
  }

}
