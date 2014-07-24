// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library test.analysis.get_errors;

import 'dart:async';

import 'package:analysis_server/src/computer/error.dart';
import 'package:analysis_server/src/constants.dart';
import 'package:analysis_server/src/domain_analysis.dart';
import 'package:analysis_server/src/protocol.dart';
import 'package:analysis_services/constants.dart';
import 'package:analysis_testing/reflective_tests.dart';
import 'package:analyzer/file_system/file_system.dart';
import 'package:unittest/unittest.dart';

import '../analysis_abstract.dart';


main() {
  group('getErrors', () {
    runReflectiveTests(GetErrorsTest);
  });
}


@ReflectiveTestCase()
class GetErrorsTest extends AbstractAnalysisTest {
  static const String requestId = 'test-getError';

  @override
  void setUp() {
    super.setUp();
    server.handlers = [new AnalysisDomainHandler(server),];
    createProject();
  }

  test_afterAnalysisComplete() {
    addTestFile('''
main() {
  print(42)
}
''');
    return waitForTasksFinished().then((_) {
      return _getErrors(testFile).then((List<AnalysisError> errors) {
        expect(errors, hasLength(1));
      });
    });
  }

  test_fileDoesNotExist() {
    String file = '$projectPath/doesNotExist.dart';
    return _getErrors(file).then((List<AnalysisError> errors) {
      expect(errors, isEmpty);
    });
  }

  test_fileWithoutContext() {
    String file = '/outside.dart';
    addFile(file, '''
main() {
  print(42);
}
''');
    return _getErrors(file).then((List<AnalysisError> errors) {
      expect(errors, isEmpty);
    });
  }

  test_hasErrors() {
    addTestFile('''
main() {
  print(42)
}
''');
    return _getErrors(testFile).then((List<AnalysisError> errors) {
      expect(errors, hasLength(1));
      {
        AnalysisError error = errors[0];
        expect(error.severity, 'ERROR');
        expect(error.type, 'SYNTACTIC_ERROR');
        expect(error.location.file, testFile);
        expect(error.location.startLine, 2);
      }
    });
  }

  test_noErrors() {
    addTestFile('''
main() {
  print(42);
}
''');
    return _getErrors(testFile).then((List<AnalysisError> errors) {
      expect(errors, isEmpty);
    });
  }

  test_removeContextAfterRequest() {
    addTestFile('''
main() {
  print(42)
}
''');
    // handle the request synchronously
    Request request = _createGetErrorsRequest();
    server.handleRequest(request);
    // remove context, causes sending a 'cancelled' error
    {
      Folder projectFolder = resourceProvider.getResource(projectPath);
      server.contextDirectoryManager.removeContext(projectFolder);
    }
    // wait for an error response
    return serverChannel.waitForResponse(request).then((Response response) {
      expect(response.getResult(ERRORS), isEmpty);
      RequestError error = response.error;
      expect(error, isNotNull);
      expect(error.code, -13);
    });
  }

  Request _createGetErrorsRequest() {
    Request request = new Request(requestId, ANALYSIS_GET_ERRORS);
    request.setParameter(FILE, testFile);
    return request;
  }

  Future<List<AnalysisError>> _getErrors(String file) {
    Request request = _createGetErrorsRequest();
    return serverChannel.sendRequest(request).then((Response response) {
      List errorsJsons = response.getResult(ERRORS);
      return errorsJsons.map(AnalysisError.fromJson).toList();
    });
  }
}
