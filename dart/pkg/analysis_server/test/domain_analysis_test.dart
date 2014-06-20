// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.domain.analysis;

import 'dart:async';

import 'package:analysis_server/src/analysis_server.dart';
import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/domain_analysis.dart';
import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_server/src/resource.dart';
import 'package:analyzer/src/generated/engine.dart';
import 'package:unittest/unittest.dart';

import 'mocks.dart';
import 'analysis_abstract.dart';
import 'reflective_tests.dart';


main() {
  groupSep = ' | ';

  runReflectiveTests(AnalysisDomainTest);

  MockServerChannel serverChannel;
  MemoryResourceProvider resourceProvider;
  AnalysisServer server;
  AnalysisDomainHandler handler;

  setUp(() {
    serverChannel = new MockServerChannel();
    resourceProvider = new MemoryResourceProvider();
    server = new AnalysisServer(
        serverChannel, resourceProvider, new MockPackageMapProvider());
    server.defaultSdk = new MockSdk();
    handler = new AnalysisDomainHandler(server);
  });

  group('notification.errors', testNotificationErrors);
  group('updateContent', testUpdateContent);
  group('setSubscriptions', test_setSubscriptions);

  group('AnalysisDomainHandler', () {
    group('setAnalysisRoots', () {
      Request request;

      setUp(() {
        request = new Request('0', ANALYSIS_SET_ANALYSIS_ROOTS);
        request.setParameter(INCLUDED, []);
        request.setParameter(EXCLUDED, []);
      });

      test('excluded', () {
        request.setParameter(EXCLUDED, ['foo']);
        // TODO(scheglov) implement
        var response = handler.handleRequest(request);
        expect(response, isResponseFailure('0'));
      });

      group('included', () {
        test('new folder', () {
          resourceProvider.newFolder('/project');
          resourceProvider.newFile('/project/pubspec.yaml', 'name: project');
          resourceProvider.newFile('/project/bin/test.dart', 'main() {}');
          request.setParameter(
              INCLUDED,
              ['/project']);
          var response = handler.handleRequest(request);
          var serverRef = server;
          expect(response, isResponseSuccess('0'));
          // verify that unit is resolved eventually
          return waitForServerOperationsPerformed(server).then((_) {
            var unit = serverRef.test_getResolvedCompilationUnit('/project/bin/test.dart');
            expect(unit, isNotNull);
          });
        });
      });
    });

    group('setPriorityFiles', () {
      test('invalid', () {
        var request = new Request('0', ANALYSIS_SET_PRIORITY_FILES);
        request.setParameter(FILES, ['/project/lib.dart']);
        var response = handler.handleRequest(request);
        expect(response, isResponseFailure('0'));
      });

      test('valid', () {
        resourceProvider.newFolder('/p1');
        resourceProvider.newFile('/p1/a.dart', 'library a;');
        resourceProvider.newFolder('/p2');
        resourceProvider.newFile('/p2/b.dart', 'library b;');
        resourceProvider.newFile('/p2/c.dart', 'library c;');

        var setRootsRequest = new Request('0', ANALYSIS_SET_ANALYSIS_ROOTS);
        setRootsRequest.setParameter(INCLUDED, ['/p1', '/p2']);
        setRootsRequest.setParameter(EXCLUDED, []);
        var setRootsResponse = handler.handleRequest(setRootsRequest);
        expect(setRootsResponse, isResponseSuccess('0'));

        void setPriorityFiles(List<String> fileList) {
          var request = new Request('0', ANALYSIS_SET_PRIORITY_FILES);
          request.setParameter(FILES, fileList);
          var response = handler.handleRequest(request);
          expect(response, isResponseSuccess('0'));
          // TODO(brianwilkerson) Enable the line below after getPriorityFiles
          // has been implemented.
          // expect(server.getPriorityFiles(), unorderedEquals(fileList));
        }

        setPriorityFiles(['/p1/a.dart', '/p2/b.dart']);
        setPriorityFiles(['/p2/b.dart', '/p2/c.dart']);
        setPriorityFiles([]);
      });
    });

    group('updateOptions', () {
      test('invalid', () {
        var request = new Request('0', ANALYSIS_UPDATE_OPTIONS);
        request.setParameter(OPTIONS, {'not-an-option' : true});
        var response = handler.handleRequest(request);
        expect(response, isResponseFailure('0'));
      });

      test('valid', () {
        AnalysisOptions options = server.contextDirectoryManager.defaultOptions;
        bool analyzeAngular = !options.analyzeAngular;
        bool enableDeferredLoading = options.enableDeferredLoading;
        var request = new Request('0', ANALYSIS_UPDATE_OPTIONS);
        request.setParameter(
            OPTIONS,
            {
              'analyzeAngular' : analyzeAngular,
              'enableDeferredLoading': enableDeferredLoading,
              'enableEnums': false
            });
        var response = handler.handleRequest(request);
        expect(response, isResponseSuccess('0'));
        expect(options.analyzeAngular, equals(analyzeAngular));
        expect(options.enableDeferredLoading, equals(enableDeferredLoading));
      });
    });

    test('updateSdks', () {
      var request = new Request('0', ANALYSIS_UPDATE_SDKS);
      request.setParameter(
          ADDED,
          ['/dart/sdk-1.3', '/dart/sdk-1.4']);
      request.setParameter(
          REMOVED,
          ['/dart/sdk-1.2']);
      request.setParameter(DEFAULT, '/dart/sdk-1.4');
      var response = handler.handleRequest(request);
      // TODO(scheglov) implement
      expect(response, isNull);
    });
  });
}


class AnalysisError {
  final String file;
  final String errorCode;
  final int offset;
  final int length;
  final String message;
  final String correction;
  AnalysisError(this.file, this.errorCode, this.offset, this.length,
      this.message, this.correction);

  @override
  String toString() {
    return 'NotificationError(file=$file; errorCode=$errorCode; '
        'offset=$offset; length=$length; message=$message)';
  }
}


AnalysisError jsonToAnalysisError(Map<String, Object> json) {
  return new AnalysisError(
      json['file'],
      json['errorCode'],
      json['offset'],
      json['length'],
      json['message'],
      json['correction']);
}


/**
 * A helper to test 'analysis.*' requests.
 */
class AnalysisTestHelper {
  MockServerChannel serverChannel;
  MemoryResourceProvider resourceProvider;
  AnalysisServer server;
  AnalysisDomainHandler handler;

  Map<String, List<String>> analysisSubscriptions = {};

  Map<String, List<AnalysisError>> filesErrors = {};
  Map<String, List<Map<String, Object>>> filesHighlights = {};
  Map<String, List<Map<String, Object>>> filesNavigation = {};

  String testFile = '/project/bin/test.dart';
  String testCode;

  AnalysisTestHelper() {
    serverChannel = new MockServerChannel();
    resourceProvider = new MemoryResourceProvider();
    server = new AnalysisServer(
        serverChannel, resourceProvider, new MockPackageMapProvider());
    server.defaultSdk = new MockSdk();
    handler = new AnalysisDomainHandler(server);
    // listen for notifications
    Stream<Notification> notificationStream = serverChannel.notificationController.stream;
    notificationStream.listen((Notification notification) {
      if (notification.event == ANALYSIS_ERRORS) {
        String file = notification.getParameter(FILE);
        List<Map<String, Object>> errorMaps = notification.getParameter(ERRORS);
        filesErrors[file] = errorMaps.map(jsonToAnalysisError).toList();
      }
      if (notification.event == ANALYSIS_HIGHLIGHTS) {
        String file = notification.getParameter(FILE);
        filesHighlights[file] = notification.getParameter(REGIONS);
      }
      if (notification.event == ANALYSIS_NAVIGATION) {
        String file = notification.getParameter(FILE);
        filesNavigation[file] = notification.getParameter(REGIONS);
      }
    });
  }

  void addAnalysisSubscriptionHighlights(String file) {
    addAnalysisSubscription(AnalysisService.HIGHLIGHTS, file);
  }

  void addAnalysisSubscriptionNavigation(String file) {
    addAnalysisSubscription(AnalysisService.NAVIGATION, file);
  }

  void addAnalysisSubscription(AnalysisService service, String file) {
    // add file to subscription
    var files = analysisSubscriptions[service.name];
    if (files == null) {
      files = <String>[];
      analysisSubscriptions[service.name] = files;
    }
    files.add(file);
    // set subscriptions
    Request request = new Request('0', ANALYSIS_SET_SUBSCRIPTIONS);
    request.setParameter(SUBSCRIPTIONS, analysisSubscriptions);
    handleSuccessfulRequest(request);
  }

  /**
   * Returns a [Future] that completes when this this helper finished all its
   * scheduled tasks.
   */
  Future waitForOperationsFinished() {
    return waitForServerOperationsPerformed(server);
  }

  /**
   * Returns the offset of [search] in [testCode].
   * Fails if not found.
   */
  int findOffset(String search) {
    int offset = testCode.indexOf(search);
    expect(offset, isNot(-1));
    return offset;
  }

  /**
   * Returns [AnalysisError]s recorded for the given [file].
   * May be empty, but not `null`.
   */
  List<AnalysisError> getErrors(String file) {
    List<AnalysisError> errors = filesErrors[file];
    if (errors != null) {
      return errors;
    }
    return <AnalysisError>[];
  }

  /**
   * Returns highlights recorded for the given [file].
   * May be empty, but not `null`.
   */
  List<Map<String, Object>> getHighlights(String file) {
    List<Map<String, Object>> highlights = filesHighlights[file];
    if (highlights != null) {
      return highlights;
    }
    return [];
  }

  /**
   * Returns navigation regions recorded for the given [file].
   * May be empty, but not `null`.
   */
  List<Map<String, Object>> getNavigation(String file) {
    List<Map<String, Object>> navigation = filesNavigation[file];
    if (navigation != null) {
      return navigation;
    }
    return [];
  }

  /**
   * Returns [AnalysisError]s recorded for the [testFile].
   * May be empty, but not `null`.
   */
  List<AnalysisError> getTestErrors() {
    return getErrors(testFile);
  }

  /**
   * Returns highlights recorded for the given [testFile].
   * May be empty, but not `null`.
   */
  List<Map<String, Object>> getTestHighlights() {
    return getHighlights(testFile);
  }

  /**
   * Returns navigation information recorded for the given [testFile].
   * May be empty, but not `null`.
   */
  List<Map<String, Object>> getTestNavigation() {
    return getNavigation(testFile);
  }

  /**
   * Creates an empty project `/project`.
   */
  void createEmptyProject() {
    resourceProvider.newFolder('/project');
    Request request = new Request('0', ANALYSIS_SET_ANALYSIS_ROOTS);
    request.setParameter(INCLUDED, ['/project']);
    request.setParameter(EXCLUDED, []);
    handleSuccessfulRequest(request);
  }

  /**
   * Creates a project with a single Dart file `/project/bin/test.dart` with
   * the given [code].
   */
  void createSingleFileProject(code) {
    this.testCode = _getCodeString(code);
    resourceProvider.newFolder('/project');
    resourceProvider.newFile(testFile, testCode);
    Request request = new Request('0', ANALYSIS_SET_ANALYSIS_ROOTS);
    request.setParameter(INCLUDED, ['/project']);
    request.setParameter(EXCLUDED, []);
    handleSuccessfulRequest(request);
  }

  String setFileContent(String path, String content) {
    resourceProvider.newFile(path, content);
    return path;
  }

  /**
   * Validates that the given [request] is handled successfully.
   */
  void handleSuccessfulRequest(Request request) {
    Response response = handler.handleRequest(request);
    expect(response, isResponseSuccess('0'));
  }

  /**
   * Stops the associated server.
   */
  void stopServer() {
    server.done();
  }

  /**
   * Send an `updateContent` request for [testFile].
   */
  void sendContentChange(Map contentChange) {
    Request request = new Request('0', ANALYSIS_UPDATE_CONTENT);
    request.setParameter('files', {
      testFile: contentChange
    });
    handleSuccessfulRequest(request);
  }

  static String _getCodeString(code) {
    if (code is List<String>) {
      code = code.join('\n');
    }
    return code as String;
  }
}


testNotificationErrors() {
  AnalysisTestHelper helper;

  setUp(() {
    helper = new AnalysisTestHelper();
  });

  test('ParserErrorCode', () {
    helper.createSingleFileProject('library lib');
    return helper.waitForOperationsFinished().then((_) {
      List<AnalysisError> errors = helper.getTestErrors();
      expect(errors, hasLength(1));
      AnalysisError error = errors[0];
      expect(error.file, '/project/bin/test.dart');
      expect(error.errorCode, 'ParserErrorCode.EXPECTED_TOKEN');
      expect(error.offset, isPositive);
      expect(error.length, isNonNegative);
      expect(error.message, isNotNull);
    });
  });

  test('StaticWarningCode', () {
    helper.createSingleFileProject([
      'main() {',
      '  print(unknown);',
      '}']);
    return helper.waitForOperationsFinished().then((_) {
      List<AnalysisError> errors = helper.getTestErrors();
      expect(errors, hasLength(1));
      AnalysisError error = errors[0];
      expect(error.errorCode, 'StaticWarningCode.UNDEFINED_IDENTIFIER');
    });
  });
}


testUpdateContent() {
  test('full content', () {
    AnalysisTestHelper helper = new AnalysisTestHelper();
    helper.createSingleFileProject('// empty');
    return helper.waitForOperationsFinished().then((_) {
      // no errors initially
      List<AnalysisError> errors = helper.getTestErrors();
      expect(errors, isEmpty);
      // update code
      helper.sendContentChange({
        CONTENT: 'library lib'
      });
      // wait, there is an error
      return helper.waitForOperationsFinished().then((_) {
        List<AnalysisError> errors = helper.getTestErrors();
        expect(errors, hasLength(1));
      });
    });
  });

  test('incremental', () {
    AnalysisTestHelper helper = new AnalysisTestHelper();
    helper.createSingleFileProject('library A;');
    return helper.waitForOperationsFinished().then((_) {
      // no errors initially
      List<AnalysisError> errors = helper.getTestErrors();
      expect(errors, isEmpty);
      // update code
      helper.sendContentChange({
        CONTENT: 'library lib',
        OFFSET: 'library '.length,
        OLD_LENGTH: 'A;'.length,
        NEW_LENGTH: 'lib'.length,
      });
      // wait, there is an error
      return helper.waitForOperationsFinished().then((_) {
        List<AnalysisError> errors = helper.getTestErrors();
        expect(errors, hasLength(1));
      });
    });
  });

  test('change on disk, normal', () {
    AnalysisTestHelper helper = new AnalysisTestHelper();
    helper.createSingleFileProject('library A;');
    return helper.waitForOperationsFinished().then((_) {
      // There should be no errors
      expect(helper.getTestErrors(), hasLength(0));
      // Change file on disk, adding a syntax error.
      helper.resourceProvider.modifyFile(helper.testFile, 'library lib');
      // There should be errors now.
      return pumpEventQueue().then((_) {
        return helper.waitForOperationsFinished().then((_) {
          expect(helper.getTestErrors(), hasLength(1));
        });
      });
    });
  });

  test('change on disk, during override', () {
    AnalysisTestHelper helper = new AnalysisTestHelper();
    helper.createSingleFileProject('library A;');
    return helper.waitForOperationsFinished().then((_) {
      // update code
      helper.sendContentChange({
        CONTENT: 'library B;'
      });
      // There should be no errors
      return helper.waitForOperationsFinished().then((_) {
        expect(helper.getTestErrors(), hasLength(0));
        // Change file on disk, adding a syntax error.
        helper.resourceProvider.modifyFile(helper.testFile, 'library lib');
        // There should still be no errors (file should not have been reread).
        return helper.waitForOperationsFinished().then((_) {
          expect(helper.getTestErrors(), hasLength(0));
          // Send a content change with a null content param--file should be
          // reread from disk.
          helper.sendContentChange({
            CONTENT: null
          });
          // There should be errors now.
          return helper.waitForOperationsFinished().then((_) {
            expect(helper.getTestErrors(), hasLength(1));
          });
        });
      });
    });
  });
}

void test_setSubscriptions() {
  test('before analysis', () {
    AnalysisTestHelper helper = new AnalysisTestHelper();
    // subscribe
    helper.addAnalysisSubscriptionHighlights(helper.testFile);
    // create project
    helper.createSingleFileProject('int V = 42;');
    // wait, there are highlight regions
    helper.waitForOperationsFinished().then((_) {
      var highlights = helper.getHighlights(helper.testFile);
      expect(highlights, isNot(isEmpty));
    });
  });

  test('after analysis', () {
    AnalysisTestHelper helper = new AnalysisTestHelper();
    // create project
    helper.createSingleFileProject('int V = 42;');
    // wait, no regions initially
    return helper.waitForOperationsFinished().then((_) {
      var highlights = helper.getHighlights(helper.testFile);
      expect(highlights, isEmpty);
      // subscribe
      helper.addAnalysisSubscriptionHighlights(helper.testFile);
      // wait, has regions
      return helper.waitForOperationsFinished().then((_) {
        var highlights = helper.getHighlights(helper.testFile);
        expect(highlights, isNot(isEmpty));
      });
    });
  });
}


@ReflectiveTestCase()
class AnalysisDomainTest extends AbstractAnalysisTest {
  Map<String, List<AnalysisError>> filesErrors = {};

  void processNotification(Notification notification) {
    if (notification.event == ANALYSIS_ERRORS) {
      String file = notification.getParameter(FILE);
      List<Map<String, Object>> errorMaps = notification.getParameter(ERRORS);
      filesErrors[file] = errorMaps.map(jsonToAnalysisError).toList();
    }
  }

  test_setRoots_packages() {
    // prepare package
    String pkgFile = '/packages/pkgA/libA.dart';
    resourceProvider.newFile(pkgFile, '''
library lib_a;
class A {}
''');
    packageMapProvider.packageMap['pkgA'] =
        [resourceProvider.getResource('/packages/pkgA')];
    addTestFile('''
import 'package:pkgA/libA.dart';
main(A a) {
}
''');
    // create project and wait for analysis
    createProject();
    return waitForTasksFinished().then((_) {
      // if 'package:pkgA/libA.dart' was resolved, then there are no errors
      expect(filesErrors[testFile], isEmpty);
      // packages file also was resolved
      expect(filesErrors[pkgFile], isEmpty);
    });
  }
}
