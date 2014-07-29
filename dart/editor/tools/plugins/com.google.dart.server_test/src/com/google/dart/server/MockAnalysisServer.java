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

import java.util.List;
import java.util.Map;

/**
 * Mock used for testing. Subclasses should override whatever methods they expect to be called.
 */
public class MockAnalysisServer implements AnalysisServer {

  @Override
  public void addAnalysisServerListener(AnalysisServerListener listener) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void applyRefactoring(String refactoringId, RefactoringApplyConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void createRefactoring(String refactoringKind, String file, int offset, int length,
      RefactoringCreateConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void deleteRefactoring(String refactoringId) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getAssists(String file, int offset, int length, AssistsConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getCompletionSuggestions(String file, int offset, CompletionIdConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getErrors(String file, AnalysisErrorsConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getFixes(List<AnalysisError> errors, FixesConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getHover(String string, int i, HoverConsumer hoverConsumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getTypeHierarchy(Location location, TypeHierarchyConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void getVersion(VersionConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void reanalyze() {
    throw new UnsupportedOperationException();
  }

  @Override
  public void removeAnalysisServerListener(AnalysisServerListener listener) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void searchClassMemberDeclarations(String name, SearchIdConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void searchClassMemberReferences(String name, SearchIdConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void searchElementReferences(String file, int offset, boolean includePotential,
      SearchIdConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void searchTopLevelDeclarations(String pattern, SearchIdConsumer consumer) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void setAnalysisRoots(List<String> includedPaths, List<String> excludedPaths) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void setAnalysisSubscriptions(Map<AnalysisService, List<String>> subscriptions) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void setPriorityFiles(List<String> files) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void setRefactoringExtractLocalOptions(
//      String refactoringId, boolean allOccurrences,
//      String name, RefactoringOptionsValidationConsumer consumer
  ) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void setRefactoringExtractMethodOptions(
//      String refactoringId, String name,
//      boolean asGetter, boolean allOccurrences, Parameter[] parameters,
//      RefactoringExtractMethodOptionsValidationConsumer consumer
  ) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void setServerSubscriptions(List<ServerService> subscriptions) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void shutdown() {
    throw new UnsupportedOperationException();
  }

  @Override
  public void start(long millisToRestart) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void updateAnalysisOptions(AnalysisOptions options) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void updateContent(Map<String, ContentChange> files) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void updateSdks(List<String> added, List<String> removed, String defaultSdk) {
    throw new UnsupportedOperationException();
  }
}
