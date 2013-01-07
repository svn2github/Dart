// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library client_test;

import 'dart:io';

import 'package:unittest/unittest.dart';
import 'package:http/http.dart' as http;
import 'package:http/src/utils.dart';
import 'utils.dart';

void main() {
  setUp(startServer);
  tearDown(stopServer);

  test('#send a StreamedRequest', () {
    var client = new http.Client();
    var request = new http.StreamedRequest("POST", serverUrl);
    request.headers[HttpHeaders.CONTENT_TYPE] =
      'application/json; charset=utf-8';

    var future = client.send(request).then((response) {
      expect(response.request, equals(request));
      expect(response.statusCode, equals(200));
      return consumeInputStream(response.stream);
    }).then(expectAsync1((bytes) => new String.fromCharCodes(bytes)));
    future.catchError((_) {}).then((_) => client.close());

    future.then(expectAsync1((content) {
      expect(content, parse(equals({
        'method': 'POST',
        'path': '/',
        'headers': {
          'content-type': ['application/json; charset=utf-8'],
          'transfer-encoding': ['chunked']
        },
        'body': '{"hello": "world"}'
      })));
    }));

    request.stream.writeString('{"hello": "world"}');
    request.stream.close();
  });
}
