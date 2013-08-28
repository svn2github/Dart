// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:scheduled_test/scheduled_test.dart';
import 'package:scheduled_test/scheduled_server.dart';

import '../../lib/src/io.dart';
import '../descriptor.dart' as d;
import '../test_pub.dart';

main() {
  initConfig();
  integration('with an expired credentials.json, refreshes and saves the '
      'refreshed access token to credentials.json', () {
    d.validPackage.create();

    var server = new ScheduledServer();
    d.credentialsFile(server, 'access token',
        refreshToken: 'refresh token',
        expiration: new DateTime.now().subtract(new Duration(hours: 1)))
        .create();

    var pub = startPublish(server);
    confirmPublish(pub);

    server.handle('POST', '/token', (request) {
      return new ByteStream(request).toBytes().then((bytes) {
        var body = new String.fromCharCodes(bytes);
        expect(body, matches(
            new RegExp(r'(^|&)refresh_token=refresh\+token(&|$)')));

        request.response.headers.contentType =
            new ContentType("application", "json");
        request.response.write(JSON.encode({
          "access_token": "new access token",
          "token_type": "bearer"
        }));
        request.response.close();
      });
    });

    server.handle('GET', '/api/packages/versions/new', (request) {
      expect(request.headers.value('authorization'),
          equals('Bearer new access token'));

      request.response.close();
    });

    pub.shouldExit();

    d.credentialsFile(server, 'new access token', refreshToken: 'refresh token')
        .validate();
  });
}