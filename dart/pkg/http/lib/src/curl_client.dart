// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library curl_client;

import 'dart:io';

import 'base_client.dart';
import 'base_request.dart';
import 'streamed_response.dart';
import 'utils.dart';

/// A drop-in replacement for [Client] that uses the `curl` command-line utility
/// rather than [dart:io] to make requests. This class will only exist
/// temporarily until [dart:io] natively supports requests over HTTPS.
class CurlClient extends BaseClient {
  /// The path to the `curl` executable to run. By default, this will look up
  /// `curl` on the system path.
  final String executable;

  /// Creates a new [CurlClient] with [executable] as the path to the `curl`
  /// executable. By default, this will look up `curl` on the system path.
  CurlClient([String executable])
    : executable = executable == null ? "curl" : executable;

  /// Sends a request via `curl` and returns the response.
  Future<StreamedResponse> send(BaseRequest request) {
    var requestStream = request.finalize();
    return withTempDir((tempDir) {
      var headerFile = new Path(tempDir).append("curl-headers").toNativePath();
      var arguments = _argumentsForRequest(request, headerFile);
      var process;
      return Process.start("curl", arguments).chain((process_) {
        process = process_;
        if (requestStream.closed) {
          process.stdin.close();
        } else {
          requestStream.pipe(process.stdin);
        }

        return _waitForHeaders(process, expectBody: request.method != "HEAD");
      }).chain((_) => new File(headerFile).readAsLines())
        .transform((lines) => _buildResponse(process, lines));
    });
  }

  /// Returns the list of arguments to `curl` necessary for performing
  /// [request]. [headerFile] is the path to the file where the response headers
  /// should be stored.
  List<String> _argumentsForRequest(BaseRequest request, String headerFile) {
    var arguments = ["--dump-header", headerFile];
    if (request.method == 'HEAD') {
      arguments.add("--head");
    } else {
      arguments.add("--request");
      arguments.add(request.method);
    }
    if (request.followRedirects) {
      arguments.add("--location");
      arguments.add("--max-redirs");
      arguments.add(request.maxRedirects.toString());
    }
    if (request.contentLength != 0)  {
      arguments.add("--data-binary");
      arguments.add("@-");
    }

    // Override the headers automatically added by curl. We want to make it
    // behave as much like the dart:io client as possible.
    var headers = {
      'accept': '',
      'user-agent': ''
    };
    mapAddAll(headers, request.headers);
    if (request.contentLength < 0) {
      headers['content-length'] = '';
      headers['transfer-encoding'] = 'chunked';
    } else if (request.contentLength > 0) {
      headers['content-length'] = request.contentLength.toString();
    }

    headers.forEach((name, value) {
      arguments.add("--header");
      arguments.add("$name: $value");
    });
    arguments.add(request.url.toString());

    return arguments;
  }

  /// Returns a [Future] that completes once the `curl` [process] has finished
  /// receiving the response headers. [expectBody] indicates that the server is
  /// expected to send a response body (which is not the case for HEAD
  /// requests).
  Future _waitForHeaders(Process process, {bool expectBody}) {
    var exitCompleter = new Completer<int>();
    var exitFuture = exitCompleter.future;
    process.onExit = (exitCode) {
      if (exitCode == 0) {
        exitCompleter.complete(0);
        return;
      }

      chainToCompleter(consumeInputStream(process.stderr)
            .transform((stderrBytes) {
        var message = new String.fromCharCodes(stderrBytes);
        if (exitCode == 47) {
          throw new RedirectLimitExceededException(message);
        } else {
          throw new HttpException(message);
        }
      }), exitCompleter);
    };

    // If there's not going to be a response body (e.g. for HEAD requests), curl
    // prints the headers to stdout instead of the body. We want to wait until
    // all the headers are received to read them from the header file.
    if (!expectBody) {
      return Futures.wait([
        consumeInputStream(process.stdout),
        exitFuture
      ]);
    }

    var completer = new Completer();
    resetCallbacks() {
      process.stdout.onData = null;
      process.stdout.onError = null;
      process.stdout.onClosed = null;
    }
    process.stdout.onData = () {
      // TODO(nweiz): If an error happens after the body data starts being
      // received, it should be piped through Response.stream once issue
      // 3657 is fixed.
      exitFuture.handleException((e) => true);
      resetCallbacks();
      completer.complete(null);
    };
    process.stdout.onError = (e) {
      resetCallbacks();
      completer.completeException(e);
    };
    process.stdout.onClosed = () {
      resetCallbacks();
      chainToCompleter(exitFuture, completer);
    };
    return completer.future;
  }

  /// Returns a [StreamedResponse] from the response data printed by the `curl`
  /// [process]. [lines] are the headers that `curl` wrote to a file.
  StreamedResponse _buildResponse(Process process, List<String> lines) {
    // When curl follows redirects, it prints the redirect headers as well as
    // the headers of the final request. Each block is separated by a blank
    // line. We just care about the last block. There is one trailing empty
    // line, though, which we don't want to consider a separator.
    var lastBlank = lines.lastIndexOf("", lines.length - 2);
    if (lastBlank != -1) lines.removeRange(0, lastBlank + 1);

    var statusParts = lines.removeAt(0).split(" ");
    var status = int.parse(statusParts[1]);
    var isRedirect = status >= 300 && status < 400;
    var reasonPhrase =
        Strings.join(" ", statusParts.getRange(2, statusParts.length - 2));
    var headers = <String>{};
    for (var line in lines) {
      if (line.isEmpty) continue;
      var split = split1(line, ":");
      headers[split[0].toLowerCase()] = split[1].trim();
    }
    var responseStream = process.stdout;
    if (responseStream.closed) {
      responseStream = new ListInputStream();
      responseStream.markEndOfStream();
    }
    var contentLength = -1;
    if (headers.containsKey('content-length')) {
      contentLength = int.parse(headers['content-length']);
    }

    return new StreamedResponse(responseStream, status, contentLength,
        headers: headers,
        isRedirect: isRedirect,
        reasonPhrase: reasonPhrase);
  }
}
