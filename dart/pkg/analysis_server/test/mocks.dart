// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library mocks;

import 'dart:async';
import 'dart:io';

import 'package:analysis_server/src/channel.dart';
import 'package:analysis_server/src/protocol.dart';

/**
 * A mock [WebSocket] for testing.
 */
class MockSocket<T> implements WebSocket {

  StreamController controller = new StreamController();
  MockSocket twin;
  Stream stream;

  factory MockSocket.pair() {
    MockSocket socket1 = new MockSocket();
    MockSocket socket2 = new MockSocket();
    socket1.twin = socket2;
    socket2.twin = socket1;
    socket1.stream = socket2.controller.stream;
    socket2.stream = socket1.controller.stream;
    return socket1;
  }

  MockSocket();

  void add(T text) => controller.add(text);

  void allowMultipleListeners() {
    stream = stream.asBroadcastStream();
  }

  Future close([int code, String reason]) => controller.close()
      .then((_) => twin.controller.close());

  StreamSubscription<T> listen(void onData(T event),
                     { Function onError, void onDone(), bool cancelOnError}) =>
    stream.listen(onData, onError: onError, onDone: onDone,
        cancelOnError: cancelOnError);

  Stream<T> where(bool test(T)) => stream.where(test);

  noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

/**
 * A mock [ServerCommunicationChannel] channel that does nothing.
 */
class MockServerChannel implements ServerCommunicationChannel {
  @override
  void listen(void onRequest(Request request), {void onError(), void onDone()}) {
  }

  @override
  void sendNotification(Notification notification) {
  }

  @override
  void sendResponse(Response response) {
  }
}
