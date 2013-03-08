// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Patch file for the dart:isolate library.

import 'dart:_isolate_helper' show IsolateNatives,
                                   lazyPort,
                                   ReceivePortImpl;

patch class _Isolate {
  patch static ReceivePort get port {
    if (lazyPort == null) {
      lazyPort = new ReceivePort();
    }
    return lazyPort;
  }

  patch static SendPort spawnFunction(void topLevelFunction(),
      [bool UnhandledExceptionCallback(IsolateUnhandledException e)]) {
    // TODO(9012): Don't ignore the UnhandledExceptionCallback.
    return IsolateNatives.spawnFunction(topLevelFunction);
  }

  patch static SendPort spawnUri(String uri) {
    return IsolateNatives.spawn(null, uri, false);
  }
}

/** Default factory for receive ports. */
patch class ReceivePort {
  patch factory ReceivePort() {
    return new ReceivePortImpl();
  }
}
