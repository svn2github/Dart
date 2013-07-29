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
package com.google.dart.engine.internal.cache;

import com.google.dart.engine.ast.CompilationUnit;
import com.google.dart.engine.element.LibraryElement;
import com.google.dart.engine.error.AnalysisError;
import com.google.dart.engine.internal.context.CacheState;
import com.google.dart.engine.internal.scope.Namespace;
import com.google.dart.engine.source.Source;
import com.google.dart.engine.source.SourceKind;
import com.google.dart.engine.utilities.source.LineInfo;

import java.util.ArrayList;

/**
 * Instances of the class {@code DartEntryImpl} implement a {@link DartEntry}.
 * 
 * @coverage dart.engine
 */
public class DartEntryImpl extends SourceEntryImpl implements DartEntry {
  /**
   * Instances of the class {@code ResolutionState} represent the information produced by resolving
   * a compilation unit as part of a specific library.
   */
  private static class ResolutionState {
    /**
     * The next resolution state or {@code null} if none.
     */
    private ResolutionState nextState;

    /**
     * The source for the defining compilation unit of the library that contains this unit. If this
     * unit is the defining compilation unit for it's library, then this will be the source for this
     * unit.
     */
    private Source librarySource;

    /**
     * The state of the cached resolved compilation unit.
     */
    private CacheState resolvedUnitState = CacheState.INVALID;

    /**
     * The resolved compilation unit, or {@code null} if the resolved compilation unit is not
     * currently cached.
     */
    private CompilationUnit resolvedUnit;

    /**
     * The state of the cached resolution errors.
     */
    private CacheState resolutionErrorsState = CacheState.INVALID;

    /**
     * The errors produced while resolving the compilation unit, or an empty array if the errors are
     * not currently cached.
     */
    private AnalysisError[] resolutionErrors = AnalysisError.NO_ERRORS;

    /**
     * The state of the cached hints.
     */
    private CacheState hintsState = CacheState.INVALID;

    /**
     * The hints produced while auditing the compilation unit, or an empty array if the hints are
     * not currently cached.
     */
    private AnalysisError[] hints = AnalysisError.NO_ERRORS;

    /**
     * Initialize a newly created resolution state.
     */
    public ResolutionState() {
      super();
    }

    /**
     * Set this state to be exactly like the given state, recursively copying the next state as
     * necessary.
     * 
     * @param other the state to be copied
     */
    public void copyFrom(ResolutionState other) {
      librarySource = other.librarySource;

      resolvedUnitState = other.resolvedUnitState;
      resolvedUnit = other.resolvedUnit;

      resolutionErrorsState = other.resolutionErrorsState;
      resolutionErrors = other.resolutionErrors;

      hintsState = other.hintsState;
      hints = other.hints;

      if (other.nextState != null) {
        nextState = new ResolutionState();
        nextState.copyFrom(other.nextState);
      }
    }

    /**
     * Invalidate all of the resolution information associated with the compilation unit.
     */
    public void invalidateAllResolutionInformation() {
      nextState = null;
      librarySource = null;

      resolvedUnitState = CacheState.INVALID;
      resolvedUnit = null;

      resolutionErrorsState = CacheState.INVALID;
      resolutionErrors = AnalysisError.NO_ERRORS;

      hintsState = CacheState.INVALID;
      hints = AnalysisError.NO_ERRORS;
    }

    /**
     * Record that an error occurred while attempting to scan or parse the entry represented by this
     * entry. This will set the state of all resolution-based information as being in error, but
     * will not change the state of any parse results.
     */
    public void recordResolutionError() {
      nextState = null;
      librarySource = null;

      resolvedUnitState = CacheState.ERROR;
      resolvedUnit = null;

      resolutionErrorsState = CacheState.ERROR;
      resolutionErrors = AnalysisError.NO_ERRORS;

      hintsState = CacheState.ERROR;
      hints = AnalysisError.NO_ERRORS;
    }

    /**
     * Write a textual representation of this state to the given builder. The result will only be
     * used for debugging purposes.
     * 
     * @param builder the builder to which the text should be written
     */
    protected void writeOn(StringBuilder builder) {
      if (librarySource != null) {
        builder.append("; resolvedUnit = ");
        builder.append(resolvedUnitState);
        builder.append("; resolutionErrors = ");
        builder.append(resolutionErrorsState);
        builder.append("; hints = ");
        builder.append(hintsState);
        if (nextState != null) {
          nextState.writeOn(builder);
        }
      }
    }
  }

  /**
   * The state of the cached source kind.
   */
  private CacheState sourceKindState = CacheState.INVALID;

  /**
   * The kind of this source.
   */
  private SourceKind sourceKind = SourceKind.UNKNOWN;

  /**
   * The state of the cached parsed compilation unit.
   */
  private CacheState parsedUnitState = CacheState.INVALID;

  /**
   * The parsed compilation unit, or {@code null} if the parsed compilation unit is not currently
   * cached.
   */
  private CompilationUnit parsedUnit;

  /**
   * The state of the cached parse errors.
   */
  private CacheState parseErrorsState = CacheState.INVALID;

  /**
   * The errors produced while scanning and parsing the compilation unit, or {@code null} if the
   * errors are not currently cached.
   */
  private AnalysisError[] parseErrors = AnalysisError.NO_ERRORS;

  /**
   * The state of the cached list of imported libraries.
   */
  private CacheState importedLibrariesState = CacheState.INVALID;

  /**
   * The list of libraries imported by the library, or an empty array if the list is not currently
   * cached. The list will be empty if the Dart file is a part rather than a library.
   */
  private Source[] importedLibraries = Source.EMPTY_ARRAY;

  /**
   * The state of the cached list of exported libraries.
   */
  private CacheState exportedLibrariesState = CacheState.INVALID;

  /**
   * The list of libraries exported by the library, or an empty array if the list is not currently
   * cached. The list will be empty if the Dart file is a part rather than a library.
   */
  private Source[] exportedLibraries = Source.EMPTY_ARRAY;

  /**
   * The state of the cached list of included parts.
   */
  private CacheState includedPartsState = CacheState.INVALID;

  /**
   * The list of parts included in the library, or an empty array if the list is not currently
   * cached. The list will be empty if the Dart file is a part rather than a library.
   */
  private Source[] includedParts = Source.EMPTY_ARRAY;

  /**
   * The information known as a result of resolving this compilation unit as part of the library
   * that contains this unit. This field will never be {@code null}.
   */
  private ResolutionState resolutionState = new ResolutionState();

  /**
   * The state of the cached library element.
   */
  private CacheState elementState = CacheState.INVALID;

  /**
   * The element representing the library, or {@code null} if the element is not currently cached.
   */
  private LibraryElement element;

  /**
   * The state of the cached public namespace.
   */
  private CacheState publicNamespaceState = CacheState.INVALID;

  /**
   * The public namespace of the library, or {@code null} if the namespace is not currently cached.
   */
  private Namespace publicNamespace;

  /**
   * The state of the cached client/ server flag.
   */
  private CacheState clientServerState = CacheState.INVALID;

  /**
   * The state of the cached launchable flag.
   */
  private CacheState launchableState = CacheState.INVALID;

  /**
   * An integer holding bit masks such as {@link #LAUNCHABLE} and {@link #CLIENT_CODE}.
   */
  private int bitmask = 0;

  /**
   * Mask indicating that this library is launchable: that the file has a main method.
   */
  private static final int LAUNCHABLE = 1 << 1;

  /**
   * Mask indicating that the library is client code: that the library depends on the html library.
   * If the library is not "client code", then it is referenced as "server code".
   */
  private static final int CLIENT_CODE = 1 << 2;

  /**
   * Initialize a newly created cache entry to be empty.
   */
  public DartEntryImpl() {
    super();
  }

  @Override
  public AnalysisError[] getAllErrors() {
    ArrayList<AnalysisError> errors = new ArrayList<AnalysisError>();
    for (AnalysisError error : parseErrors) {
      errors.add(error);
    }
    ResolutionState state = resolutionState;
    while (state != null) {
      for (AnalysisError error : state.resolutionErrors) {
        errors.add(error);
      }
      for (AnalysisError error : state.hints) {
        errors.add(error);
      }
      state = state.nextState;
    };
    if (errors.size() == 0) {
      return AnalysisError.NO_ERRORS;
    }
    return errors.toArray(new AnalysisError[errors.size()]);
  }

  @Override
  public CompilationUnit getAnyParsedCompilationUnit() {
    if (parsedUnitState == CacheState.VALID) {
      return parsedUnit;
    }
    return getAnyResolvedCompilationUnit();
  }

  @Override
  public CompilationUnit getAnyResolvedCompilationUnit() {
    ResolutionState state = resolutionState;
    while (state != null) {
      if (state.resolvedUnitState == CacheState.VALID) {
        return state.resolvedUnit;
      }
      state = state.nextState;
    };
    return null;
  }

  @Override
  public SourceKind getKind() {
    return sourceKind;
  }

  @Override
  public CacheState getState(DataDescriptor<?> descriptor) {
    if (descriptor == ELEMENT) {
      return elementState;
    } else if (descriptor == EXPORTED_LIBRARIES) {
      return exportedLibrariesState;
    } else if (descriptor == IMPORTED_LIBRARIES) {
      return importedLibrariesState;
    } else if (descriptor == INCLUDED_PARTS) {
      return includedPartsState;
    } else if (descriptor == IS_CLIENT) {
      return clientServerState;
    } else if (descriptor == IS_LAUNCHABLE) {
      return launchableState;
    } else if (descriptor == PARSE_ERRORS) {
      return parseErrorsState;
    } else if (descriptor == PARSED_UNIT) {
      return parsedUnitState;
    } else if (descriptor == PUBLIC_NAMESPACE) {
      return publicNamespaceState;
    } else if (descriptor == SOURCE_KIND) {
      return sourceKindState;
    } else {
      return super.getState(descriptor);
    }
  }

  @Override
  public CacheState getState(DataDescriptor<?> descriptor, Source librarySource) {
    ResolutionState state = resolutionState;
    while (state != null) {
      if (librarySource.equals(state.librarySource)) {
        if (descriptor == RESOLUTION_ERRORS) {
          return resolutionState.resolutionErrorsState;
        } else if (descriptor == RESOLVED_UNIT) {
          return resolutionState.resolvedUnitState;
        } else if (descriptor == HINTS) {
          return resolutionState.hintsState;
        } else {
          throw new IllegalArgumentException("Invalid descriptor: " + descriptor);
        }
      }
      state = state.nextState;
    };
    if (descriptor == RESOLUTION_ERRORS || descriptor == RESOLVED_UNIT || descriptor == HINTS) {
      return CacheState.INVALID;
    } else {
      throw new IllegalArgumentException("Invalid descriptor: " + descriptor);
    }
  }

  @Override
  @SuppressWarnings("unchecked")
  public <E> E getValue(DataDescriptor<E> descriptor) {
    if (descriptor == ELEMENT) {
      return (E) element;
    } else if (descriptor == EXPORTED_LIBRARIES) {
      return (E) exportedLibraries;
    } else if (descriptor == IMPORTED_LIBRARIES) {
      return (E) importedLibraries;
    } else if (descriptor == INCLUDED_PARTS) {
      return (E) includedParts;
    } else if (descriptor == IS_CLIENT) {
      return (E) ((bitmask & CLIENT_CODE) != 0 ? Boolean.TRUE : Boolean.FALSE);
    } else if (descriptor == IS_LAUNCHABLE) {
      return (E) ((bitmask & LAUNCHABLE) != 0 ? Boolean.TRUE : Boolean.FALSE);
    } else if (descriptor == PARSE_ERRORS) {
      return (E) parseErrors;
    } else if (descriptor == PARSED_UNIT) {
      return (E) parsedUnit;
    } else if (descriptor == PUBLIC_NAMESPACE) {
      return (E) publicNamespace;
    } else if (descriptor == SOURCE_KIND) {
      return (E) sourceKind;
    }
    return super.getValue(descriptor);
  }

  @Override
  @SuppressWarnings("unchecked")
  public <E> E getValue(DataDescriptor<E> descriptor, Source librarySource) {
    ResolutionState state = resolutionState;
    while (state != null) {
      if (librarySource.equals(state.librarySource)) {
        if (descriptor == RESOLUTION_ERRORS) {
          return (E) state.resolutionErrors;
        } else if (descriptor == RESOLVED_UNIT) {
          return (E) state.resolvedUnit;
        } else if (descriptor == HINTS) {
          return (E) state.hints;
        } else {
          throw new IllegalArgumentException("Invalid descriptor: " + descriptor);
        }
      }
      state = state.nextState;
    };
    if (descriptor == RESOLUTION_ERRORS || descriptor == HINTS) {
      return (E) AnalysisError.NO_ERRORS;
    } else if (descriptor == RESOLVED_UNIT) {
      return null;
    } else {
      throw new IllegalArgumentException("Invalid descriptor: " + descriptor);
    }
  }

  @Override
  public DartEntryImpl getWritableCopy() {
    DartEntryImpl copy = new DartEntryImpl();
    copy.copyFrom(this);
    return copy;
  }

  /**
   * Invalidate all of the information associated with the compilation unit.
   */
  public void invalidateAllInformation() {
    setState(LINE_INFO, CacheState.INVALID);

    sourceKind = SourceKind.UNKNOWN;
    sourceKindState = CacheState.INVALID;

    parseErrors = AnalysisError.NO_ERRORS;
    parseErrorsState = CacheState.INVALID;

    parsedUnit = null;
    parsedUnitState = CacheState.INVALID;

    invalidateAllResolutionInformation();
  }

  /**
   * Invalidate all of the resolution information associated with the compilation unit.
   */
  public void invalidateAllResolutionInformation() {
    element = null;
    elementState = CacheState.INVALID;

    includedParts = Source.EMPTY_ARRAY;
    includedPartsState = CacheState.INVALID;

    exportedLibraries = Source.EMPTY_ARRAY;
    exportedLibrariesState = CacheState.INVALID;

    importedLibraries = Source.EMPTY_ARRAY;
    importedLibrariesState = CacheState.INVALID;

    bitmask = 0;
    clientServerState = CacheState.INVALID;
    launchableState = CacheState.INVALID;

    publicNamespace = null;
    publicNamespaceState = CacheState.INVALID;

    resolutionState.invalidateAllResolutionInformation();
  }

  /**
   * Record that an error occurred while attempting to scan or parse the entry represented by this
   * entry. This will set the state of all information, including any resolution-based information,
   * as being in error.
   */
  public void recordParseError() {
    setState(LINE_INFO, CacheState.ERROR);

    sourceKind = SourceKind.UNKNOWN;
    sourceKindState = CacheState.ERROR;

    parseErrors = AnalysisError.NO_ERRORS;
    parseErrorsState = CacheState.ERROR;

    parsedUnit = null;
    parsedUnitState = CacheState.ERROR;

    exportedLibraries = Source.EMPTY_ARRAY;
    exportedLibrariesState = CacheState.ERROR;

    importedLibraries = Source.EMPTY_ARRAY;
    importedLibrariesState = CacheState.ERROR;

    includedParts = Source.EMPTY_ARRAY;
    includedPartsState = CacheState.ERROR;

    recordResolutionError();
  }

  /**
   * Record that the parse-related information for the associated source is about to be computed by
   * the current thread.
   */
  public void recordParseInProcess() {
    if (getState(LINE_INFO) != CacheState.VALID) {
      setState(LINE_INFO, CacheState.IN_PROCESS);
    }
    if (sourceKindState != CacheState.VALID) {
      sourceKindState = CacheState.IN_PROCESS;
    }
    if (parseErrorsState != CacheState.VALID) {
      parseErrorsState = CacheState.IN_PROCESS;
    }
    if (parsedUnitState != CacheState.VALID) {
      parsedUnitState = CacheState.IN_PROCESS;
    }
    if (exportedLibrariesState != CacheState.VALID) {
      exportedLibrariesState = CacheState.IN_PROCESS;
    }
    if (importedLibrariesState != CacheState.VALID) {
      importedLibrariesState = CacheState.IN_PROCESS;
    }
    if (includedPartsState != CacheState.VALID) {
      includedPartsState = CacheState.IN_PROCESS;
    }
  }

  /**
   * Record that an error occurred while attempting to scan or parse the entry represented by this
   * entry. This will set the state of all resolution-based information as being in error, but will
   * not change the state of any parse results.
   */
  public void recordResolutionError() {
    element = null;
    elementState = CacheState.ERROR;

    bitmask = 0;
    clientServerState = CacheState.ERROR;
    launchableState = CacheState.ERROR;

    publicNamespace = null;
    publicNamespaceState = CacheState.ERROR;

    resolutionState.recordResolutionError();
  }

  /**
   * Remove any resolution information associated with this compilation unit being part of the given
   * library, presumably because it is no longer part of the library.
   * 
   * @param librarySource the source of the defining compilation unit of the library that used to
   *          contain this part but no longer does
   */
  public void removeResolution(Source librarySource) {
    if (librarySource != null) {
      if (librarySource.equals(resolutionState.librarySource)) {
        if (resolutionState.nextState == null) {
          resolutionState.invalidateAllResolutionInformation();
        } else {
          resolutionState = resolutionState.nextState;
        }
      } else {
        ResolutionState priorState = resolutionState;
        ResolutionState state = resolutionState.nextState;
        while (state != null) {
          if (librarySource.equals(state.librarySource)) {
            priorState.nextState = state.nextState;
            break;
          }
          priorState = state;
          state = state.nextState;
        }
      }
    }
  }

  /**
   * Set the results of parsing the compilation unit at the given time to the given values.
   * 
   * @param modificationStamp the earliest time at which the source was last modified before the
   *          parsing was started
   * @param lineInfo the line information resulting from parsing the compilation unit
   * @param unit the AST structure resulting from parsing the compilation unit
   * @param errors the parse errors resulting from parsing the compilation unit
   */
  public void setParseResults(long modificationStamp, LineInfo lineInfo, CompilationUnit unit,
      AnalysisError[] errors) {
    if (getState(LINE_INFO) != CacheState.VALID) {
      setValue(LINE_INFO, lineInfo);
    }
    if (parsedUnitState != CacheState.VALID) {
      parsedUnit = unit;
      parsedUnitState = CacheState.VALID;
    }
    if (parseErrorsState != CacheState.VALID) {
      parseErrors = errors == null ? AnalysisError.NO_ERRORS : errors;
      parseErrorsState = CacheState.VALID;
    }
  }

  @Override
  public void setState(DataDescriptor<?> descriptor, CacheState state) {
    if (descriptor == ELEMENT) {
      element = updatedValue(state, element, null);
      elementState = state;
    } else if (descriptor == EXPORTED_LIBRARIES) {
      exportedLibraries = updatedValue(state, exportedLibraries, Source.EMPTY_ARRAY);
      exportedLibrariesState = state;
    } else if (descriptor == IMPORTED_LIBRARIES) {
      importedLibraries = updatedValue(state, importedLibraries, Source.EMPTY_ARRAY);
      importedLibrariesState = state;
    } else if (descriptor == INCLUDED_PARTS) {
      includedParts = updatedValue(state, includedParts, Source.EMPTY_ARRAY);
      includedPartsState = state;
    } else if (descriptor == IS_CLIENT) {
      bitmask = updatedValue(state, bitmask, CLIENT_CODE);
      clientServerState = state;
    } else if (descriptor == IS_LAUNCHABLE) {
      bitmask = updatedValue(state, bitmask, LAUNCHABLE);
      launchableState = state;
    } else if (descriptor == PARSE_ERRORS) {
      parseErrors = updatedValue(state, parseErrors, AnalysisError.NO_ERRORS);
      parseErrorsState = state;
    } else if (descriptor == PARSED_UNIT) {
      parsedUnit = updatedValue(state, parsedUnit, null);
      parsedUnitState = state;
    } else if (descriptor == PUBLIC_NAMESPACE) {
      publicNamespace = updatedValue(state, publicNamespace, null);
      publicNamespaceState = state;
    } else if (descriptor == SOURCE_KIND) {
      sourceKind = updatedValue(state, sourceKind, SourceKind.UNKNOWN);
      sourceKindState = state;
    } else {
      super.setState(descriptor, state);
    }
  }

  /**
   * Set the state of the data represented by the given descriptor in the context of the given
   * library to the given state.
   * 
   * @param descriptor the descriptor representing the data whose state is to be set
   * @param librarySource the source of the defining compilation unit of the library that is the
   *          context for the data
   * @param cacheState the new state of the data represented by the given descriptor
   */
  public void setState(DataDescriptor<?> descriptor, Source librarySource, CacheState cacheState) {
    ResolutionState state = getOrCreateResolutionState(librarySource);
    if (descriptor == RESOLUTION_ERRORS) {
      state.resolutionErrors = updatedValue(
          cacheState,
          state.resolutionErrors,
          AnalysisError.NO_ERRORS);
      state.resolutionErrorsState = cacheState;
    } else if (descriptor == RESOLVED_UNIT) {
      state.resolvedUnit = updatedValue(cacheState, state.resolvedUnit, null);
      state.resolvedUnitState = cacheState;
    } else if (descriptor == HINTS) {
      state.hints = updatedValue(cacheState, state.hints, AnalysisError.NO_ERRORS);
      state.hintsState = cacheState;
    } else {
      throw new IllegalArgumentException("Invalid descriptor: " + descriptor);
    }
  }

  @Override
  public <E> void setValue(DataDescriptor<E> descriptor, E value) {
    if (descriptor == ELEMENT) {
      element = (LibraryElement) value;
      elementState = CacheState.VALID;
    } else if (descriptor == EXPORTED_LIBRARIES) {
      exportedLibraries = value == null ? Source.EMPTY_ARRAY : (Source[]) value;
      exportedLibrariesState = CacheState.VALID;
    } else if (descriptor == IMPORTED_LIBRARIES) {
      importedLibraries = value == null ? Source.EMPTY_ARRAY : (Source[]) value;
      importedLibrariesState = CacheState.VALID;
    } else if (descriptor == INCLUDED_PARTS) {
      includedParts = value == null ? Source.EMPTY_ARRAY : (Source[]) value;
      includedPartsState = CacheState.VALID;
    } else if (descriptor == IS_CLIENT) {
      if (((Boolean) value).booleanValue()) {
        bitmask |= CLIENT_CODE;
      } else {
        bitmask &= ~CLIENT_CODE;
      }
      clientServerState = CacheState.VALID;
    } else if (descriptor == IS_LAUNCHABLE) {
      if (((Boolean) value).booleanValue()) {
        bitmask |= LAUNCHABLE;
      } else {
        bitmask &= ~LAUNCHABLE;
      }
      launchableState = CacheState.VALID;
    } else if (descriptor == PARSE_ERRORS) {
      parseErrors = value == null ? AnalysisError.NO_ERRORS : (AnalysisError[]) value;
      parseErrorsState = CacheState.VALID;
    } else if (descriptor == PARSED_UNIT) {
      parsedUnit = (CompilationUnit) value;
      parsedUnitState = CacheState.VALID;
    } else if (descriptor == PUBLIC_NAMESPACE) {
      publicNamespace = (Namespace) value;
      publicNamespaceState = CacheState.VALID;
    } else if (descriptor == SOURCE_KIND) {
      sourceKind = (SourceKind) value;
      sourceKindState = CacheState.VALID;
    } else {
      super.setValue(descriptor, value);
    }
  }

  /**
   * Set the value of the data represented by the given descriptor in the context of the given
   * library to the given value, and set the state of that data to {@link CacheState#VALID}.
   * 
   * @param descriptor the descriptor representing which data is to have its value set
   * @param librarySource the source of the defining compilation unit of the library that is the
   *          context for the data
   * @param value the new value of the data represented by the given descriptor and library
   */
  public <E> void setValue(DataDescriptor<E> descriptor, Source librarySource, E value) {
    ResolutionState state = getOrCreateResolutionState(librarySource);
    if (descriptor == RESOLUTION_ERRORS) {
      state.resolutionErrors = value == null ? AnalysisError.NO_ERRORS : (AnalysisError[]) value;
      state.resolutionErrorsState = CacheState.VALID;
    } else if (descriptor == RESOLVED_UNIT) {
      state.resolvedUnit = (CompilationUnit) value;
      state.resolvedUnitState = CacheState.VALID;
    } else if (descriptor == HINTS) {
      state.hints = value == null ? AnalysisError.NO_ERRORS : (AnalysisError[]) value;
      state.hintsState = CacheState.VALID;
    }
  }

  @Override
  protected void copyFrom(SourceEntryImpl entry) {
    super.copyFrom(entry);
    DartEntryImpl other = (DartEntryImpl) entry;
    sourceKindState = other.sourceKindState;
    sourceKind = other.sourceKind;
    parsedUnitState = other.parsedUnitState;
    parsedUnit = other.parsedUnit;
    parseErrorsState = other.parseErrorsState;
    parseErrors = other.parseErrors;
    includedPartsState = other.includedPartsState;
    includedParts = other.includedParts;
    exportedLibrariesState = other.exportedLibrariesState;
    exportedLibraries = other.exportedLibraries;
    importedLibrariesState = other.importedLibrariesState;
    importedLibraries = other.importedLibraries;
    resolutionState.copyFrom(other.resolutionState);
    elementState = other.elementState;
    element = other.element;
    publicNamespaceState = other.publicNamespaceState;
    publicNamespace = other.publicNamespace;
    clientServerState = other.clientServerState;
    launchableState = other.launchableState;
    bitmask = other.bitmask;
  }

  @Override
  protected void writeOn(StringBuilder builder) {
    builder.append("Dart: ");
    super.writeOn(builder);
    builder.append("; sourceKind = ");
    builder.append(sourceKindState);
    builder.append("; parsedUnit = ");
    builder.append(parsedUnitState);
    builder.append("; parseErrors = ");
    builder.append(parseErrorsState);
    builder.append("; exportedLibraries = ");
    builder.append(exportedLibrariesState);
    builder.append("; importedLibraries = ");
    builder.append(importedLibrariesState);
    builder.append("; includedParts = ");
    builder.append(includedPartsState);
    builder.append("; element = ");
    builder.append(elementState);
    builder.append("; publicNamespace = ");
    builder.append(publicNamespaceState);
    builder.append("; clientServer = ");
    builder.append(clientServerState);
    builder.append("; launchable = ");
    builder.append(launchableState);
    resolutionState.writeOn(builder);
  }

  /**
   * Return a resolution state for the specified library, creating one as necessary.
   * 
   * @param librarySource the library source (not {@code null})
   * @return the resolution state (not {@code null})
   */
  private ResolutionState getOrCreateResolutionState(Source librarySource) {
    ResolutionState state = resolutionState;
    if (state.librarySource == null) {
      state.librarySource = librarySource;
      return state;
    }
    while (!state.librarySource.equals(librarySource)) {
      if (state.nextState == null) {
        ResolutionState newState = new ResolutionState();
        newState.librarySource = librarySource;
        state.nextState = newState;
        return newState;
      }
      state = state.nextState;
    }
    return state;
  }

  /**
   * Given that one of the flags is being transitioned to the given state, return the value of the
   * flags that should be kept in the cache.
   * 
   * @param state the state to which the data is being transitioned
   * @param currentValue the value of the flags before the transition
   * @param bitMask the mask used to access the bit whose state is being set
   * @return the value of the data that should be kept in the cache
   */
  private int updatedValue(CacheState state, int currentValue, int bitMask) {
    if (state == CacheState.VALID) {
      throw new IllegalArgumentException("Use setValue() to set the state to VALID");
    } else if (state == CacheState.IN_PROCESS) {
      //
      // We can leave the current value in the cache for any 'get' methods to access.
      //
      return currentValue;
    }
    return currentValue &= ~bitMask;
  }
}
