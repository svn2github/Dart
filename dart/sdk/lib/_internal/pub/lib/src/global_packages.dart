// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub.global_packages;

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;

import 'io.dart';
import 'lock_file.dart';
import 'log.dart' as log;
import 'package.dart';
import 'system_cache.dart';
import 'solver/version_solver.dart';
import 'source/cached.dart';
import 'utils.dart';
import 'version.dart';

/// Maintains the set of packages that have been globally activated.
///
/// These have been hand-chosen by the user to make their executables in bin/
/// available to the entire system. This lets them access them even when the
/// current working directory is not inside another entrypoint package.
///
/// Only one version of a given package name can be globally activated at a
/// time. Activating a different version of a package will deactivate the
/// previous one.
class GlobalPackages {
  /// The [SystemCache] containing the global packages.
  final SystemCache cache;

  /// The directory where the lockfiles for activated packages are stored.
  String get _directory => path.join(cache.rootDir, "global_packages");

  /// The source that global packages can be activated from.
  // TODO(rnystrom): Allow activating packages from other sources.
  CachedSource get _source => cache.sources["hosted"] as CachedSource;

  /// Creates a new global package registry backed by the given directory on
  /// the user's file system.
  ///
  /// The directory may not physically exist yet. If not, this will create it
  /// when needed.
  GlobalPackages(this.cache);

  /// Finds the latest version of the hosted package with [name] that matches
  /// [constraint] and makes it the active global version.
  Future activate(String name, VersionConstraint constraint) {
    var lockFilePath = path.join(_directory, name + ".lock");

    // See if we already have it activated.
    var lockFile;
    var currentVersion;
    try {
      lockFile = new LockFile.load(lockFilePath, cache.sources);
      currentVersion = lockFile.packages[name].version;

      // Pull the root package out of the lock file so the solver doesn't see
      // it.
      lockFile.packages.remove(name);

      log.message("Package ${log.bold(name)} is already active at "
          "version ${log.bold(currentVersion)}.");
    } on IOException catch (error) {
      // If we couldn't read the lock and version file, it's not activated.
      lockFile = new LockFile.empty();
    }

    var package;
    var id;
    return _selectVersion(name, currentVersion, constraint).then((version) {
      // Make sure it's in the cache.
      id = new PackageId(name, _source.name, version, name);
      return _source.downloadToSystemCache(id);
    }).then((p) {
      package = p;
      // Resolve it and download its dependencies.
      return resolveVersions(cache.sources, package, lockFile: lockFile);
    }).then((result) {
      if (!result.succeeded) throw result.error;
      result.showReport();

      // Make sure all of the dependencies are locally installed.
      return Future.wait(result.packages.map((id) {
        var source = cache.sources[id.source];
        if (source is! CachedSource) return new Future.value();
        return source.downloadToSystemCache(id)
            .then((_) => source.resolveId(id));
      }));
    }).then((ids) {
      var lockFile = new LockFile(ids);

      // Add the root package itself to the lockfile.
      lockFile.packages[name] = id;

      ensureDir(_directory);
      writeTextFile(lockFilePath,
          lockFile.serialize(cache.rootDir, cache.sources));

      log.message("Activated ${log.bold(package.name)} ${package.version}.");
      // TODO(rnystrom): Look in "bin" and display list of binaries that
      // user can run.
    });
  }

  /// Picks the best version of [package] to activate that meets [constraint].
  ///
  /// If [version] is not `null`, this tries to maintain that version if
  /// possible.
  Future<Version> _selectVersion(String package, Version version,
      VersionConstraint constraint) {
    // If we already have a valid active version, just use it.
    if (version != null && constraint.allows(version)) {
      return new Future.value(version);
    }

    // Otherwise, select the best version the matches the constraint.
    return _source.getVersions(package, package).then((versions) {
      versions = versions.where(constraint.allows).toList();

      if (versions.isEmpty) {
        // TODO(rnystrom): Show most recent unmatching version?
        dataError("Package ${log.bold(package)} has no versions that match "
            "$constraint.");
      }

      // Pick the best matching version.
      versions.sort(Version.prioritize);
      return versions.last;
    });
  }
}
