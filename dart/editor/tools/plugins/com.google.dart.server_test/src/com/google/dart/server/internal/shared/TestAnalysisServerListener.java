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

package com.google.dart.server.internal.shared;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.dart.engine.error.ErrorCode;
import com.google.dart.engine.source.Source;
import com.google.dart.server.AnalysisError;
import com.google.dart.server.AnalysisServerError;
import com.google.dart.server.AnalysisServerErrorCode;
import com.google.dart.server.AnalysisServerListener;
import com.google.dart.server.AnalysisStatus;
import com.google.dart.server.Element;
import com.google.dart.server.HighlightRegion;
import com.google.dart.server.NavigationRegion;
import com.google.dart.server.Outline;
import com.google.dart.server.internal.local.asserts.NavigationRegionsAssert;

import junit.framework.Assert;
import junit.framework.AssertionFailedError;

import static org.fest.assertions.Assertions.assertThat;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Mock implementation of {@link AnalysisServerListener}.
 */
public class TestAnalysisServerListener implements AnalysisServerListener {
  private final List<AnalysisServerError> serverErrors = Lists.newArrayList();
  private final Map<String, AnalysisError[]> sourcesErrors = Maps.newHashMap();
  private final Map<String, HighlightRegion[]> highlightsMap = Maps.newHashMap();
  private final Map<String, NavigationRegion[]> navigationMap = Maps.newHashMap();
  private final Map<String, Outline> outlineMap = Maps.newHashMap();

  /**
   * Assert that the number of errors that have been gathered matches the number of errors that are
   * given and that they have the expected error codes. The order in which the errors were gathered
   * is ignored.
   * 
   * @param file the file to check errors for
   * @param expectedErrorCodes the error codes of the errors that should have been gathered
   * @throws AssertionFailedError if a different number of errors have been gathered than were
   *           expected
   */
  public synchronized void assertErrorsWithCodes(String file, ErrorCode... expectedErrorCodes) {
    AnalysisError[] errors = getErrors(file);
    assertErrorsWithCodes(errors, expectedErrorCodes);
  }

  /**
   * Returns {@link NavigationRegionsAssert} for the given file.
   */
  public synchronized NavigationRegionsAssert assertNavigationRegions(String file) {
    return new NavigationRegionsAssert(getNavigationRegions(file));
  }

  /**
   * Asserts that there was no {@link AnalysisServerError} reported.
   */
  public synchronized void assertNoServerErrors() {
    assertThat(serverErrors).isEmpty();
  }

  /**
   * Assert that the number of {@link #serverErrors} that have been gathered matches the number of
   * errors that are given and that they have the expected error codes. The order in which the
   * errors were gathered is ignored.
   * 
   * @param expectedErrorCodes the error codes of the errors that should have been gathered
   * @throws AssertionFailedError if a different number of errors have been gathered than were
   *           expected
   */
  public synchronized void assertServerErrorsWithCodes(
      AnalysisServerErrorCode... expectedErrorCodes) {
    StringBuilder builder = new StringBuilder();
    //
    // Verify that the expected error codes have a non-empty message.
    //
    for (AnalysisServerErrorCode errorCode : expectedErrorCodes) {
      Assert.assertFalse("Empty error code message", errorCode.getMessage().isEmpty());
    }
    //
    // Compute the expected number of each type of error.
    //
    Map<AnalysisServerErrorCode, Integer> expectedCounts = Maps.newHashMap();
    for (AnalysisServerErrorCode code : expectedErrorCodes) {
      Integer count = expectedCounts.get(code);
      if (count == null) {
        count = Integer.valueOf(1);
      } else {
        count = Integer.valueOf(count.intValue() + 1);
      }
      expectedCounts.put(code, count);
    }
    //
    // Compute the actual number of each type of error.
    //
    Map<AnalysisServerErrorCode, List<AnalysisServerError>> errorsByCode = Maps.newHashMap();
    for (AnalysisServerError error : serverErrors) {
      AnalysisServerErrorCode code = error.getErrorCode();
      List<AnalysisServerError> list = errorsByCode.get(code);
      if (list == null) {
        list = Lists.newArrayList();
        errorsByCode.put(code, list);
      }
      list.add(error);
    }
    //
    // Compare the expected and actual number of each type of error.
    //
    for (Map.Entry<AnalysisServerErrorCode, Integer> entry : expectedCounts.entrySet()) {
      AnalysisServerErrorCode code = entry.getKey();
      int expectedCount = entry.getValue().intValue();
      int actualCount;
      List<AnalysisServerError> list = errorsByCode.remove(code);
      if (list == null) {
        actualCount = 0;
      } else {
        actualCount = list.size();
      }
      if (actualCount != expectedCount) {
        if (builder.length() == 0) {
          builder.append("Expected ");
        } else {
          builder.append("; ");
        }
        builder.append(expectedCount);
        builder.append(" errors of type ");
        builder.append(code.getClass().getSimpleName() + "." + code);
        builder.append(", found ");
        builder.append(actualCount);
      }
    }
    //
    // Check that there are no more errors in the actual-errors map, otherwise, record message.
    //
    for (Map.Entry<AnalysisServerErrorCode, List<AnalysisServerError>> entry : errorsByCode.entrySet()) {
      AnalysisServerErrorCode code = entry.getKey();
      List<AnalysisServerError> actualErrors = entry.getValue();
      int actualCount = actualErrors.size();
      if (builder.length() == 0) {
        builder.append("Expected ");
      } else {
        builder.append("; ");
      }
      builder.append("0 errors of type ");
      builder.append(code.getClass().getSimpleName() + "." + code);
      builder.append(", found ");
      builder.append(actualCount);
    }
    if (builder.length() > 0) {
      Assert.fail(builder.toString());
    }
  }

  /**
   * Removes all of reported {@link NavigationRegion}s.
   */
  public synchronized void clearNavigationRegions() {
    navigationMap.clear();
  }

  @Override
  public synchronized void computedErrors(String file, AnalysisError[] errors) {
    sourcesErrors.put(file, errors);
  }

  @Override
  public synchronized void computedHighlights(String file, HighlightRegion[] highlights) {
    highlightsMap.put(file, highlights);
  }

  @Override
  public synchronized void computedNavigation(String file, NavigationRegion[] targets) {
    navigationMap.put(file, targets);
  }

  @Override
  public synchronized void computedOutline(String file, Outline outline) {
    outlineMap.put(file, outline);
  }

  /**
   * Returns a navigation {@link Element} at the given position.
   */
  public synchronized Element findNavigationElement(String file, int offset) {
    NavigationRegion[] regions = getNavigationRegions(file);
    if (regions != null) {
      for (NavigationRegion navigationRegion : regions) {
        if (navigationRegion.containsInclusive(offset)) {
          return navigationRegion.getTargets()[0];
        }
      }
    }
    return null;
  }

  /**
   * Returns {@link AnalysisError} for the given file, may be empty, but not {@code null}.
   */
  public synchronized AnalysisError[] getErrors(String file) {
    AnalysisError[] errors = sourcesErrors.get(file);
    if (errors == null) {
      return AnalysisError.NO_ERRORS;
    }
    return errors;
  }

  /**
   * Returns {@link HighlightRegion}s for the given file, maybe {@code null} if have not been ever
   * notified.
   */
  public synchronized HighlightRegion[] getHighlightRegions(String file) {
    return highlightsMap.get(file);
  }

  /**
   * Returns {@link NavigationRegion}s for the given file, maybe {@code null} if have not been ever
   * notified.
   */
  public synchronized NavigationRegion[] getNavigationRegions(String file) {
    return navigationMap.get(file);
  }

  /**
   * Returns {@link Outline} for the given context and {@link Source}, maybe {@code null} if have
   * not been ever notified.
   */
  public synchronized Outline getOutline(String file) {
    return outlineMap.get(file);
  }

  @Override
  public void serverConnected() {
  }

  @Override
  public synchronized void serverError(AnalysisServerError error) {
    serverErrors.add(error);
  }

  @Override
  public void serverStatus(AnalysisStatus analysis) {
  }

  /**
   * Assert that the number of errors that have been given matches the number of errors that are
   * given and that they have the expected error codes. The order in which the errors were gathered
   * is ignored.
   * 
   * @param errors the errors to validate
   * @param expectedErrorCodes the error codes of the errors that should have been gathered
   * @throws AssertionFailedError if a different number of errors have been gathered than were
   *           expected
   */
  private void assertErrorsWithCodes(AnalysisError[] errors, ErrorCode... expectedErrorCodes) {
    StringBuilder builder = new StringBuilder();
    //
    // Verify that the expected error codes have a non-empty message.
    //
    for (ErrorCode errorCode : expectedErrorCodes) {
      Assert.assertFalse("Empty error code message", errorCode.getMessage().isEmpty());
    }
    //
    // Compute the expected number of each type of error.
    //
    HashMap<ErrorCode, Integer> expectedCounts = new HashMap<ErrorCode, Integer>();
    for (ErrorCode code : expectedErrorCodes) {
      Integer count = expectedCounts.get(code);
      if (count == null) {
        count = Integer.valueOf(1);
      } else {
        count = Integer.valueOf(count.intValue() + 1);
      }
      expectedCounts.put(code, count);
    }
    //
    // Compute the actual number of each type of error.
    //
    HashMap<ErrorCode, ArrayList<AnalysisError>> errorsByCode = new HashMap<ErrorCode, ArrayList<AnalysisError>>();
    for (AnalysisError error : errors) {
      ErrorCode code = error.getErrorCode();
      ArrayList<AnalysisError> list = errorsByCode.get(code);
      if (list == null) {
        list = new ArrayList<AnalysisError>();
        errorsByCode.put(code, list);
      }
      list.add(error);
    }
    //
    // Compare the expected and actual number of each type of error.
    //
    for (Map.Entry<ErrorCode, Integer> entry : expectedCounts.entrySet()) {
      ErrorCode code = entry.getKey();
      int expectedCount = entry.getValue().intValue();
      int actualCount;
      ArrayList<AnalysisError> list = errorsByCode.remove(code);
      if (list == null) {
        actualCount = 0;
      } else {
        actualCount = list.size();
      }
      if (actualCount != expectedCount) {
        if (builder.length() == 0) {
          builder.append("Expected ");
        } else {
          builder.append("; ");
        }
        builder.append(expectedCount);
        builder.append(" errors of type ");
        builder.append(code.getClass().getSimpleName() + "." + code);
        builder.append(", found ");
        builder.append(actualCount);
      }
    }
    //
    // Check that there are no more errors in the actual-errors map, otherwise, record message.
    //
    for (Map.Entry<ErrorCode, ArrayList<AnalysisError>> entry : errorsByCode.entrySet()) {
      ErrorCode code = entry.getKey();
      ArrayList<AnalysisError> actualErrors = entry.getValue();
      int actualCount = actualErrors.size();
      if (builder.length() == 0) {
        builder.append("Expected ");
      } else {
        builder.append("; ");
      }
      builder.append("0 errors of type ");
      builder.append(code.getClass().getSimpleName() + "." + code);
      builder.append(", found ");
      builder.append(actualCount);
      builder.append(" (");
      for (int i = 0; i < actualErrors.size(); i++) {
        AnalysisError error = actualErrors.get(i);
        if (i > 0) {
          builder.append(", ");
        }
        builder.append(error.getOffset());
      }
      builder.append(")");
    }
    if (builder.length() > 0) {
      Assert.fail(builder.toString());
    }
  }
}
