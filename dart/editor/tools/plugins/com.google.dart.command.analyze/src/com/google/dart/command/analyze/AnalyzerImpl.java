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
package com.google.dart.command.analyze;

import com.google.dart.engine.AnalysisEngine;
import com.google.dart.engine.context.AnalysisContext;
import com.google.dart.engine.context.AnalysisException;
import com.google.dart.engine.element.CompilationUnitElement;
import com.google.dart.engine.element.LibraryElement;
import com.google.dart.engine.error.AnalysisError;
import com.google.dart.engine.error.ErrorSeverity;
import com.google.dart.engine.internal.context.AnalysisOptionsImpl;
import com.google.dart.engine.sdk.DartSdk;
import com.google.dart.engine.sdk.DirectoryBasedDartSdk;
import com.google.dart.engine.source.ContentCache;
import com.google.dart.engine.source.DartUriResolver;
import com.google.dart.engine.source.FileBasedSource;
import com.google.dart.engine.source.FileUriResolver;
import com.google.dart.engine.source.PackageUriResolver;
import com.google.dart.engine.source.Source;
import com.google.dart.engine.source.SourceFactory;
import com.google.dart.engine.source.UriKind;

import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Scans, parses, and analyzes a library.
 */
class AnalyzerImpl {
  private static final HashMap<File, DartSdk> sdkMap = new HashMap<File, DartSdk>();

  private static ErrorSeverity getMaxErrorSeverity(List<AnalysisError> errors) {
    ErrorSeverity status = ErrorSeverity.NONE;

    for (AnalysisError error : errors) {
      ErrorSeverity severity = error.getErrorCode().getErrorSeverity();

      status = status.max(severity);
    }

    return status;
  }

  /**
   * @return the new or cached instance of the {@link DartSdk} with the given directory.
   */
  private static DartSdk getSdk(File sdkDirectory) {
    DartSdk sdk = sdkMap.get(sdkDirectory);
    if (sdk == null) {
      sdk = new DirectoryBasedDartSdk(sdkDirectory);
      sdkMap.put(sdkDirectory, sdk);
    }
    return sdk;
  }

  private AnalyzerOptions options;
  private DartSdk sdk;

  public AnalyzerImpl(AnalyzerOptions options) {
    this.options = options;
    this.sdk = getSdk(options.getDartSdkPath());
  }

  /**
   * Treats the {@code sourceFile} as the top level library and analyzes the unit for warnings and
   * errors.
   * 
   * @param sourceFile file to analyze
   * @param options configuration for this analysis pass
   * @param errors the list to add errors to
   * @return {@code  true} on success, {@code false} on failure.
   */
  public ErrorSeverity analyze(File sourceFile, List<AnalysisError> errors) throws IOException,
      AnalysisException {
    if (sourceFile == null) {
      throw new IllegalArgumentException("sourceFile cannot be null");
    }

    // prepare "packages" directory
    File packageDirectory;
    if (options.getPackageRootPath() != null) {
      packageDirectory = options.getPackageRootPath();
    } else {
      packageDirectory = getPackageDirectoryFor(sourceFile);
    }

    // create SourceFactory
    SourceFactory sourceFactory;
    ContentCache contentCache = new ContentCache();
    if (packageDirectory != null) {
      sourceFactory = new SourceFactory(
          contentCache,
          new DartUriResolver(sdk),
          new FileUriResolver(),
          new PackageUriResolver(packageDirectory.getAbsoluteFile()));
    } else {
      sourceFactory = new SourceFactory(new DartUriResolver(sdk), new FileUriResolver());
    }

    // create options for context
    AnalysisOptionsImpl contextOptions = new AnalysisOptionsImpl();
    contextOptions.setAudit(options.getAudit());
    //TODO (danrubel): Enable strict mode by default when it is ready
    //contextOptions.setStrictMode(true);

    // prepare AnalysisContext
    AnalysisContext context = AnalysisEngine.getInstance().createAnalysisContext();
    context.setSourceFactory(sourceFactory);
    context.setAnalysisOptions(contextOptions);

    // analyze the given file
    Source librarySource = new FileBasedSource(contentCache, sourceFile.getAbsoluteFile());
    LibraryElement library = context.computeLibraryElement(librarySource);
    context.resolveCompilationUnit(librarySource, library);

    // prepare errors
    Set<Source> sources = getAllSources(library);
    getAllErrors(context, sources, errors);
    return getMaxErrorSeverity(errors);
  }

  Set<Source> getAllSources(LibraryElement library) {
    Set<CompilationUnitElement> units = new HashSet<CompilationUnitElement>();
    Set<LibraryElement> libraries = new HashSet<LibraryElement>();
    Set<Source> sources = new HashSet<Source>();

    addLibrary(library, libraries, units, sources);

    return sources;
  }

  private void addCompilationUnit(CompilationUnitElement unit, Set<LibraryElement> libraries,
      Set<CompilationUnitElement> units, Set<Source> sources) {
    if (unit == null || units.contains(unit)) {
      return;
    }

    units.add(unit);

    sources.add(unit.getSource());
  }

  private void addLibrary(LibraryElement library, Set<LibraryElement> libraries,
      Set<CompilationUnitElement> units, Set<Source> sources) {
    if (library == null || libraries.contains(library)) {
      return;
    }

    UriKind uriKind = library.getSource().getUriKind();

    // Optionally skip package: libraries.
    if (!options.getShowPackageWarnings() && uriKind == UriKind.PACKAGE_URI) {
      return;
    }

    // Optionally skip SDK libraries.
    if (!options.getShowSdkWarnings() && uriKind == UriKind.DART_URI) {
      return;
    }

    libraries.add(library);

    // add compilation units
    addCompilationUnit(library.getDefiningCompilationUnit(), libraries, units, sources);

    for (CompilationUnitElement child : library.getParts()) {
      addCompilationUnit(child, libraries, units, sources);
    }

    // add referenced libraries
    for (LibraryElement child : library.getImportedLibraries()) {
      addLibrary(child, libraries, units, sources);
    }

    for (LibraryElement child : library.getExportedLibraries()) {
      addLibrary(child, libraries, units, sources);
    }
  }

  private void getAllErrors(AnalysisContext context, Set<Source> sources, List<AnalysisError> errors) {
    for (Source source : sources) {
      AnalysisError[] sourceErrors = context.getErrors(source).getErrors();
      errors.addAll(Arrays.asList(sourceErrors));
    }
  }

  private File getPackageDirectoryFor(File sourceFile) {
    // we are going to ask parent file, so get absolute path
    sourceFile = sourceFile.getAbsoluteFile();

    // look in the containing directories
    File dir = sourceFile.getParentFile();
    while (dir != null) {
      File packagesDir = new File(dir, "packages");
      if (packagesDir.exists()) {
        return packagesDir;
      }
      dir = dir.getParentFile();
    }

    return null;
  }

}
