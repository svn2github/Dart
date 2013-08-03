/*
 * Copyright (c) 2013, the Dart project authors.
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

package com.google.dart.engine.context;

/**
 * Container with statistics about the {@link AnalysisContext}.
 */
public interface AnalysisContentStatistics {
  /**
   * Information about single item in the cache.
   */
  public static interface CacheRow {
    int getErrorCount();

    int getFlushedCount();

    int getInProcessCount();

    int getInvalidCount();

    String getName();

    int getValidCount();
  }

  /**
   * @return the statistics for each kind of cached data.
   */
  CacheRow[] getCacheRows();
}
