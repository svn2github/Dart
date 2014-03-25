// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub.barback.build_environment;

import 'dart:async';

import 'package:watcher/watcher.dart';

import 'build_environment.dart';
import 'barback_server.dart';

// TODO(rnystrom): Rename to "SourceDirectory" and clean up various doc
// comments that refer to "build directories" to use "source directory".
/// A directory in the entrypoint package whose contents have been made
/// available to barback and that are bound to a server.
class BuildDirectory {
  final BuildEnvironment _environment;

  /// The relative directory path within the package.
  final String directory;

  /// The server bound to this directory.
  BarbackServer get server => _server;
  BarbackServer _server;

  /// The subscription to the [DirectoryWatcher] used to watch this directory
  /// for changes.
  ///
  /// If the directory is not being watched, this will be `null`.
  StreamSubscription<WatchEvent> watchSubscription;

  BuildDirectory(this._environment, this.directory);

  /// Binds a server running on [hostname]:[port] to this directory.
  Future<BarbackServer> serve(String hostname, int port) {
    return BarbackServer.bind(_environment, hostname, port, directory)
        .then((server) => _server = server);
  }

  /// Removes the build directory from the build environment.
  ///
  /// Closes the server, removes the assets from barback, and stops watching it.
  Future close() {
    var futures = [server.close()];

    // Stop watching the directory.
    if (watchSubscription != null) {
      var cancel = watchSubscription.cancel();
      if (cancel != null) futures.add(cancel);
    }

    return Future.wait(futures);
  }
}
