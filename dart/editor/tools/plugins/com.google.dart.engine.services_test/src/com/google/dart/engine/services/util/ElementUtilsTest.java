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

package com.google.dart.engine.services.util;

import com.google.dart.engine.element.Element;
import com.google.dart.engine.element.LibraryElement;
import com.google.dart.engine.services.internal.refactoring.RefactoringImplTest;

import static org.fest.assertions.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import java.util.Set;

public class ElementUtilsTest extends RefactoringImplTest {
  private static Element mockElement(String name) {
    Element element = mock(Element.class);
    when(element.getDisplayName()).thenReturn(name);
    return element;
  }

  private static LibraryElement mockLibraryElement(String name) {
    LibraryElement library = mock(LibraryElement.class);
    when(library.toString()).thenReturn(name);
    when(library.getDisplayName()).thenReturn(name);
    when(library.getImportedLibraries()).thenReturn(new LibraryElement[] {});
    when(library.getExportedLibraries()).thenReturn(new LibraryElement[] {});
    return library;
  }

  public void test_getVisibleElementsLibraries_cycle() throws Exception {
    LibraryElement library = mockLibraryElement("app");
    LibraryElement libraryA = mockLibraryElement("A");
    when(library.getImportedLibraries()).thenReturn(new LibraryElement[] {libraryA});
    when(libraryA.getImportedLibraries()).thenReturn(new LibraryElement[] {library});
    Set<LibraryElement> visibleLibraries = ElementUtils.getVisibleElementsLibraries(library);
    assertThat(visibleLibraries).containsOnly(library, libraryA);
  }

  public void test_getVisibleElementsLibraries_directExport() throws Exception {
    LibraryElement library = mockLibraryElement("app");
    LibraryElement libraryA = mockLibraryElement("A");
    when(library.getExportedLibraries()).thenReturn(new LibraryElement[] {libraryA});
    Set<LibraryElement> visibleLibraries = ElementUtils.getVisibleElementsLibraries(library);
    assertThat(visibleLibraries).containsOnly(library);
  }

  public void test_getVisibleElementsLibraries_directImport() throws Exception {
    LibraryElement library = mockLibraryElement("app");
    LibraryElement libraryA = mockLibraryElement("A");
    when(library.getImportedLibraries()).thenReturn(new LibraryElement[] {libraryA});
    Set<LibraryElement> visibleLibraries = ElementUtils.getVisibleElementsLibraries(library);
    assertThat(visibleLibraries).containsOnly(library, libraryA);
  }

  public void test_getVisibleElementsLibraries_indirectExport() throws Exception {
    LibraryElement library = mockLibraryElement("app");
    LibraryElement libraryA = mockLibraryElement("A");
    LibraryElement libraryAA = mockLibraryElement("AA");
    when(library.getImportedLibraries()).thenReturn(new LibraryElement[] {libraryA});
    when(libraryA.getExportedLibraries()).thenReturn(new LibraryElement[] {libraryAA});
    Set<LibraryElement> visibleLibraries = ElementUtils.getVisibleElementsLibraries(library);
    assertThat(visibleLibraries).containsOnly(library, libraryA, libraryAA);
  }

  public void test_getVisibleElementsLibraries_indirectImport() throws Exception {
    LibraryElement library = mockLibraryElement("app");
    LibraryElement libraryA = mockLibraryElement("A");
    LibraryElement libraryAA = mockLibraryElement("AA");
    when(library.getImportedLibraries()).thenReturn(new LibraryElement[] {libraryA});
    when(libraryA.getImportedLibraries()).thenReturn(new LibraryElement[] {libraryAA});
    Set<LibraryElement> visibleLibraries = ElementUtils.getVisibleElementsLibraries(library);
    assertThat(visibleLibraries).containsOnly(library, libraryA, libraryAA);
  }

  public void test_getVisibleElementsLibraries_noImports() throws Exception {
    LibraryElement library = mockLibraryElement("app");
    Set<LibraryElement> visibleLibraries = ElementUtils.getVisibleElementsLibraries(library);
    assertThat(visibleLibraries).containsOnly(library);
  }

  public void test_isAccessible_public() throws Exception {
    LibraryElement whatLibrary = mock(LibraryElement.class);
    LibraryElement whereLibrary = mock(LibraryElement.class);
    Element what = mockElement("test");
    Element where = mockElement("no-matter");
    when(what.getLibrary()).thenReturn(whatLibrary);
    when(where.getLibrary()).thenReturn(whereLibrary);
    // check
    ElementUtils.isAccessible(what, where);
    verify(what).isAccessibleIn(whereLibrary);
  }

  public void test_isPrivate_null() throws Exception {
    Element element = mockElement(null);
    assertFalse(ElementUtils.isPrivate(element));
  }

  public void test_isPrivate_private() throws Exception {
    Element element = mockElement("_test");
    assertTrue(ElementUtils.isPrivate(element));
  }

  public void test_isPrivate_public() throws Exception {
    Element element = mockElement("test");
    assertFalse(ElementUtils.isPrivate(element));
  }

  public void test_isPublic_null() throws Exception {
    Element element = mockElement(null);
    assertTrue(ElementUtils.isPublic(element));
  }

  public void test_isPublic_private() throws Exception {
    Element element = mockElement("_test");
    assertFalse(ElementUtils.isPublic(element));
  }

  public void test_isPublic_public() throws Exception {
    Element element = mockElement("test");
    assertTrue(ElementUtils.isPublic(element));
  }
}
