// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library utils;

import 'dart:io';
import 'dart:json';
import 'dart:uri';

import '../../unittest/lib/unittest.dart';
import '../lib/http.dart' as http;
import '../lib/src/utils.dart';

/// The current server instance.
HttpServer _server;

/// The URL for the current server instance.
Uri get serverUrl => new Uri.fromString('http://localhost:${_server.port}');

/// A dummy URL for constructing requests that won't be sent.
Uri get dummyUrl => new Uri.fromString('http://dartlang.org/');

/// Starts a new HTTP server.
void startServer() {
  _server = new HttpServer();

  _server.addRequestHandler((request) => request.path == '/error',
      (request, response) {
    response.statusCode = 400;
    response.contentLength = 0;
    closeResponse(request, response);
  });

  _server.addRequestHandler((request) => request.path == '/loop',
      (request, response) {
    var n = int.parse(new Uri.fromString(request.uri).query);
    response.statusCode = 302;
    response.headers.set('location',
        serverUrl.resolve('/loop?${n + 1}').toString());
    response.contentLength = 0;
    closeResponse(request, response);
  });

  _server.addRequestHandler((request) => request.path == '/redirect',
      (request, response) {
    response.statusCode = 302;
    response.headers.set('location', serverUrl.resolve('/').toString());
    response.contentLength = 0;
    closeResponse(request, response);
  });

  _server.defaultRequestHandler = (request, response) {
    consumeInputStream(request.inputStream).then((requestBodyBytes) {
      response.statusCode = 200;
      response.headers.contentType = new ContentType("application", "json");

      var requestBody;
      if (requestBodyBytes.isEmpty) {
        requestBody = null;
      } else if (request.headers.contentType.charset != null) {
        var encoding = requiredEncodingForCharset(
            request.headers.contentType.charset);
        requestBody = decodeString(requestBodyBytes, encoding);
      } else {
        requestBody = requestBodyBytes;
      }

      var content = {
        'method': request.method,
        'path': request.path,
        'headers': <String>{}
      };
      if (requestBody != null) content['body'] = requestBody;
      request.headers.forEach((name, values) {
        // These headers are automatically generated by dart:io, so we don't
        // want to test them here.
        if (name == 'cookie' || name == 'host') return;

        content['headers'][name] = values;
      });

      var outputEncoding;
      var encodingName = request.queryParameters['response-encoding'];
      if (encodingName != null) {
        outputEncoding = requiredEncodingForCharset(encodingName);
      } else {
        outputEncoding = Encoding.ASCII;
      }

      var body = JSON.stringify(content);
      response.contentLength = body.length;
      response.outputStream.writeString(body, outputEncoding);
      response.outputStream.close();
    });
  };

  _server.listen("127.0.0.1", 0);
}

/// Stops the current HTTP server.
void stopServer() {
  _server.close();
  _server = null;
}

/// Closes [response] while ignoring the body of [request].
///
/// Due to issue 6984, it's necessary to drain the request body before closing
/// the response.
void closeResponse(HttpRequest request, HttpResponse response) {
  request.inputStream.onData = request.inputStream.read;
  request.inputStream.onClosed = response.outputStream.close;
}

/// A matcher that matches JSON that parses to a value that matches the inner
/// matcher.
Matcher parse(matcher) => new _Parse(matcher);

class _Parse extends BaseMatcher {
  final Matcher _matcher;

  _Parse(this._matcher);

  bool matches(item, MatchState matchState) {
    if (item is! String) return false;

    var parsed;
    try {
      parsed = JSON.parse(item);
    } catch (e) {
      return false;
    }

    return _matcher.matches(parsed, matchState);
  }

  Description describe(Description description) {
    return description.add('parses to a value that ')
      .addDescriptionOf(_matcher);
  }
}

// TODO(nweiz): remove this once it's built in to unittest
/// A matcher for StateErrors.
const isStateError = const _StateError();

/// A matcher for functions that throw StateError.
const Matcher throwsStateError =
    const Throws(isStateError);

class _StateError extends TypeMatcher {
  const _StateError() : super("StateError");
  bool matches(item, MatchState matchState) => item is StateError;
}

/// A matcher for HttpExceptions.
const isHttpException = const _HttpException();

/// A matcher for functions that throw HttpException.
const Matcher throwsHttpException =
    const Throws(isHttpException);

class _HttpException extends TypeMatcher {
  const _HttpException() : super("HttpException");
  bool matches(item, MatchState matchState) => item is HttpException;
}

/// A matcher for RedirectLimitExceededExceptions.
const isRedirectLimitExceededException =
    const _RedirectLimitExceededException();

/// A matcher for functions that throw RedirectLimitExceededException.
const Matcher throwsRedirectLimitExceededException =
    const Throws(isRedirectLimitExceededException);

class _RedirectLimitExceededException extends TypeMatcher {
  const _RedirectLimitExceededException() :
      super("RedirectLimitExceededException");

  bool matches(item, MatchState matchState) =>
    item is RedirectLimitExceededException;
}
