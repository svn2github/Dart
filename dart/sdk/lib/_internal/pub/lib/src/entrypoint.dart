// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pub.entrypoint;

import 'dart:async' hide TimeoutException;

import 'package:path/path.dart' as path;

import 'git.dart' as git;
import 'io.dart';
import 'lock_file.dart';
import 'log.dart' as log;
import 'package.dart';
import 'package_graph.dart';
import 'system_cache.dart';
import 'utils.dart';
import 'solver/version_solver.dart';

/// Pub operates over a directed graph of dependencies that starts at a root
/// "entrypoint" package. This is typically the package where the current
/// working directory is located. An entrypoint knows the [root] package it is
/// associated with and is responsible for managing the "packages" directory
/// for it.
///
/// That directory contains symlinks to all packages used by an app. These links
/// point either to the [SystemCache] or to some other location on the local
/// filesystem.
///
/// While entrypoints are typically applications, a pure library package may end
/// up being used as an entrypoint. Also, a single package may be used as an
/// entrypoint in one context but not in another. For example, a package that
/// contains a reusable library may not be the entrypoint when used by an app,
/// but may be the entrypoint when you're running its tests.
class Entrypoint {
  /// The root package this entrypoint is associated with.
  final Package root;

  /// The system-wide cache which caches packages that need to be fetched over
  /// the network.
  final SystemCache cache;

  /// A map of the [Future]s that were or are being used to asynchronously get
  /// packages.
  ///
  /// Includes packages that are in-transit and ones that have already
  /// completed.
  final _pendingGets = new Map<PackageId, Future<PackageId>>();

  /// Loads the entrypoint from a package at [rootDir].
  Entrypoint(String rootDir, SystemCache cache)
      : root = new Package.load(null, rootDir, cache.sources),
        cache = cache;

  // TODO(rnystrom): Make this path configurable.
  /// The path to the entrypoint's "packages" directory.
  String get packagesDir => path.join(root.dir, 'packages');

  /// `true` if the entrypoint package currently has a lock file.
  bool get lockFileExists => entryExists(lockFilePath);

  /// The path to the entrypoint package's lockfile.
  String get lockFilePath => path.join(root.dir, 'pubspec.lock');

  /// Gets package [id] and makes it available for use by this entrypoint.
  ///
  /// If this completes successfully, the package is guaranteed to be importable
  /// using the `package:` scheme. Returns the resolved [PackageId].
  ///
  /// This automatically downloads the package to the system-wide cache as well
  /// if it requires network access to retrieve (specifically, if the package's
  /// source has [shouldCache] as `true`).
  ///
  /// See also [getDependencies].
  Future<PackageId> get(PackageId id) {
    var pending = _pendingGets[id];
    if (pending != null) return pending;

    var packageDir = path.join(packagesDir, id.name);
    var source;

    var future = new Future.sync(() {
      ensureDir(path.dirname(packageDir));

      if (entryExists(packageDir)) {
        // TODO(nweiz): figure out when to actually delete the directory, and
        // when we can just re-use the existing symlink.
        log.fine("Deleting package directory for ${id.name} before get.");
        deleteEntry(packageDir);
      }

      source = cache.sources[id.source];

      if (source.shouldCache) {
        return cache.download(id).then(
            (pkg) => createPackageSymlink(id.name, pkg.dir, packageDir));
      } else {
        return source.get(id, packageDir).then((found) {
          if (found) return null;
          fail('Package ${id.name} not found in source "${id.source}".');
        });
      }
    }).then((_) => source.resolveId(id));

    _pendingGets[id] = future;

    return future;
  }

  /// Gets all dependencies of the [root] package, respecting the [LockFile]
  /// if present.
  ///
  /// Returns a [Future] that completes when all dependencies are available.
  Future getDependencies() {
    return new Future.sync(() {
      return resolveVersions(cache.sources, root, lockFile: loadLockFile());
    }).then(_getDependencies);
  }

  /// Gets the latest available versions of all dependencies of the [root]
  /// package, writing a new [LockFile].
  ///
  /// Returns a [Future] that completes when all dependencies are available.
  Future upgradeAllDependencies() {
    return resolveVersions(cache.sources, root).then(_getDependencies);
  }

  /// Gets the latest available versions of [dependencies], while leaving
  /// other dependencies as specified by the [LockFile] if possible.
  ///
  /// Returns a [Future] that completes when all dependencies are available.
  Future upgradeDependencies(List<String> dependencies) {
    return new Future.sync(() {
      return resolveVersions(cache.sources, root,
          lockFile: loadLockFile(), useLatest: dependencies);
    }).then(_getDependencies);
  }

  /// Removes the old packages directory, gets all dependencies listed in
  /// [result], and writes a [LockFile].
  Future _getDependencies(SolveResult result) {
    return new Future.sync(() {
      if (!result.succeeded) throw result.error;

      // Warn the user if any overrides were in effect.
      if (result.overrides.isNotEmpty) {
        var buffer = new StringBuffer();
        buffer.write("Warning: You are overriding these dependencies:");
        for (var override in result.overrides) {
          buffer.write("\n- $override");
        }
        log.warning(buffer);
      }

      cleanDir(packagesDir);
      return Future.wait(result.packages.map((id) {
        if (id.isRoot) return new Future.value(id);
        return get(id);
      }).toList());
    }).then((ids) {
      _saveLockFile(ids);
      _linkSelf();
      _linkSecondaryPackageDirs();
    });
  }

  /// Loads the list of concrete package versions from the `pubspec.lock`, if it
  /// exists. If it doesn't, this completes to an empty [LockFile].
  LockFile loadLockFile() {
    if (!lockFileExists) return new LockFile.empty();
    return new LockFile.load(lockFilePath, cache.sources);
  }

  /// Determines whether or not the lockfile is out of date with respect to the
  /// pubspec.
  ///
  /// This will be `false` if there is no lockfile at all, or if the pubspec
  /// contains dependencies that are not in the lockfile or that don't match
  /// what's in there.
  bool isLockFileUpToDate() {
    var lockFile = loadLockFile();

    checkDependency(package) {
      var locked = lockFile.packages[package.name];
      if (locked == null) return false;

      if (package.source != locked.source) return false;
      if (!package.constraint.allows(locked.version)) return false;

      var source = cache.sources[package.source];
      if (!source.descriptionsEqual(package.description, locked.description)) {
        return false;
      }

      return true;
    }

    if (!root.dependencies.every(checkDependency)) return false;
    if (!root.devDependencies.every(checkDependency)) return false;

    return true;
  }

  /// Gets dependencies if the lockfile is out of date with respect to the
  /// pubspec.
  Future ensureLockFileIsUpToDate() {
    return new Future.sync(() {
      if (isLockFileUpToDate()) return null;

      if (lockFileExists) {
        log.message(
            "Your pubspec has changed, so we need to update your lockfile:");
      } else {
        log.message(
            "You don't have a lockfile, so we need to generate that:");
      }

      return getDependencies().then((_) {
        log.message("Got dependencies!");
      });
    });
  }

  /// Loads the package graph for the application and all of its transitive
  /// dependencies.
  Future<PackageGraph> loadPackageGraph() {
    var lockFile = loadLockFile();
    return Future.wait(lockFile.packages.values.map((id) {
      var source = cache.sources[id.source];
      return source.getDirectory(id)
          .then((dir) => new Package.load(id.name, dir, cache.sources));
    })).then((packages) {
      var packageMap = <String, Package>{};
      for (var package in packages) {
        packageMap[package.name] = package;
      }
      packageMap[root.name] = root;
      return new PackageGraph(this, lockFile, packageMap);
    });
  }

  /// Saves a list of concrete package versions to the `pubspec.lock` file.
  void _saveLockFile(List<PackageId> packageIds) {
    var lockFile = new LockFile.empty();
    for (var id in packageIds) {
      if (!id.isRoot) lockFile.packages[id.name] = id;
    }

    var lockFilePath = path.join(root.dir, 'pubspec.lock');
    writeTextFile(lockFilePath, lockFile.serialize(root.dir, cache.sources));
  }

  /// Creates a self-referential symlink in the `packages` directory that allows
  /// a package to import its own files using `package:`.
  void _linkSelf() {
    var linkPath = path.join(packagesDir, root.name);
    // Create the symlink if it doesn't exist.
    if (entryExists(linkPath)) return;
    ensureDir(packagesDir);
    createPackageSymlink(root.name, root.dir, linkPath,
        isSelfLink: true, relative: true);
  }

  /// Add "packages" directories to the whitelist of directories that may
  /// contain Dart entrypoints.
  void _linkSecondaryPackageDirs() {
    // Only the main "bin" directory gets a "packages" directory, not its
    // subdirectories.
    var binDir = path.join(root.dir, 'bin');
    if (dirExists(binDir)) _linkSecondaryPackageDir(binDir);

    // The others get "packages" directories in subdirectories too.
    for (var dir in ['benchmark', 'example', 'test', 'tool', 'web']) {
      _linkSecondaryPackageDirsRecursively(path.join(root.dir, dir));
    }
 }

  /// Creates a symlink to the `packages` directory in [dir] and all its
  /// subdirectories.
  void _linkSecondaryPackageDirsRecursively(String dir) {
    if (!dirExists(dir)) return;
    _linkSecondaryPackageDir(dir);
    _listDirWithoutPackages(dir)
        .where(dirExists)
        .forEach(_linkSecondaryPackageDir);
  }

  // TODO(nweiz): roll this into [listDir] in io.dart once issue 4775 is fixed.
  /// Recursively lists the contents of [dir], excluding hidden `.DS_Store`
  /// files and `package` files.
  List<String> _listDirWithoutPackages(dir) {
    return flatten(listDir(dir).map((file) {
      if (path.basename(file) == 'packages') return [];
      if (!dirExists(file)) return [];
      var fileAndSubfiles = [file];
      fileAndSubfiles.addAll(_listDirWithoutPackages(file));
      return fileAndSubfiles;
    }));
  }

  /// Creates a symlink to the `packages` directory in [dir]. Will replace one
  /// if already there.
  void _linkSecondaryPackageDir(String dir) {
    var symlink = path.join(dir, 'packages');
    if (entryExists(symlink)) deleteEntry(symlink);
    createSymlink(packagesDir, symlink, relative: true);
  }

  /// The basenames of files that are automatically excluded from archives.
  final _BLACKLISTED_FILES = const ['pubspec.lock'];

  /// The basenames of directories that are automatically excluded from
  /// archives.
  final _BLACKLISTED_DIRS = const ['packages'];

  // TODO(nweiz): unit test this function.
  /// Returns a list of files that are considered to be part of this package.
  ///
  /// If this is a Git repository, this will respect .gitignore; otherwise, it
  /// will return all non-hidden, non-blacklisted files.
  ///
  /// If [beneath] is passed, this will only return files beneath that path.
  Future<List<String>> packageFiles({String beneath}) {
    if (beneath == null) beneath = root.dir;

    return git.isInstalled.then((gitInstalled) {
      if (dirExists(path.join(root.dir, '.git')) && gitInstalled) {
        // Later versions of git do not allow a path for ls-files that appears
        // to be outside of the repo, so make sure we give it a relative path.
        var relativeBeneath = path.relative(beneath, from: root.dir);

        // List all files that aren't gitignored, including those not checked
        // in to Git.
        return git.run(
            ["ls-files", "--cached", "--others", "--exclude-standard",
             relativeBeneath],
            workingDir: root.dir).then((files) {
          // Git always prints files relative to the project root, but we want
          // them relative to the working directory. It also prints forward
          // slashes on Windows which we normalize away for easier testing.
          return files.map((file) => path.normalize(path.join(root.dir, file)));
        });
      }

      return listDir(beneath, recursive: true);
    }).then((files) {
      return files.where((file) {
        // Skip directories and broken symlinks.
        if (!fileExists(file)) return false;

        var relative = path.relative(file, from: beneath);
        if (_BLACKLISTED_FILES.contains(path.basename(relative))) return false;
        return !path.split(relative).any(_BLACKLISTED_DIRS.contains);
      }).toList();
    });
  }
}
