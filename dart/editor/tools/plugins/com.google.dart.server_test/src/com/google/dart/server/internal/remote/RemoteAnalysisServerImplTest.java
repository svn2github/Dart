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

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.dart.server.AnalysisOptions;
import com.google.dart.server.AnalysisStatus;
import com.google.dart.server.CompletionRelevance;
import com.google.dart.server.CompletionSuggestion;
import com.google.dart.server.CompletionSuggestionKind;
import com.google.dart.server.Element;
import com.google.dart.server.ElementKind;
import com.google.dart.server.ErrorFixes;
import com.google.dart.server.GetAssistsConsumer;
import com.google.dart.server.GetErrorsConsumer;
import com.google.dart.server.GetFixesConsumer;
import com.google.dart.server.GetHoverConsumer;
import com.google.dart.server.GetSuggestionsConsumer;
import com.google.dart.server.GetTypeHierarchyConsumer;
import com.google.dart.server.GetVersionConsumer;
import com.google.dart.server.HighlightRegion;
import com.google.dart.server.HighlightType;
import com.google.dart.server.HoverInformation;
import com.google.dart.server.NavigationRegion;
import com.google.dart.server.Occurrences;
import com.google.dart.server.Outline;
import com.google.dart.server.OverrideMember;
import com.google.dart.server.SearchIdConsumer;
import com.google.dart.server.SearchResult;
import com.google.dart.server.SearchResultKind;
import com.google.dart.server.ServerStatus;
import com.google.dart.server.SourceChange;
import com.google.dart.server.SourceEdit;
import com.google.dart.server.SourceFileEdit;
import com.google.dart.server.TypeHierarchyItem;
import com.google.dart.server.generated.types.AnalysisError;
import com.google.dart.server.generated.types.ErrorSeverity;
import com.google.dart.server.generated.types.Location;
import com.google.dart.server.generated.types.ServerService;
import com.google.dart.server.internal.AnalysisServerError;
import com.google.dart.server.internal.integration.RemoteAnalysisServerImplIntegrationTest;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import static org.fest.assertions.Assertions.assertThat;

import java.util.ArrayList;
import java.util.List;

/**
 * Unit tests for {@link RemoteAnalysisServerImpl}, for integration tests which actually uses the
 * remote server, see {@link RemoteAnalysisServerImplIntegrationTest}.
 */
public class RemoteAnalysisServerImplTest extends AbstractRemoteServerTest {

  public void test_analysis_getErrors() throws Exception {
    final AnalysisError[][] errors = new AnalysisError[1][1];
    server.analysis_getErrors("/fileA.dart", new GetErrorsConsumer() {
      @Override
      public void computedErrors(AnalysisError[] e) {
        errors[0] = e;
      }
    });

    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.getErrors',",
        "  'params': {",
        "    'file': '/fileA.dart'",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'errors' : [",
        "      {",
        "        'severity': 'ERROR',",
        "        'type': 'SYNTACTIC_ERROR',",
        "        'location': {",
        "          'file': '/fileA.dart',",
        "          'offset': 1,",
        "          'length': 2,",
        "          'startLine': 3,",
        "          'startColumn': 4",
        "        },",
        "        'message': 'message A',",
        "        'correction': 'correction A'",
        "      },",
        "      {",
        "        'severity': 'ERROR',",
        "        'type': 'COMPILE_TIME_ERROR',",
        "        'location': {",
        "          'file': '/fileB.dart',",
        "          'offset': 5,",
        "          'length': 6,",
        "          'startLine': 7,",
        "          'startColumn': 8",
        "        },",
        "        'message': 'message B',",
        "        'correction': 'correction B'",
        "      }",
        "    ]",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();

    assertThat(errors[0]).hasSize(2);
    assertEquals(new AnalysisError(ErrorSeverity.ERROR, "SYNTACTIC_ERROR", new Location(
        "/fileA.dart",
        1,
        2,
        3,
        4), "message A", "correction A"), errors[0][0]);
    assertEquals(new AnalysisError(ErrorSeverity.ERROR, "COMPILE_TIME_ERROR", new Location(
        "/fileB.dart",
        5,
        6,
        7,
        8), "message B", "correction B"), errors[0][1]);
  }

  public void test_analysis_getHover() throws Exception {
    final HoverInformation[] hovers = new HoverInformation[1];
    server.analysis_getHover("/fileA.dart", 17, new GetHoverConsumer() {
      @Override
      public void computedHovers(HoverInformation[] result) {
        hovers[0] = result[0];
      }
    });

    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.getHover',",
        "  'params': {",
        "    'file': '/fileA.dart',",
        "    'offset': 17",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'hovers': [",
        "      {",
        "        'offset': '22',",
        "        'length': '5',",
        "        'containingLibraryName': 'myLibrary',",
        "        'containingLibraryPath': '/path/to/lib',",
        "        'dartdoc': 'some dartdoc',",
        "        'elementDescription': 'element description',",
        "        'elementKind': 'element kind',",
        "        'parameter': 'some parameter',",
        "        'propagatedType': 'typeA',",
        "        'staticType': 'typeB'",
        "      }",
        "    ]",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertNotNull(hovers[0]);
    assertEquals(22, hovers[0].getOffset());
    assertEquals(5, hovers[0].getLength());
    assertEquals("myLibrary", hovers[0].getContainingLibraryName());
    assertEquals("/path/to/lib", hovers[0].getContainingLibraryPath());
    assertEquals("some dartdoc", hovers[0].getDartdoc());
    assertEquals("element description", hovers[0].getElementDescription());
    assertEquals("element kind", hovers[0].getElementKind());
    assertEquals("some parameter", hovers[0].getParameter());
    assertEquals("typeA", hovers[0].getPropagatedType());
    assertEquals("typeB", hovers[0].getStaticType());
  }

  public void test_analysis_notification_errors() throws Exception {
    putResponse(//
        "{",
        "  'event': 'analysis.errors',",
        "  'params': {",
        "    'file': '/test.dart',",
        "    'errors' : [",
        "      {",
        "        'severity': 'ERROR',",
        "        'type': 'SYNTACTIC_ERROR',",
        "        'location': {",
        "          'file': '/fileA.dart',",
        "          'offset': 1,",
        "          'length': 2,",
        "          'startLine': 3,",
        "          'startColumn': 4",
        "        },",
        "        'message': 'message A',",
        "        'correction': 'correction A'",
        "      },",
        "      {",
        "        'severity': 'ERROR',",
        "        'type': 'COMPILE_TIME_ERROR',",
        "        'location': {",
        "          'file': '/fileB.dart',",
        "          'offset': 5,",
        "          'length': 6,",
        "          'startLine': 7,",
        "          'startColumn': 8",
        "        },",
        "        'message': 'message B',",
        "        'correction': 'correction B'",
        "      }",
        "    ]",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    listener.assertErrorsWithAnalysisErrors("/test.dart", new AnalysisError(
        ErrorSeverity.ERROR,
        "SYNTACTIC_ERROR",
        new Location("/fileA.dart", 1, 2, 3, 4),
        "message A",
        "correction A"), new AnalysisError(ErrorSeverity.ERROR, "COMPILE_TIME_ERROR", new Location(
        "/fileB.dart",
        5,
        6,
        7,
        8), "message B", "correction B"));
  }

  public void test_analysis_notification_highlights() throws Exception {
    putResponse(//
        "{",
        "  'event': 'analysis.highlights',",
        "  'params': {",
        "    'file': '/test.dart',",
        "    'regions' : [",
        "      {",
        "        'type': 'CLASS',",
        "        'offset': 1,",
        "        'length': 2",
        "      },",
        "      {",
        "        'type': 'FIELD',",
        "        'offset': 10,",
        "        'length': 20",
        "      }",
        "    ]",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    HighlightRegion[] regions = listener.getHighlightRegions("/test.dart");
    assertThat(regions).hasSize(2);
    {
      HighlightRegion error = regions[0];
      assertSame(HighlightType.CLASS, error.getType());
      assertEquals(1, error.getOffset());
      assertEquals(2, error.getLength());
    }
    {
      HighlightRegion error = regions[1];
      assertSame(HighlightType.FIELD, error.getType());
      assertEquals(10, error.getOffset());
      assertEquals(20, error.getLength());
    }
  }

  public void test_analysis_notification_navigation() throws Exception {
    putResponse(//
        "{",
        "  'event': 'analysis.navigation',",
        "  'params': {",
        "    'file': '/test.dart',",
        "    'regions' : [",
        "      {",
        "        'offset': 1,",
        "        'length': 2,",
        "        'targets': [",
        "          {",
        "            'kind': 'COMPILATION_UNIT',",
        "            'name': 'name0',",
        "            'location': {",
        "              'file': '/test2.dart',",
        "              'offset': 3,",
        "              'length': 4,",
        "              'startLine': 5,",
        "              'startColumn': 6",
        "            },",
        "            'flags': 0,",
        "            'parameters': 'parameters0',",
        "            'returnType': 'returnType0'",
        "          },",
        "          {",
        "            'kind': 'CLASS',",
        "            'name': '_name1',",
        "            'location': {",
        "              'file': '/test3.dart',",
        "              'offset': 7,",
        "              'length': 8,",
        "              'startLine': 9,",
        "              'startColumn': 10",
        "            },",
        "            'flags': 63",
        "          }",
        "        ]",
        "      }",
        "    ]",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    NavigationRegion[] regions = listener.getNavigationRegions("/test.dart");
    assertThat(regions).hasSize(1);
    {
      NavigationRegion region = regions[0];
      assertEquals(1, region.getOffset());
      assertEquals(2, region.getLength());
      Element[] elements = region.getTargets();
      assertThat(elements).hasSize(2);
      {
        Element element = elements[0];
        assertEquals(ElementKind.COMPILATION_UNIT, element.getKind());
        assertEquals("name0", element.getName());
        Location location = element.getLocation();
        assertEquals("/test2.dart", location.getFile());
        assertEquals(3, location.getOffset());
        assertEquals(4, location.getLength());
        assertEquals(5, location.getStartLine());
        assertEquals(6, location.getStartColumn());
        assertFalse(element.isAbstract());
        assertFalse(element.isConst());
        assertFalse(element.isDeprecated());
        assertFalse(element.isFinal());
        assertFalse(element.isPrivate());
        assertFalse(element.isTopLevelOrStatic());
        assertEquals("parameters0", element.getParameters());
        assertEquals("returnType0", element.getReturnType());
      }
      {
        Element element = elements[1];
        assertEquals(ElementKind.CLASS, element.getKind());
        assertEquals("_name1", element.getName());
        Location location = element.getLocation();
        assertEquals("/test3.dart", location.getFile());
        assertEquals(7, location.getOffset());
        assertEquals(8, location.getLength());
        assertEquals(9, location.getStartLine());
        assertEquals(10, location.getStartColumn());
        assertTrue(element.isAbstract());
        assertTrue(element.isConst());
        assertTrue(element.isDeprecated());
        assertTrue(element.isFinal());
        assertTrue(element.isPrivate());
        assertTrue(element.isTopLevelOrStatic());
        assertNull(element.getParameters());
        assertNull(element.getReturnType());
      }
    }
  }

  public void test_analysis_notification_occurences() throws Exception {
    putResponse(//
        "{",
        "  'event': 'analysis.occurrences',",
        "  'params': {",
        "    'file': '/test.dart',",
        "    'occurrences' : [",
        "      {",
        "        'element': {",
        "          'kind': 'CLASS',",
        "          'name': 'name0',",
        "          'location': {",
        "            'file': '/test2.dart',",
        "            'offset': 7,",
        "            'length': 8,",
        "            'startLine': 9,",
        "            'startColumn': 10",
        "          },",
        "          'flags': 63",
        "        },",
        "        'offsets': [1,2,3,4,5],",
        "        'length': 6",
        "      }",
        "    ]",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    Occurrences[] occurrencesArray = listener.getOccurrences("/test.dart");

    // assertions on occurrences
    assertThat(occurrencesArray).hasSize(1);
    Occurrences occurrences = occurrencesArray[0];
    {
      Element element = occurrences.getElement();
      assertEquals(ElementKind.CLASS, element.getKind());
      assertEquals("name0", element.getName());
      Location location = element.getLocation();
      assertEquals("/test2.dart", location.getFile());
      assertEquals(7, location.getOffset());
      assertEquals(8, location.getLength());
      assertEquals(9, location.getStartLine());
      assertEquals(10, location.getStartColumn());
      assertTrue(element.isAbstract());
      assertTrue(element.isConst());
      assertTrue(element.isDeprecated());
      assertTrue(element.isFinal());
      assertTrue(element.isPrivate());
      assertTrue(element.isTopLevelOrStatic());
      assertNull(element.getParameters());
      assertNull(element.getReturnType());
    }
    assertThat(occurrences.getOffsets()).hasSize(5).contains(1, 2, 3, 4, 5);
    assertEquals(6, occurrences.getLength());
  }

  public void test_analysis_notification_outline() throws Exception {
    putResponse(//
        "{",
        "  'event': 'analysis.outline',",
        "  'params': {",
        "    'file': '/test.dart',",
        "    'outline' : {",
        "      'element': {",
        "        'kind': 'COMPILATION_UNIT',",
        "        'name': 'name0',",
        "        'location': {",
        "          'file': '/test2.dart',",
        "          'offset': 3,",
        "          'length': 4,",
        "          'startLine': 5,",
        "          'startColumn': 6",
        "        },",
        "        'flags': 63,",
        "        'parameters': 'parameters0',",
        "        'returnType': 'returnType0'",
        "      },",
        "      'offset': 1,",
        "      'length': 2,",
        "      'children': [",
        "      {",
        "        'element': {",
        "          'kind': 'CLASS',",
        "          'name': '_name1',",
        "          'location': {",
        "            'file': '/test3.dart',",
        "            'offset': 9,",
        "            'length': 10,",
        "            'startLine': 11,",
        "            'startColumn': 12",
        "          },",
        "          'flags': 0",
        "        },",
        "        'offset': 7,",
        "        'length': 8",
        "      }",
        "    ]",
        "    }",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    Outline outline = listener.getOutline("/test.dart");

    // assertions on outline
    assertThat(outline.getChildren()).hasSize(1);
    assertEquals(1, outline.getOffset());
    assertEquals(2, outline.getLength());
    Element element = outline.getElement();
    assertEquals(ElementKind.COMPILATION_UNIT, element.getKind());
    assertEquals("name0", element.getName());
    Location location = element.getLocation();
    assertEquals("/test2.dart", location.getFile());
    assertEquals(3, location.getOffset());
    assertEquals(4, location.getLength());
    assertEquals(5, location.getStartLine());
    assertEquals(6, location.getStartColumn());
    assertTrue(element.isAbstract());
    assertTrue(element.isConst());
    assertTrue(element.isDeprecated());
    assertTrue(element.isFinal());
    assertTrue(element.isPrivate());
    assertTrue(element.isTopLevelOrStatic());
    assertEquals("parameters0", element.getParameters());
    assertEquals("returnType0", element.getReturnType());

    // assertions on child
    Outline child = outline.getChildren()[0];
    assertEquals(7, child.getOffset());
    assertEquals(8, child.getLength());
    assertThat(child.getChildren()).hasSize(0);
    Element childElement = child.getElement();
    assertEquals(ElementKind.CLASS, childElement.getKind());
    assertEquals("_name1", childElement.getName());
    location = childElement.getLocation();
    assertEquals("/test3.dart", location.getFile());
    assertEquals(9, location.getOffset());
    assertEquals(10, location.getLength());
    assertEquals(11, location.getStartLine());
    assertEquals(12, location.getStartColumn());

    assertFalse(childElement.isAbstract());
    assertFalse(childElement.isConst());
    assertFalse(childElement.isDeprecated());
    assertFalse(childElement.isFinal());
    assertFalse(childElement.isPrivate());
    assertFalse(childElement.isTopLevelOrStatic());
    assertNull(childElement.getParameters());
    assertNull(childElement.getReturnType());
  }

  public void test_analysis_notification_overrides() throws Exception {
    putResponse(//
        "{",
        "  'event': 'analysis.overrides',",
        "  'params': {",
        "    'file': '/test.dart',",
        "    'overrides' : [",
        "      {",
        "        'offset': 1,",
        "        'length': 2,",
        "        'superclassElement': {",
        "          'kind': 'CLASS',",
        "          'name': 'name1',",
        "          'location': {",
        "            'file': '/test2.dart',",
        "            'offset': 3,",
        "            'length': 4,",
        "            'startLine': 5,",
        "            'startColumn': 6",
        "          },",
        "          'flags': 0",
        "        }",
        "      },",
        "      {",
        "        'offset': 7,",
        "        'length': 8",
        "      }",
        "    ]",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    OverrideMember[] overrides = listener.getOverrides("/test.dart");

    // assertions on overrides
    assertThat(overrides).hasSize(2);
    {
      assertEquals(1, overrides[0].getOffset());
      assertEquals(2, overrides[0].getLength());
      Element superclassElement = overrides[0].getSuperclassElement();
      assertNotNull(superclassElement);
      assertEquals("name1", superclassElement.getName());
    }
    {
      assertEquals(7, overrides[1].getOffset());
      assertEquals(8, overrides[1].getLength());
      assertNull(overrides[1].getSuperclassElement());
    }
  }

  public void test_analysis_reanalyze() throws Exception {
    server.analysis_reanalyze();
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.reanalyze'",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_setAnalysisRoots() throws Exception {
    server.analysis_setAnalysisRoots(
        ImmutableList.of("/fileA.dart", "/fileB.dart"),
        ImmutableList.of("/fileC.dart", "/fileD.dart"));
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.setAnalysisRoots',",
        "  'params': {",
        "    'included': ['/fileA.dart', '/fileB.dart'],",
        "    'excluded': ['/fileC.dart', '/fileD.dart']",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_setAnalysisRoots_emptyLists() throws Exception {
    server.analysis_setAnalysisRoots(new ArrayList<String>(0), new ArrayList<String>(0));
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.setAnalysisRoots',",
        "  'params': {",
        "    'included': [],",
        "    'excluded': []",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_setAnalysisRoots_nullLists() throws Exception {
    server.analysis_setAnalysisRoots(null, null);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.setAnalysisRoots',",
        "  'params': {",
        "    'included': [],",
        "    'excluded': []",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_setPriorityFiles() throws Exception {
    server.analysis_setPriorityFiles(ImmutableList.of("/fileA.dart", "/fileB.dart"));
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.setPriorityFiles',",
        "  'params': {",
        "    'files': ['/fileA.dart', '/fileB.dart']",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_setPriorityFiles_emptyList() throws Exception {
    server.analysis_setPriorityFiles(new ArrayList<String>(0));
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.setPriorityFiles',",
        "  'params': {",
        "    'files': []",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_setPriorityFiles_nullList() throws Exception {
    server.analysis_setPriorityFiles(null);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.setPriorityFiles',",
        "  'params': {",
        "    'files': []",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  // TODO (jwren) uncomment after re-implemented
//  public void test_analysis_setSubscriptions() throws Exception {
//    LinkedHashMap<AnalysisService, List<String>> subscriptions = new LinkedHashMap<AnalysisService, List<String>>();
//    subscriptions.put(AnalysisService.ERRORS, new ArrayList<String>(0));
//    subscriptions.put(AnalysisService.HIGHLIGHTS, ImmutableList.of("/fileA.dart"));
//    subscriptions.put(AnalysisService.NAVIGATION, ImmutableList.of("/fileB.dart", "/fileC.dart"));
//    subscriptions.put(
//        AnalysisService.OUTLINE,
//        ImmutableList.of("/fileD.dart", "/fileE.dart", "/fileF.dart"));
//
//    server.analysis_setSubscriptions(subscriptions);
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'analysis.setSubscriptions',",
//        "  'params': {",
//        "    'subscriptions': {",
//        "      ERRORS: [],",
//        "      HIGHLIGHTS: ['/fileA.dart'],",
//        "      NAVIGATION: ['/fileB.dart', '/fileC.dart'],",
//        "      OUTLINE: ['/fileD.dart', '/fileE.dart', '/fileF.dart']",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }

  // TODO (jwren) uncomment after re-implemented
//  public void test_analysis_setSubscriptions_emptyMap() throws Exception {
//    server.analysis_setSubscriptions(new HashMap<AnalysisService, List<String>>(0));
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'analysis.setSubscriptions',",
//        "  'params': {",
//        "    'subscriptions': {}",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }

  // TODO (jwren) uncomment after re-implemented
//  public void test_analysis_setSubscriptions_nullMap() throws Exception {
//    server.analysis_setSubscriptions(null);
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'analysis.setSubscriptions',",
//        "  'params': {",
//        "    'subscriptions': {}",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }

  public void test_analysis_updateAnalysisOptions_all_false() throws Exception {
    AnalysisOptions options = new AnalysisOptions();
    options.setAnalyzeAngular(false);
    options.setAnalyzePolymer(false);
    options.setEnableAsync(false);
    options.setEnableDeferredLoading(false);
    options.setEnableEnums(false);
    options.setGenerateDart2jsHints(false);
    options.setGenerateHints(false);
    server.analysis_updateOptions(options);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.updateOptions',",
        "  'params': {",
        "    'options': {",
        "      'analyzeAngular': false,",
        "      'analyzePolymer': false,",
        "      'enableAsync': false,",
        "      'enableDeferredLoading': false,",
        "      'enableEnums': false,",
        "      'generateDart2jsHints': false,",
        "      'generateHints': false",
        "    }",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_updateAnalysisOptions_all_true() throws Exception {
    AnalysisOptions options = new AnalysisOptions();
    options.setAnalyzeAngular(true);
    options.setAnalyzePolymer(true);
    options.setEnableAsync(true);
    options.setEnableDeferredLoading(true);
    options.setEnableEnums(true);
    options.setGenerateDart2jsHints(true);
    options.setGenerateHints(true);
    server.analysis_updateOptions(options);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.updateOptions',",
        "  'params': {",
        "    'options': {",
        "      'analyzeAngular': true,",
        "      'analyzePolymer': true,",
        "      'enableAsync': true,",
        "      'enableDeferredLoading': true,",
        "      'enableEnums': true,",
        "      'generateDart2jsHints': true,",
        "      'generateHints': true",
        "    }",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_updateAnalysisOptions_subset1() throws Exception {
    AnalysisOptions options = new AnalysisOptions();
    options.setAnalyzeAngular(true);
    server.analysis_updateOptions(options);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.updateOptions',",
        "  'params': {",
        "    'options': {",
        "      'analyzeAngular': true",
        "    }",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_analysis_updateAnalysisOptions_subset2() throws Exception {
    AnalysisOptions options = new AnalysisOptions();
    options.setAnalyzePolymer(true);
    options.setEnableAsync(false);
    options.setEnableDeferredLoading(true);
    server.analysis_updateOptions(options);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'analysis.updateOptions',",
        "  'params': {",
        "    'options': {",
        "      'analyzePolymer': true,",
        "      'enableAsync': false,",
        "      'enableDeferredLoading': true",
        "    }",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  // TODO (jwren) API change
//  public void test_analysis_updateContent() throws Exception {
//    Map<String, ContentChange> files = ImmutableMap.of(
//        "/fileA.dart",
//        new ContentChange("aaa"),
//        "/fileB.dart",
//        new ContentChange("bbb", 1, 2, 3));
//    server.analysis_updateContent(files);
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'analysis.updateContent',",
//        "  'params': {",
//        "    'files': {",
//        "      '/fileA.dart': {",
//        "        'content': 'aaa'",
//        "      },",
//        "      '/fileB.dart': {",
//        "        'content': 'bbb',",
//        "        'offset': 1,",
//        "        'oldLength': 2,",
//        "        'newLength': 3",
//        "      }",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_analysis_updateContent_emptyList() throws Exception {
//    Map<String, ContentChange> files = Maps.newHashMap();
//    server.updateContent(files);
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'analysis.updateContent',",
//        "  'params': {",
//        "    'files': {",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_analysis_updateContent_nullList() throws Exception {
//    server.updateContent(null);
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'analysis.updateContent',",
//        "  'params': {",
//        "    'files': {",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }

  public void test_completion_getSuggestions() throws Exception {
    final String[] completionIdPtr = {null};
    server.completion_getSuggestions("/fileA.dart", 0, new GetSuggestionsConsumer() {
      @Override
      public void computedCompletionId(String completionId) {
        completionIdPtr[0] = completionId;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'completion.getSuggestions',",
        "  'params': {",
        "    'file': '/fileA.dart',",
        "    'offset': 0",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'id': 'completionId0'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertEquals("completionId0", completionIdPtr[0]);
  }

  public void test_completion_notification_results() throws Exception {
    putResponse(//
        "{",
        "  'event': 'completion.results',",
        "  'params': {",
        "    'id': 'completion0',",
        "    'replacementOffset': 107,",
        "    'replacementLength': 108,",
        "    'results' : [",
        "      {",
        "        'kind': 'CLASS',",
        "        'relevance': 'LOW',",
        "        'completion': 'completion0',",
        "        'selectionOffset': 4,",
        "        'selectionLength': 5,",
        "        'isDeprecated': true,",
        "        'isPotential': true,",
        "        'docSummary': 'docSummary0',",
        "        'docComplete': 'docComplete0',",
        "        'declaringType': 'declaringType0',",
        "        'returnType': 'returnType0',",
        "        'parameterNames': ['param0', 'param1'],",
        "        'parameterTypes': ['paramType0', 'paramType1'],",
        "        'requiredParameterCount': 2,",
        "        'positionalParameterCount': 0,",
        "        'parameterName': 'param2',",
        "        'parameterType': 'paramType2'",
        "      },",
        "      {",
        "        'kind': 'CLASS_ALIAS',",
        "        'relevance': 'DEFAULT',",
        "        'completion': 'completion1',",
        "        'selectionOffset': 10,",
        "        'selectionLength': 11,",
        "        'isDeprecated': true,",
        "        'isPotential': true",
        "      }",
        "    ],",
        "    'last': true",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    assertThat(listener.getCompletionReplacementOffset("completion0")).isEqualTo(107);
    assertThat(listener.getCompletionReplacementLength("completion0")).isEqualTo(108);
    CompletionSuggestion[] suggestions = listener.getCompletions("completion0");
    assertThat(suggestions).hasSize(2);
    assertThat(listener.getCompletionIsLast("completion0")).isEqualTo(true);
    {
      CompletionSuggestion suggestion = suggestions[0];
      assertEquals(CompletionSuggestionKind.CLASS, suggestion.getKind());
      assertEquals(CompletionRelevance.LOW, suggestion.getRelevance());
      assertEquals(suggestion.getCompletion(), "completion0");
      assertEquals(suggestion.getSelectionOffset(), 4);
      assertEquals(suggestion.getSelectionLength(), 5);
      assertTrue(suggestion.isDeprecated());
      assertTrue(suggestion.isPotential());
      assertEquals(suggestion.getElementDocSummary(), "docSummary0");
      assertEquals(suggestion.getElementDocDetails(), "docComplete0");
      assertEquals(suggestion.getDeclaringType(), "declaringType0");
      assertEquals(suggestion.getReturnType(), "returnType0");
      String[] parameterNames = suggestion.getParameterNames();
      assertThat(parameterNames).hasSize(2);
      assertThat(parameterNames).contains("param0");
      assertThat(parameterNames).contains("param1");
      String[] parameterTypes = suggestion.getParameterTypes();
      assertThat(parameterTypes).hasSize(2);
      assertThat(parameterTypes).contains("paramType0");
      assertThat(parameterTypes).contains("paramType1");
      assertEquals(suggestion.getRequiredParameterCount(), 2);
      assertEquals(suggestion.getPositionalParameterCount(), 0);
      assertEquals(suggestion.getParameterName(), "param2");
      assertEquals(suggestion.getParameterType(), "paramType2");
      assertFalse(suggestion.hasNamed());
    }
    {
      CompletionSuggestion suggestion = suggestions[1];
      assertEquals(CompletionSuggestionKind.CLASS_ALIAS, suggestion.getKind());
      assertEquals(CompletionRelevance.DEFAULT, suggestion.getRelevance());
      assertEquals(suggestion.getCompletion(), "completion1");
      assertEquals(suggestion.getSelectionOffset(), 10);
      assertEquals(suggestion.getSelectionLength(), 11);
      assertTrue(suggestion.isDeprecated());
      assertTrue(suggestion.isPotential());
      // optional params
      assertNull(suggestion.getElementDocSummary());
      assertNull(suggestion.getElementDocDetails());
      assertNull(suggestion.getDeclaringType());
      assertNull(suggestion.getReturnType());
      assertThat(suggestion.getParameterNames()).hasSize(0);
      assertThat(suggestion.getParameterTypes()).hasSize(0);
      assertEquals(suggestion.getRequiredParameterCount(), 0);
      assertEquals(suggestion.getPositionalParameterCount(), 0);
      assertNull(suggestion.getParameterName());
      assertNull(suggestion.getParameterType());
      assertFalse(suggestion.hasNamed());
    }
  }

  // TODO (jwren) refactoring API changed
//  public void test_edit_applyRefactoring() throws Exception {
//    final RefactoringProblem[][] problemsArray = {{null}};
//    final SourceChange[] sourceChangeArray = {null};
//    server.applyRefactoring("refactoringId1", new RefactoringApplyConsumer() {
//      @Override
//      public void computed(RefactoringProblem[] problems, SourceChange sourceChange) {
//        problemsArray[0] = problems;
//        sourceChangeArray[0] = sourceChange;
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.applyRefactoring',",
//        "  'params': {",
//        "    'id': 'refactoringId1'",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'status': [",
//        "      {",
//        "        'severity':'INFO',",
//        "        'message':'message1',",
//        "        'location': {",
//        "          'file': 'someFile.dart',",
//        "          'offset': 1,",
//        "          'length': 2,",
//        "          'startLine': 3,",
//        "          'startColumn': 4",
//        "        }",
//        "      },",
//        "      {",
//        "        'severity':'WARNING',",
//        "        'message':'message2',",
//        "        'location': {",
//        "          'file': 'someFile2.dart',",
//        "          'offset': 5,",
//        "          'length': 6,",
//        "          'startLine': 7,",
//        "          'startColumn': 8",
//        "        }",
//        "      }",
//        "    ],",
//        "    'change': {",
//        "      'message': 'message3',",
//        "      'edits': [",
//        "        {",
//        "          'file':'someFile3.dart',",
//        "          'edits': [",
//        "            {",
//        "              'offset': 9,",
//        "              'length': 10,",
//        "              'replacement': 'replacement1'",
//        "            }",
//        "          ]",
//        "        }",
//        "      ]",
//        "    }",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions on 'status' (RefactoringProblem array)
//    {
//      assertThat(problemsArray[0]).hasSize(2);
//      RefactoringProblem refactoringProblem1 = problemsArray[0][0];
//      RefactoringProblem refactoringProblem2 = problemsArray[0][1];
//      {
//        assertEquals(RefactoringProblemSeverity.INFO, refactoringProblem1.getSeverity());
//        assertEquals("message1", refactoringProblem1.getMessage());
//        assertEquals(
//            new LocationImpl("someFile.dart", 1, 2, 3, 4),
//            refactoringProblem1.getLocation());
//      }
//      {
//        assertEquals(RefactoringProblemSeverity.WARNING, refactoringProblem2.getSeverity());
//        assertEquals("message2", refactoringProblem2.getMessage());
//        assertEquals(
//            new LocationImpl("someFile2.dart", 5, 6, 7, 8),
//            refactoringProblem2.getLocation());
//      }
//    }
//
//    // assertions on 'change' (SourceChange)
//    {
//      assertEquals("message3", sourceChangeArray[0].getMessage());
//      assertThat(sourceChangeArray[0].getEdits()).hasSize(1);
//      SourceFileEdit sourceFileEdit = sourceChangeArray[0].getEdits()[0];
//      assertEquals("someFile3.dart", sourceFileEdit.getFile());
//      assertThat(sourceFileEdit.getEdits()).hasSize(1);
//      SourceEdit sourceEdit = sourceFileEdit.getEdits()[0];
//      assertEquals(9, sourceEdit.getOffset());
//      assertEquals(10, sourceEdit.getLength());
//      assertEquals("replacement1", sourceEdit.getReplacement());
//    }
//  }
//
//  public void test_edit_applyRefactoring_emptyLists() throws Exception {
//    final RefactoringProblem[][] problemsArray = {{null}};
//    final SourceChange[] sourceChangeArray = {null};
//    server.applyRefactoring("refactoringId1", new RefactoringApplyConsumer() {
//      @Override
//      public void computed(RefactoringProblem[] problems, SourceChange sourceChange) {
//        problemsArray[0] = problems;
//        sourceChangeArray[0] = sourceChange;
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.applyRefactoring',",
//        "  'params': {",
//        "    'id': 'refactoringId1'",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'status': [],",
//        "    'change': {",
//        "      'message': 'message1',",
//        "      'edits': []",
//        "    }",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//    assertThat(problemsArray[0]).isEmpty();
//    assertEquals("message1", sourceChangeArray[0].getMessage());
//    assertThat(sourceChangeArray[0].getEdits()).isEmpty();
//  }
//
//  public void test_edit_createRefactoring() throws Exception {
//    final String[] refactoringId = {null};
//    final RefactoringProblem[][] problemsArray = {{null}};
//    server.createRefactoring(
//        RefactoringKind.CONVERT_GETTER_TO_METHOD,
//        "/fileA.dart",
//        1,
//        2,
//        new RefactoringCreateConsumer() {
//          @Override
//          public void computedStatus(String id, RefactoringProblem[] status,
//              Map<String, Object> feedback) {
//            refactoringId[0] = id;
//            problemsArray[0] = status;
//          }
//        });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.createRefactoring',",
//        "  'params': {",
//        "    'kind': 'CONVERT_GETTER_TO_METHOD',",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'id': 'refactoringId0',",
//        "    'status': [",
//        "      {",
//        "        'severity':'INFO',",
//        "        'message':'message1',",
//        "        'location': {",
//        "          'file': 'someFile.dart',",
//        "          'offset': 1,",
//        "          'length': 2,",
//        "          'startLine': 3,",
//        "          'startColumn': 4",
//        "        }",
//        "      },",
//        "      {",
//        "        'severity':'WARNING',",
//        "        'message':'message2',",
//        "        'location': {",
//        "          'file': 'someFile2.dart',",
//        "          'offset': 5,",
//        "          'length': 6,",
//        "          'startLine': 7,",
//        "          'startColumn': 8",
//        "        }",
//        "      }",
//        "    ],",
//        "    'feedback': {}",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertion on refactoring id
//    assertEquals("refactoringId0", refactoringId[0]);
//
//    // assertions on 'status' (RefactoringProblem array)
//    {
//      assertThat(problemsArray[0]).hasSize(2);
//      RefactoringProblem refactoringProblem1 = problemsArray[0][0];
//      RefactoringProblem refactoringProblem2 = problemsArray[0][1];
//      {
//        assertEquals(RefactoringProblemSeverity.INFO, refactoringProblem1.getSeverity());
//        assertEquals("message1", refactoringProblem1.getMessage());
//        assertEquals(
//            new LocationImpl("someFile.dart", 1, 2, 3, 4),
//            refactoringProblem1.getLocation());
//      }
//      {
//        assertEquals(RefactoringProblemSeverity.WARNING, refactoringProblem2.getSeverity());
//        assertEquals("message2", refactoringProblem2.getMessage());
//        assertEquals(
//            new LocationImpl("someFile2.dart", 5, 6, 7, 8),
//            refactoringProblem2.getLocation());
//      }
//    }
//  }
//
//  public void test_edit_createRefactoring_emptyLists() throws Exception {
//    final String[] refactoringId = {null};
//    final RefactoringProblem[][] problemsArray = {{null}};
//    server.createRefactoring(
//        RefactoringKind.CONVERT_GETTER_TO_METHOD,
//        "/fileA.dart",
//        1,
//        2,
//        new RefactoringCreateConsumer() {
//          @Override
//          public void computedStatus(String id, RefactoringProblem[] status,
//              Map<String, Object> feedback) {
//            refactoringId[0] = id;
//            problemsArray[0] = status;
//          }
//        });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.createRefactoring',",
//        "  'params': {",
//        "    'kind': 'CONVERT_GETTER_TO_METHOD',",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'id': 'refactoringId0',",
//        "    'status': [],",
//        "    'feedback': {}",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions
//    assertEquals("refactoringId0", refactoringId[0]);
//    assertThat(problemsArray[0]).isEmpty();
//  }
//
//  public void test_edit_createRefactoring_extractLocalVariable() throws Exception {
//    final String[] refactoringId = {null};
//    final RefactoringProblem[][] problemsArray = {{null}};
//    @SuppressWarnings("rawtypes")
//    final Map[] feedback = {null};
//    server.createRefactoring(
//        RefactoringKind.EXTRACT_LOCAL_VARIABLE,
//        "/fileA.dart",
//        1,
//        2,
//        new RefactoringCreateConsumer() {
//          @Override
//          public void computedStatus(String id, RefactoringProblem[] status, Map<String, Object> f) {
//            refactoringId[0] = id;
//            problemsArray[0] = status;
//            feedback[0] = f;
//          }
//        });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.createRefactoring',",
//        "  'params': {",
//        "    'kind': 'EXTRACT_LOCAL_VARIABLE',",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'id': 'refactoringId0',",
//        "    'status': [],",
//        "    'feedback': {",
//        "      'names':['a','b','c'],",
//        "      'offsets':[1,2,3],",
//        "      'lengths':[4,5,6]",
//        "    }",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions
//    assertEquals("refactoringId0", refactoringId[0]);
//    assertThat(problemsArray[0]).isEmpty();
//    assertThat(feedback[0]).hasSize(3);
//    assertTrue(feedback[0].containsKey("names"));
//    assertTrue(feedback[0].containsKey("offsets"));
//    assertTrue(feedback[0].containsKey("lengths"));
//    assertThat(feedback[0].get("names")).isEqualTo(new String[] {"a", "b", "c"});
//    assertThat(feedback[0].get("offsets")).isEqualTo(new int[] {1, 2, 3});
//    assertThat(feedback[0].get("lengths")).isEqualTo(new int[] {4, 5, 6});
//  }
//
//  public void test_edit_createRefactoring_extractMethod() throws Exception {
//    final String[] refactoringId = {null};
//    final RefactoringProblem[][] problemsArray = {{null}};
//    @SuppressWarnings("rawtypes")
//    final Map[] feedback = {null};
//    server.createRefactoring(
//        RefactoringKind.EXTRACT_METHOD,
//        "/fileA.dart",
//        1,
//        2,
//        new RefactoringCreateConsumer() {
//          @Override
//          public void computedStatus(String id, RefactoringProblem[] status, Map<String, Object> f) {
//            refactoringId[0] = id;
//            problemsArray[0] = status;
//            feedback[0] = f;
//          }
//        });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.createRefactoring',",
//        "  'params': {",
//        "    'kind': 'EXTRACT_METHOD',",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'id': 'refactoringId0',",
//        "    'status': [],",
//        "    'feedback': {",
//        "      'offset': 1,",
//        "      'length': 2,",
//        "      'returnType': 'returnType0',",
//        "      'names': ['a', 'b'],",
//        "      'canCreateGetter': false,",
//        "      'parameters': [",
//        "        {",
//        "          'type':'type0',",
//        "          'name':'name0'",
//        "        },",
//        "        {",
//        "          'type':'type1',",
//        "          'name':'name1'",
//        "        }",
//        "      ],",
//        "      'occurrences': 3,",
//        "      'offsets': [4, 5, 6],",
//        "      'lengths': [7, 8, 9]",
//        "    }",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions
//    assertEquals("refactoringId0", refactoringId[0]);
//    assertThat(problemsArray[0]).isEmpty();
//    assertThat(feedback[0]).hasSize(9);
//    assertTrue(feedback[0].containsKey("offset"));
//    assertTrue(feedback[0].containsKey("length"));
//    assertTrue(feedback[0].containsKey("returnType"));
//    assertTrue(feedback[0].containsKey("names"));
//    assertTrue(feedback[0].containsKey("canCreateGetter"));
//    assertTrue(feedback[0].containsKey("parameters"));
//    assertTrue(feedback[0].containsKey("occurrences"));
//    assertTrue(feedback[0].containsKey("offsets"));
//    assertTrue(feedback[0].containsKey("lengths"));
//    assertThat(feedback[0].get("offset")).isEqualTo(1);
//    assertThat(feedback[0].get("length")).isEqualTo(2);
//    assertThat(feedback[0].get("returnType")).isEqualTo("returnType0");
//    assertThat(feedback[0].get("names")).isEqualTo(new String[] {"a", "b"});
//    assertThat(feedback[0].get("canCreateGetter")).isEqualTo(false);
//    assertThat(feedback[0].get("parameters")).isEqualTo(
//        new Parameter[] {new ParameterImpl("type0", "name0"), new ParameterImpl("type1", "name1")});
//    assertThat(feedback[0].get("occurrences")).isEqualTo(3);
//    assertThat(feedback[0].get("offsets")).isEqualTo(new int[] {4, 5, 6});
//    assertThat(feedback[0].get("lengths")).isEqualTo(new int[] {7, 8, 9});
//  }
//
//  public void test_edit_createRefactoring_rename() throws Exception {
//    final String[] refactoringId = {null};
//    final RefactoringProblem[][] problemsArray = {{null}};
//    @SuppressWarnings("rawtypes")
//    final Map[] feedback = {null};
//    server.createRefactoring(
//        RefactoringKind.RENAME,
//        "/fileA.dart",
//        1,
//        2,
//        new RefactoringCreateConsumer() {
//          @Override
//          public void computedStatus(String id, RefactoringProblem[] status, Map<String, Object> f) {
//            refactoringId[0] = id;
//            problemsArray[0] = status;
//            feedback[0] = f;
//          }
//        });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.createRefactoring',",
//        "  'params': {",
//        "    'kind': 'RENAME',",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'id': 'refactoringId0',",
//        "    'status': [],",
//        "    'feedback': {",
//        "      'offset': 1,",
//        "      'length': 2",
//        "    }",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions
//    assertEquals("refactoringId0", refactoringId[0]);
//    assertThat(problemsArray[0]).isEmpty();
//    assertThat(feedback[0]).hasSize(2);
//    assertTrue(feedback[0].containsKey("offset"));
//    assertTrue(feedback[0].containsKey("length"));
//    assertThat(feedback[0].get("offset")).isEqualTo(1);
//    assertThat(feedback[0].get("length")).isEqualTo(2);
//  }
//
//  public void test_edit_deleteRefactoring() throws Exception {
//    server.deleteRefactoring("refactoringId0");
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.deleteRefactoring',",
//        "  'params': {",
//        "    'id': 'refactoringId0'",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }

  public void test_edit_getAssists() throws Exception {
    final SourceChange[][] sourceChangeArray = {{null}};
    server.edit_getAssists("/fileA.dart", 1, 2, new GetAssistsConsumer() {
      @Override
      public void computedSourceChanges(SourceChange[] sourceChanges) {
        sourceChangeArray[0] = sourceChanges;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'edit.getAssists',",
        "  'params': {",
        "    'file': '/fileA.dart',",
        "    'offset': 1,",
        "    'length': 2",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'assists': [",
        "      {",
        "        'message': 'message1',",
        "        'edits': [",
        "          {",
        "            'file':'someFile1.dart',",
        "            'edits': [",
        "              {",
        "                'offset': 1,",
        "                'length': 2,",
        "                'replacement': 'replacement1'",
        "             }",
        "            ]",
        "          }",
        "        ]",
        "      },",
        "      {",
        "        'message': 'message2',",
        "        'edits': [",
        "          {",
        "            'file':'someFile2.dart',",
        "            'edits': [",
        "              {",
        "                'offset': 3,",
        "                'length': 4,",
        "                'replacement': 'replacement2'",
        "             }",
        "            ]",
        "          }",
        "        ]",
        "      }",
        "    ]",
        "  }",
        "}");
    server.test_waitForWorkerComplete();

    // assertions on 'refactorings' (List<SourceChange>)
    SourceChange[] sourceChanges = sourceChangeArray[0];
    assertThat(sourceChanges).hasSize(2);
    {
      assertEquals("message1", sourceChanges[0].getMessage());
      assertThat(sourceChanges[0].getEdits()).hasSize(1);
      SourceFileEdit sourceFileEdit = sourceChanges[0].getEdits()[0];
      assertEquals("someFile1.dart", sourceFileEdit.getFile());
      assertThat(sourceFileEdit.getEdits()).hasSize(1);
      SourceEdit sourceEdit = sourceFileEdit.getEdits()[0];
      assertEquals(1, sourceEdit.getOffset());
      assertEquals(2, sourceEdit.getLength());
      assertEquals("replacement1", sourceEdit.getReplacement());
    }
    {
      assertEquals("message2", sourceChanges[1].getMessage());
      assertThat(sourceChanges[1].getEdits()).hasSize(1);
      SourceFileEdit sourceFileEdit = sourceChanges[1].getEdits()[0];
      assertEquals("someFile2.dart", sourceFileEdit.getFile());
      assertThat(sourceFileEdit.getEdits()).hasSize(1);
      SourceEdit sourceEdit = sourceFileEdit.getEdits()[0];
      assertEquals(3, sourceEdit.getOffset());
      assertEquals(4, sourceEdit.getLength());
      assertEquals("replacement2", sourceEdit.getReplacement());
    }
  }

  public void test_edit_getFixes() throws Exception {
    final ErrorFixes[][] errorFixesArray = {{null}};
    server.edit_getFixes("/fileA.dart", 1, new GetFixesConsumer() {
      @Override
      public void computedFixes(ErrorFixes[] e) {
        errorFixesArray[0] = e;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'edit.getFixes',",
        "  'params': {",
        "    'file': '/fileA.dart',",
        "    'offset': 1",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'fixes': [",
        "      {",
        "        'error': {",
        "          'severity': 'ERROR',",
        "          'type': 'SYNTACTIC_ERROR',",
        "          'location': {",
        "            'file': '/fileA.dart',",
        "            'offset': 1,",
        "            'length': 2,",
        "            'startLine': 3,",
        "            'startColumn': 4",
        "          },",
        "          'message': 'message A',",
        "          'correction': 'correction A'",
        "        },",
        "        'fixes': [",
        "          {",
        "            'message': 'message3',",
        "            'edits': [",
        "              {",
        "                'file':'someFile3.dart',",
        "                'edits': [",
        "                  {",
        "                    'offset': 9,",
        "                    'length': 10,",
        "                    'replacement': 'replacement1'",
        "                  }",
        "                ]",
        "              }",
        "            ]",
        "          }",
        "        ]",
        "      },",
        "      {",
        "        'error': {",
        "          'severity': 'ERROR',",
        "          'type': 'COMPILE_TIME_ERROR',",
        "          'location': {",
        "            'file': '/fileB.dart',",
        "            'offset': 5,",
        "            'length': 6,",
        "            'startLine': 7,",
        "            'startColumn': 8",
        "          },",
        "          'message': 'message B',",
        "          'correction': 'correction B'",
        "        },",
        "        'fixes':[]",
        "      }",
        "    ]",
        "  }",
        "}");
    server.test_waitForWorkerComplete();

    // assertions on 'fixes' (List<ErrorFixes>)
    ErrorFixes[] errorFixes = errorFixesArray[0];
    assertThat(errorFixes).hasSize(2);
    {
      AnalysisError error = errorFixes[0].getError();
      assertEquals(new AnalysisError(ErrorSeverity.ERROR, "SYNTACTIC_ERROR", new Location(
          "/fileA.dart",
          1,
          2,
          3,
          4), "message A", "correction A"), error);
      SourceChange[] sourceChangeArray = errorFixes[0].getFixes();
      assertThat(sourceChangeArray).hasSize(1);
      assertEquals("message3", sourceChangeArray[0].getMessage());
      assertThat(sourceChangeArray[0].getEdits()).hasSize(1);
      SourceFileEdit sourceFileEdit = sourceChangeArray[0].getEdits()[0];
      assertEquals("someFile3.dart", sourceFileEdit.getFile());
      assertThat(sourceFileEdit.getEdits()).hasSize(1);
      SourceEdit sourceEdit = sourceFileEdit.getEdits()[0];
      assertEquals(9, sourceEdit.getOffset());
      assertEquals(10, sourceEdit.getLength());
      assertEquals("replacement1", sourceEdit.getReplacement());
    }
    {
      AnalysisError error = errorFixes[1].getError();
      assertEquals(new AnalysisError(ErrorSeverity.ERROR, "COMPILE_TIME_ERROR", new Location(
          "/fileB.dart",
          5,
          6,
          7,
          8), "message B", "correction B"), error);
      SourceChange[] sourceChangeArray = errorFixes[1].getFixes();
      assertThat(sourceChangeArray).isEmpty();
    }
  }

  // TODO (jwren) refactoring API changed
//  public void test_edit_getRefactorings() throws Exception {
//    final String[][] refactoringKindsArray = {{null}};
//    server.getRefactorings("/fileA.dart", 1, 2, new RefactoringGetConsumer() {
//      @Override
//      public void computedRefactoringKinds(String[] refactoringKinds) {
//        refactoringKindsArray[0] = refactoringKinds;
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.getRefactorings',",
//        "  'params': {",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'kinds': ['CONVERT_GETTER_TO_METHOD','CONVERT_METHOD_TO_GETTER','EXTRACT_LOCAL_VARIABLE']",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions on 'kinds' (List<RefactoringKind>)
//    String[] refactoringKinds = refactoringKindsArray[0];
//    assertThat(refactoringKinds).hasSize(3);
//    assertThat(refactoringKinds).contains(
//        RefactoringKind.CONVERT_GETTER_TO_METHOD,
//        RefactoringKind.CONVERT_METHOD_TO_GETTER,
//        RefactoringKind.EXTRACT_LOCAL_VARIABLE);
//  }
//
//  public void test_edit_getRefactorings_emptyKindsList() throws Exception {
//    final String[][] refactoringKindsArray = {{null}};
//    server.getRefactorings("/fileA.dart", 1, 2, new RefactoringGetConsumer() {
//      @Override
//      public void computedRefactoringKinds(String[] refactoringKinds) {
//        refactoringKindsArray[0] = refactoringKinds;
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.getRefactorings',",
//        "  'params': {",
//        "    'file': '/fileA.dart',",
//        "    'offset': 1,",
//        "    'length': 2",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'kinds': []",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions on 'kinds' (List<RefactoringKind>)
//    assertThat(refactoringKindsArray[0]).hasSize(0);
//  }
//
//  public void test_edit_setRefactoringOptions() throws Exception {
//    final RefactoringProblem[][] refactoringProblemsArray = {{null}};
//    server.setRefactoringOptions("refactoringId0", null, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//        refactoringProblemsArray[0] = problems;
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {}",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'status': [",
//        "      {",
//        "        'severity':'INFO',",
//        "        'message':'message1',",
//        "        'location': {",
//        "          'file': 'someFile.dart',",
//        "          'offset': 1,",
//        "          'length': 2,",
//        "          'startLine': 3,",
//        "          'startColumn': 4",
//        "        }",
//        "      },",
//        "      {",
//        "        'severity':'WARNING',",
//        "        'message':'message2',",
//        "        'location': {",
//        "          'file': 'someFile2.dart',",
//        "          'offset': 5,",
//        "          'length': 6,",
//        "          'startLine': 7,",
//        "          'startColumn': 8",
//        "        }",
//        "      }",
//        "    ]",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions on 'status' (RefactoringProblem array)
//    RefactoringProblem[] refactoringProblems = refactoringProblemsArray[0];
//    assertThat(refactoringProblems).hasSize(2);
//
//    RefactoringProblem refactoringProblem1 = refactoringProblems[0];
//    RefactoringProblem refactoringProblem2 = refactoringProblems[1];
//    {
//      assertEquals(RefactoringProblemSeverity.INFO, refactoringProblem1.getSeverity());
//      assertEquals("message1", refactoringProblem1.getMessage());
//      assertEquals(new LocationImpl("someFile.dart", 1, 2, 3, 4), refactoringProblem1.getLocation());
//    }
//    {
//      assertEquals(RefactoringProblemSeverity.WARNING, refactoringProblem2.getSeverity());
//      assertEquals("message2", refactoringProblem2.getMessage());
//      assertEquals(
//          new LocationImpl("someFile2.dart", 5, 6, 7, 8),
//          refactoringProblem2.getLocation());
//    }
//  }
//
//  public void test_edit_setRefactoringOptions_emptyProblemsList() throws Exception {
//    final RefactoringProblem[][] refactoringProblemsArray = {{null}};
//    server.setRefactoringOptions("refactoringId0", null, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//        refactoringProblemsArray[0] = problems;
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {}",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//
//    putResponse(//
//        "{",
//        "  'id': '0',",
//        "  'result': {",
//        "    'status': [",
//        "    ]",
//        "  }",
//        "}");
//    server.test_waitForWorkerComplete();
//
//    // assertions on 'status' (RefactoringProblem array)
//    RefactoringProblem[] refactoringProblems = refactoringProblemsArray[0];
//    assertThat(refactoringProblems).isEmpty();
//  }
//
//  public void test_edit_setRefactoringOptions_extractLocalVariable() throws Exception {
//    HashMap<String, Object> options = new HashMap<String, Object>();
//    options.put("name", "name1");
//    options.put("extractAll", Boolean.TRUE);
//    server.setRefactoringOptions("refactoringId0", options, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {",
//        "      'name': 'name1',",
//        "      'extractAll': true",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_edit_setRefactoringOptions_extractMethod_noParameters() throws Exception {
//    HashMap<String, Object> options = new HashMap<String, Object>();
//    options.put("returnType", "returnType1");
//    options.put("createGetter", Boolean.TRUE);
//    options.put("name", "name1");
//    options.put("parameters", new Parameter[] {});
//    options.put("extractAll", Boolean.TRUE);
//
//    server.setRefactoringOptions("refactoringId0", options, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {",
//        "      'returnType': 'returnType1',",
//        "      'createGetter': true,",
//        "      'name': 'name1',",
//        "      'parameters': [],",
//        "      'extractAll': true",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_edit_setRefactoringOptions_extractMethod_someParameters() throws Exception {
//    HashMap<String, Object> options = new HashMap<String, Object>();
//    options.put("returnType", "returnType1");
//    options.put("createGetter", Boolean.FALSE);
//    options.put("name", "name1");
//    options.put("parameters", new Parameter[] {
//        new ParameterImpl("type1", "name1"), new ParameterImpl("type2", "name2")});
//    options.put("extractAll", Boolean.FALSE);
//
//    server.setRefactoringOptions("refactoringId0", options, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {",
//        "      'returnType': 'returnType1',",
//        "      'createGetter': false,",
//        "      'name': 'name1',",
//        "      'parameters': [",
//        "        {",
//        "          'type': 'type1',",
//        "          'name': 'name1'",
//        "        },",
//        "        {",
//        "          'type': 'type2',",
//        "          'name': 'name2'",
//        "        }",
//        "      ],",
//        "      'extractAll': false",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_edit_setRefactoringOptions_inlineMethod_false() throws Exception {
//    HashMap<String, Object> options = new HashMap<String, Object>();
//    options.put("deleteSource", Boolean.FALSE);
//    options.put("inlineAll", Boolean.FALSE);
//
//    server.setRefactoringOptions("refactoringId0", options, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {",
//        "      'deleteSource': false,",
//        "      'inlineAll': false",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_edit_setRefactoringOptions_inlineMethod_true() throws Exception {
//    HashMap<String, Object> options = new HashMap<String, Object>();
//    options.put("deleteSource", Boolean.TRUE);
//    options.put("inlineAll", Boolean.TRUE);
//
//    server.setRefactoringOptions("refactoringId0", options, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {",
//        "      'deleteSource': true,",
//        "      'inlineAll': true",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }
//
//  public void test_edit_setRefactoringOptions_rename() throws Exception {
//    HashMap<String, Object> options = new HashMap<String, Object>();
//    options.put("newName", "newName1");
//
//    server.setRefactoringOptions("refactoringId0", options, new RefactoringSetOptionsConsumer() {
//      @Override
//      public void computedStatus(RefactoringProblem[] problems) {
//      }
//    });
//    List<JsonObject> requests = requestSink.getRequests();
//    JsonElement expected = parseJson(//
//        "{",
//        "  'id': '0',",
//        "  'method': 'edit.setRefactoringOptions',",
//        "  'params': {",
//        "    'id': 'refactoringId0',",
//        "    'options': {",
//        "      'newName': 'newName1'",
//        "    }",
//        "  }",
//        "}");
//    assertTrue(requests.contains(expected));
//  }

  public void test_error() throws Exception {
    server.server_shutdown();
    putResponse(//
        "{",
        "  'id': '0',",
        "  'error': {",
        "    'code': 'SOME_CODE',",
        "    'message': 'testing parsing of error response'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
  }

  public void test_search_findElementReferences() throws Exception {
    final String[] result = new String[1];
    server.findElementReferences("/fileA.dart", 17, false, new SearchIdConsumer() {
      @Override
      public void computedSearchId(String searchId) {
        result[0] = searchId;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'search.findElementReferences',",
        "  'params': {",
        "    'file': '/fileA.dart',",
        "    'offset': 17,",
        "    'includePotential': false",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'id': 'searchId0'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertEquals("searchId0", result[0]);
  }

  public void test_search_findMemberDeclarations() throws Exception {
    final String[] result = new String[1];
    server.findMemberDeclarations("mydeclaration", new SearchIdConsumer() {
      @Override
      public void computedSearchId(String searchId) {
        result[0] = searchId;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'search.findMemberDeclarations',",
        "  'params': {",
        "    'name': 'mydeclaration'",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'id': 'searchId1'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertEquals("searchId1", result[0]);
  }

  public void test_search_findMemberReferences() throws Exception {
    final String[] result = new String[1];
    server.findMemberReferences("mydeclaration", new SearchIdConsumer() {
      @Override
      public void computedSearchId(String searchId) {
        result[0] = searchId;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'search.findMemberReferences',",
        "  'params': {",
        "    'name': 'mydeclaration'",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'id': 'searchId2'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertEquals("searchId2", result[0]);
  }

  public void test_search_findTopLevelDeclarations() throws Exception {
    final String[] result = new String[1];
    server.findTopLevelDeclarations("some-pattern", new SearchIdConsumer() {
      @Override
      public void computedSearchId(String searchId) {
        result[0] = searchId;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'search.findTopLevelDeclarations',",
        "  'params': {",
        "    'pattern': 'some-pattern'",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'id': 'searchId3'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertEquals("searchId3", result[0]);
  }

  public void test_search_getTypeHierarchy() throws Exception {
    final TypeHierarchyItem[] items = new TypeHierarchyItem[1];
    server.search_getTypeHierarchy("/fileA.dart", 1, new GetTypeHierarchyConsumer() {
      @Override
      public void computedHierarchy(TypeHierarchyItem target) {
        items[0] = target;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'search.getTypeHierarchy',",
        "  'params': {",
        "    'file': '/fileA.dart',",
        "    'offset': 1",
        "  }",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'hierarchy': {",
        "      'classElement': {",
        "        'kind': 'CLASS',",
        "        'name': 'name1',",
        "        'location': {",
        "          'file': '/test1.dart',",
        "          'offset': 1,",
        "          'length': 2,",
        "          'startLine': 3,",
        "          'startColumn': 4",
        "        },",
        "        'flags': 63",
        "      },",
        "      'displayName': 'displayName1',",
        "      'memberElement': {",
        "        'kind': 'CLASS',",
        "        'name': 'name2',",
        "        'location': {",
        "          'file': '/test2.dart',",
        "          'offset': 5,",
        "          'length': 6,",
        "          'startLine': 7,",
        "          'startColumn': 8",
        "        },",
        "        'flags': 0",
        "      },",
        "      'superclass': {",
        "        'classElement': {",
        "          'kind': 'CLASS',",
        "          'name': 'name3',",
        "          'location': {",
        "            'file': '/test3.dart',",
        "            'offset': 9,",
        "            'length': 10,",
        "            'startLine': 11,",
        "            'startColumn': 12",
        "          },",
        "          'flags': 63",
        "        },",
        "        'interfaces': [],",
        "        'mixins': [],",
        "        'subclasses': []",
        "      },",
        "      'interfaces': [],",
        "      'mixins': [],",
        "      'subclasses': []",
        "    }",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    TypeHierarchyItem item = items[0];
    assertNotNull(item);
    // classElement
    {
      Element element = item.getClassElement();
      assertEquals(ElementKind.CLASS, element.getKind());
      assertEquals("name1", element.getName());
      Location location = element.getLocation();
      assertEquals("/test1.dart", location.getFile());
      assertEquals(1, location.getOffset());
      assertEquals(2, location.getLength());
      assertEquals(3, location.getStartLine());
      assertEquals(4, location.getStartColumn());
      assertTrue(element.isAbstract());
      assertTrue(element.isConst());
      assertTrue(element.isDeprecated());
      assertTrue(element.isFinal());
      assertTrue(element.isPrivate());
      assertTrue(element.isTopLevelOrStatic());
    }
    // displayName
    assertEquals("displayName1", item.getDisplayName());
    assertEquals("displayName1", item.getBestName());
    // memberElement
    {
      Element element = item.getMemberElement();
      assertEquals(ElementKind.CLASS, element.getKind());
      assertEquals("name2", element.getName());
      Location location = element.getLocation();
      assertEquals("/test2.dart", location.getFile());
      assertEquals(5, location.getOffset());
      assertEquals(6, location.getLength());
      assertEquals(7, location.getStartLine());
      assertEquals(8, location.getStartColumn());
      assertFalse(element.isAbstract());
      assertFalse(element.isConst());
      assertFalse(element.isDeprecated());
      assertFalse(element.isFinal());
      assertFalse(element.isPrivate());
      assertFalse(element.isTopLevelOrStatic());
    }
    // extendedType
    {
      TypeHierarchyItem childItem = item.getSuperclass();
      assertNotNull(childItem);
      {
        Element element = childItem.getClassElement();
        assertEquals(ElementKind.CLASS, element.getKind());
        assertEquals("name3", element.getName());
        assertEquals("name3", childItem.getBestName());
        Location location = element.getLocation();
        assertEquals("/test3.dart", location.getFile());
        assertEquals(9, location.getOffset());
        assertEquals(10, location.getLength());
        assertEquals(11, location.getStartLine());
        assertEquals(12, location.getStartColumn());
      }
      assertNull(childItem.getDisplayName());
      assertNull(childItem.getMemberElement());
      assertNull(childItem.getSuperclass());
      assertThat(childItem.getInterfaces()).hasSize(0);
      assertThat(childItem.getMixins()).hasSize(0);
      assertThat(childItem.getSubclasses()).hasSize(0);
    }
    // implementedTypes/ withTypes/ subtypes
    assertThat(item.getInterfaces()).hasSize(0);
    assertThat(item.getMixins()).hasSize(0);
    assertThat(item.getSubclasses()).hasSize(0);
  }

  public void test_search_notification_results() throws Exception {
    putResponse(//
        "{",
        "  'event': 'search.results',",
        "  'params': {",
        "    'id': 'searchId7',",
        "    'results' : [",
        "      {",
        "        'location': {",
        "          'file': 'someFile.dart',",
        "          'offset': 9,",
        "          'length': 10,",
        "          'startLine': 11,",
        "          'startColumn': 12",
        "        },",
        "        'kind': 'DECLARATION',",
        "        'isPotential': true,",
        "        'path': [",
        "          {",
        "            'kind': 'FUNCTION',",
        "            'name': 'foo',",
        "            'location': {",
        "              'file': 'fileA.dart',",
        "              'offset': 13,",
        "              'length': 14,",
        "              'startLine': 15,",
        "              'startColumn': 16",
        "            },",
        "            'flags': 42,",
        "            'parameters': '(a, b, c)',",
        "            'returnType': 'anotherType'",
        "          },",
        "          {",
        "            'kind': 'CLASS',",
        "            'name': 'myClass',",
        "            'location': {",
        "              'file': 'fileB.dart',",
        "              'offset': 17,",
        "              'length': 18,",
        "              'startLine': 19,",
        "              'startColumn': 20",
        "            },",
        "            'flags': 21",
        "          }",
        "        ]",
        "      }",
        "    ],",
        "    'last': true",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    SearchResult[] results = listener.getSearchResults("searchId7");
    assertThat(results).hasSize(1);
    {
      SearchResult result = results[0];
      assertLocation(result.getLocation(), "someFile.dart", 9, 10, 11, 12);
      assertEquals(SearchResultKind.DECLARATION, result.getKind());
      assertEquals(true, result.isPotential());
      {
        Element[] path = result.getPath();
        assertThat(path).hasSize(2);
        {
          Element element = path[0];
          assertEquals(ElementKind.FUNCTION, element.getKind());
          assertEquals("foo", element.getName());
          assertLocation(element.getLocation(), "fileA.dart", 13, 14, 15, 16);
          assertEquals(false, element.isAbstract());
          assertEquals(true, element.isConst());
          assertEquals(false, element.isFinal());
          assertEquals(true, element.isTopLevelOrStatic());
          assertEquals(false, element.isPrivate());
          assertEquals(true, element.isDeprecated());
          assertEquals("(a, b, c)", element.getParameters());
          assertEquals("anotherType", element.getReturnType());
        }
        {
          Element element = path[1];
          assertEquals(ElementKind.CLASS, element.getKind());
          assertEquals("myClass", element.getName());
          assertLocation(element.getLocation(), "fileB.dart", 17, 18, 19, 20);
          assertEquals(true, element.isAbstract());
          assertEquals(false, element.isConst());
          assertEquals(true, element.isFinal());
          assertEquals(false, element.isTopLevelOrStatic());
          assertEquals(true, element.isPrivate());
          assertEquals(false, element.isDeprecated());
          assertNull(element.getParameters());
          assertNull(element.getReturnType());
        }
      }
    }
  }

  public void test_server_getVersion() throws Exception {
    final String[] versionPtr = {null};
    server.server_getVersion(new GetVersionConsumer() {
      @Override
      public void computedVersion(String version) {
        versionPtr[0] = version;
      }
    });
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'server.getVersion'",
        "}");
    assertTrue(requests.contains(expected));

    putResponse(//
        "{",
        "  'id': '0',",
        "  'result': {",
        "    'version': '0.0.1'",
        "  }",
        "}");
    server.test_waitForWorkerComplete();
    assertEquals("0.0.1", versionPtr[0]);
  }

  public void test_server_notification_connected() throws Exception {
    listener.assertServerConnected(false);
    putResponse(//
        "{",
        "  'event': 'server.connected'",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    listener.assertServerConnected(true);
  }

  public void test_server_notification_error() throws Exception {
    putResponse(//
        "{",
        "  'event': 'server.error',",
        "  'params': {",
        "    'fatal': false,",
        "    'message': 'message0',",
        "    'stackTrace': 'stackTrace0'",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    List<AnalysisServerError> errors = Lists.newArrayList();
    errors.add(new AnalysisServerError(false, "message0", "stackTrace0"));
    listener.assertServerErrors(errors);
  }

  public void test_server_notification_error2() throws Exception {
    putResponse(//
        "{",
        "  'event': 'server.error',",
        "  'params': {",
        "    'fatal': false,",
        "    'message': 'message0',",
        "    'stackTrace': 'stackTrace0'",
        "  }",
        "}");
    putResponse(//
        "{",
        "  'event': 'server.error',",
        "  'params': {",
        "    'fatal': true,",
        "    'message': 'message1',",
        "    'stackTrace': 'stackTrace1'",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    List<AnalysisServerError> errors = Lists.newArrayList();
    errors.add(new AnalysisServerError(false, "message0", "stackTrace0"));
    errors.add(new AnalysisServerError(true, "message1", "stackTrace1"));
    listener.assertServerErrors(errors);
  }

  public void test_server_notification_status_false() throws Exception {
    putResponse(//
        "{",
        "  'event': 'server.status',",
        "  'params': {",
        "    'analysis': {",
        "      'analyzing': false",
        "    }",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    ServerStatus serverStatus = new ServerStatus();
    serverStatus.setAnalysisStatus(new AnalysisStatus(false, null));
    listener.assertServerStatus(serverStatus);
  }

  public void test_server_notification_status_true() throws Exception {
    putResponse(//
        "{",
        "  'event': 'server.status',",
        "  'params': {",
        "    'analysis': {",
        "      'analyzing': true,",
        "      'analysisTarget': 'target0'",
        "    }",
        "  }",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    ServerStatus serverStatus = new ServerStatus();
    serverStatus.setAnalysisStatus(new AnalysisStatus(true, "target0"));
    listener.assertServerStatus(serverStatus);
  }

  public void test_server_setSubscriptions_emptyList() throws Exception {
    server.server_setSubscriptions(new ArrayList<String>(0));
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'server.setSubscriptions',",
        "  'params': {",
        "    'subscriptions': []",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_server_setSubscriptions_nullList() throws Exception {
    server.server_setSubscriptions(null);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'server.setSubscriptions',",
        "  'params': {",
        "    'subscriptions': []",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_server_setSubscriptions_status() throws Exception {
    ArrayList<String> subscriptions = new ArrayList<String>();
    subscriptions.add(ServerService.STATUS);
    server.server_setSubscriptions(subscriptions);
    List<JsonObject> requests = requestSink.getRequests();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'server.setSubscriptions',",
        "  'params': {",
        "    'subscriptions': [STATUS]",
        "  }",
        "}");
    assertTrue(requests.contains(expected));
  }

  public void test_server_shutdown() throws Exception {
    server.server_shutdown();
    JsonElement expected = parseJson(//
        "{",
        "  'id': '0',",
        "  'method': 'server.shutdown'",
        "}");
    assertTrue(requestSink.getRequests().contains(expected));
    assertFalse(requestSink.isClosed());
    putResponse(//
        "{",
        "  'id': '0'",
        "}");
    responseStream.waitForEmpty();
    server.test_waitForWorkerComplete();
    assertTrue(requestSink.isClosed());
    assertTrue(socket.isStopped());
  }

  public void test_server_startup() throws Exception {
    server.start(10);
    // Simulate a response
    putResponse(//
        "{",
        "  'id': '0'",
        "}");
    assertTrue(socket.isStarted());
    assertTrue(socket.waitForRestart(500));
    assertTrue(socket.getRequestSink().getRequests().size() > 0);
    assertTrue(socket.isStopped());
    assertTrue(socket.isStarted());
    server.server_shutdown();
  }

  private void assertLocation(Location location, String file, int offset, int length,
      int startLine, int startColumn) {
    assertEquals(file, location.getFile());
    assertEquals(offset, location.getOffset());
    assertEquals(length, location.getLength());
    assertEquals(startLine, location.getStartLine());
    assertEquals(startColumn, location.getStartColumn());
  }

  /**
   * Builds a JSON string from the given lines.
   */
  private JsonElement parseJson(String... lines) {
    String json = Joiner.on('\n').join(lines);
    json = json.replace('\'', '"');
    return new JsonParser().parse(json);
  }
}
