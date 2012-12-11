// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library entrypoint;

import 'io.dart';
import 'lock_file.dart';
import 'log.dart' as log;
import 'package.dart';
import 'root_source.dart';
import 'system_cache.dart';
import 'utils.dart';
import 'version.dart';
import 'version_solver.dart';

/**
 * Pub operates over a directed graph of dependencies that starts at a root
 * "entrypoint" package. This is typically the package where the current
 * working directory is located. An entrypoint knows the [root] package it is
 * associated with and is responsible for managing the "packages" directory
 * for it.
 *
 * That directory contains symlinks to all packages used by an app. These links
 * point either to the [SystemCache] or to some other location on the local
 * filesystem.
 *
 * While entrypoints are typically applications, a pure library package may end
 * up being used as an entrypoint. Also, a single package may be used as an
 * entrypoint in one context but not in another. For example, a package that
 * contains a reusable library may not be the entrypoint when used by an app,
 * but may be the entrypoint when you're running its tests.
 */
class Entrypoint {
  /**
   * The root package this entrypoint is associated with.
   */
  final Package root;

  /**
   * The system-wide cache which caches packages that need to be fetched over
   * the network.
   */
  final SystemCache cache;

  /**
   * Packages which are either currently being asynchronously installed to the
   * directory, or have already been installed.
   */
  final Map<PackageId, Future<PackageId>> _installs;

  Entrypoint(this.root, this.cache)
  : _installs = new Map<PackageId, Future<PackageId>>();

  /// Loads the entrypoint from a package at [rootDir].
  static Future<Entrypoint> load(String rootDir, SystemCache cache) {
    return Package.load(null, rootDir, cache.sources).transform((package) =>
        new Entrypoint(package, cache));
  }

  /**
   * The path to this "packages" directory.
   */
  // TODO(rnystrom): Make this path configurable.
  String get path => join(root.dir, 'packages');

  /**
   * Ensures that the package identified by [id] is installed to the directory.
   * Returns the resolved [PackageId].
   *
   * If this completes successfully, the package is guaranteed to be importable
   * using the `package:` scheme.
   *
   * This will automatically install the package to the system-wide cache as
   * well if it requires network access to retrieve (specifically, if
   * `id.source.shouldCache` is true).
   *
   * See also [installDependencies].
   */
  Future<PackageId> install(PackageId id) {
    var pendingOrCompleted = _installs[id];
    if (pendingOrCompleted != null) return pendingOrCompleted;

    var packageDir = join(path, id.name);
    var future = ensureDir(dirname(packageDir)).chain((_) {
      return exists(packageDir);
    }).chain((exists) {
      if (!exists) return new Future.immediate(null);
      // TODO(nweiz): figure out when to actually delete the directory, and when
      // we can just re-use the existing symlink.
      log.fine("Deleting package directory for ${id.name} before install.");
      return deleteDir(packageDir);
    }).chain((_) {
      if (id.source.shouldCache) {
        return cache.install(id).chain(
            (pkg) => createPackageSymlink(id.name, pkg.dir, packageDir));
      } else {
        return id.source.install(id, packageDir).transform((found) {
          if (found) return null;
          // TODO(nweiz): More robust error-handling.
          throw 'Package ${id.name} not found in source "${id.source.name}".';
        });
      }
    }).chain((_) => id.resolved);

    _installs[id] = future;

    return future;
  }

  /**
   * Installs all dependencies of the [root] package to its "packages"
   * directory, respecting the [LockFile] if present. Returns a [Future] that
   * completes when all dependencies are installed.
   */
  Future installDependencies() {
    return _loadLockFile()
      .chain((lockFile) => resolveVersions(cache.sources, root, lockFile))
      .chain(_installDependencies);
  }

  /**
   * Installs the latest available versions of all dependencies of the [root]
   * package to its "package" directory, writing a new [LockFile]. Returns a
   * [Future] that completes when all dependencies are installed.
   */
  Future updateAllDependencies() {
    return resolveVersions(cache.sources, root, new LockFile.empty())
      .chain(_installDependencies);
  }

  /**
   * Installs the latest available versions of [dependencies], while leaving
   * other dependencies as specified by the [LockFile] if possible. Returns a
   * [Future] that completes when all dependencies are installed.
   */
  Future updateDependencies(List<String> dependencies) {
    return _loadLockFile().chain((lockFile) {
      var versionSolver = new VersionSolver(cache.sources, root, lockFile);
      for (var dependency in dependencies) {
        versionSolver.useLatestVersion(dependency);
      }
      return versionSolver.solve();
    }).chain(_installDependencies);
  }

  /**
   * Removes the old packages directory, installs all dependencies listed in
   * [packageVersions], and writes a [LockFile].
   */
  Future _installDependencies(List<PackageId> packageVersions) {
    return cleanDir(path).chain((_) {
      return Futures.wait(packageVersions.map((id) {
        if (id.source is RootSource) return new Future.immediate(id);
        return install(id);
      }));
    }).chain(_saveLockFile)
      .chain(_installSelfReference)
      .chain(_linkSecondaryPackageDirs);
  }

  /**
   * Loads the list of concrete package versions from the `pubspec.lock`, if it
   * exists. If it doesn't, this completes to an empty [LockFile].
   *
   * If there's an error reading the `pubspec.lock` file, this will print a
   * warning message and act as though the file doesn't exist.
   */
  Future<LockFile> _loadLockFile() {
    var lockFilePath = join(root.dir, 'pubspec.lock');

    log.fine("Loading lockfile.");
    return fileExists(lockFilePath).chain((exists) {
      if (!exists) {
        log.fine("No lock file at $lockFilePath, creating empty one.");
        return new Future<LockFile>.immediate(new LockFile.empty());
      }

      return readTextFile(lockFilePath).transform((text) =>
          new LockFile.parse(text, cache.sources));
    });
  }

  /**
   * Saves a list of concrete package versions to the `pubspec.lock` file.
   */
  Future _saveLockFile(List<PackageId> packageIds) {
    var lockFile = new LockFile.empty();
    for (var id in packageIds) {
      if (id.source is! RootSource) lockFile.packages[id.name] = id;
    }

    var lockFilePath = join(root.dir, 'pubspec.lock');
    log.fine("Saving lockfile.");
    return writeTextFile(lockFilePath, lockFile.serialize());
  }

  /**
   * Installs a self-referential symlink in the `packages` directory that will
   * allow a package to import its own files using `package:`.
   */
  Future _installSelfReference(_) {
    var linkPath = join(path, root.name);
    return exists(linkPath).chain((exists) {
      // Create the symlink if it doesn't exist.
      if (exists) return new Future.immediate(null);
      return ensureDir(path).chain(
          (_) => createPackageSymlink(root.name, root.dir, linkPath,
              isSelfLink: true));
    });
  }

  /**
   * If `bin/`, `test/`, or `example/` directories exist, symlink `packages/`
   * into them so that their entrypoints can be run. Do the same for any
   * subdirectories of `test/` and `example/`.
   */
  Future _linkSecondaryPackageDirs(_) {
    var binDir = join(root.dir, 'bin');
    var exampleDir = join(root.dir, 'example');
    var testDir = join(root.dir, 'test');
    var toolDir = join(root.dir, 'tool');
    var webDir = join(root.dir, 'web');
    return dirExists(binDir).chain((exists) {
      if (!exists) return new Future.immediate(null);
      return _linkSecondaryPackageDir(binDir);
    }).chain((_) => _linkSecondaryPackageDirsRecursively(exampleDir))
      .chain((_) => _linkSecondaryPackageDirsRecursively(testDir))
      .chain((_) => _linkSecondaryPackageDirsRecursively(toolDir))
      .chain((_) => _linkSecondaryPackageDirsRecursively(webDir));
  }

  /**
   * Creates a symlink to the `packages` directory in [dir] and all its
   * subdirectories.
   */
  Future _linkSecondaryPackageDirsRecursively(String dir) {
    return dirExists(dir).chain((exists) {
      if (!exists) return new Future.immediate(null);
      return _linkSecondaryPackageDir(dir)
        .chain((_) => _listDirWithoutPackages(dir))
        .chain((files) {
        return Futures.wait(files.map((file) {
          return dirExists(file).chain((isDir) {
            if (!isDir) return new Future.immediate(null);
            return _linkSecondaryPackageDir(file);
          });
        }));
      });
    });
  }

  // TODO(nweiz): roll this into [listDir] in io.dart once issue 4775 is fixed.
  /**
   * Recursively lists the contents of [dir], excluding hidden `.DS_Store` files
   * and `package` files.
   */
  Future<List<String>> _listDirWithoutPackages(dir) {
    return listDir(dir).chain((files) {
      return Futures.wait(files.map((file) {
        if (basename(file) == 'packages') return new Future.immediate([]);
        return dirExists(file).chain((isDir) {
          if (!isDir) return new Future.immediate([]);
          return _listDirWithoutPackages(file);
        }).transform((subfiles) {
          var fileAndSubfiles = [file];
          fileAndSubfiles.addAll(subfiles);
          return fileAndSubfiles;
        });
      }));
    }).transform(flatten);
  }

  /**
   * Creates a symlink to the `packages` directory in [dir] if none exists.
   */
  Future _linkSecondaryPackageDir(String dir) {
    var to = join(dir, 'packages');
    return exists(to).chain((exists) {
      if (exists) return new Future.immediate(null);
      return createSymlink(path, to);
    });
  }
}
