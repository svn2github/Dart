// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pubspec;

import 'package.dart';
import 'source.dart';
import 'source_registry.dart';
import 'utils.dart';
import 'version.dart';
import 'yaml/yaml.dart';

/**
 * The parsed and validated contents of a pubspec file.
 */
class Pubspec {
  /**
   * This package's name.
   */
  final String name;

  /**
   * This package's version.
   */
  final Version version;

  /**
   * The packages this package depends on.
   */
  List<PackageRef> dependencies;

  Pubspec(this.name, this.version, this.dependencies);

  Pubspec.empty()
    : name = null,
      version = Version.none,
      dependencies = <PackageRef>[];

  /** Whether or not the pubspec has no contents. */
  bool get isEmpty =>
    name == null && version == Version.none && dependencies.isEmpty;

  /**
   * Parses the pubspec whose text is [contents]. If the pubspec doesn't define
   * version for itself, it defaults to [Version.none].
   */
  factory Pubspec.parse(String contents, SourceRegistry sources) {
    var name = null;
    var version = Version.none;

    if (contents.trim() == '') return new Pubspec.empty();

    var parsedPubspec = loadYaml(contents);
    if (parsedPubspec == null) return new Pubspec.empty();

    if (parsedPubspec is! Map) {
      throw new FormatException('The pubspec must be a YAML mapping.');
    }

    if (parsedPubspec.containsKey('name')) {
      name = parsedPubspec['name'];
      if (name is! String) {
        throw new FormatException(
            'The pubspec "name" field should be a string, but was "$name".');
      }
    }

    if (parsedPubspec.containsKey('version')) {
      version = new Version.parse(parsedPubspec['version']);
    }

    var dependencies = _parseDependencies(sources,
        parsedPubspec['dependencies']);

    // Even though the pub app itself doesn't use these fields, we validate
    // them here so that users find errors early before they try to upload to
    // the server:

    if (parsedPubspec.containsKey('homepage') &&
        parsedPubspec['homepage'] is! String) {
      throw new FormatException(
          'The "homepage" field should be a string, but was '
          '${parsedPubspec["homepage"]}.');
    }

    if (parsedPubspec.containsKey('author') &&
        parsedPubspec['author'] is! String) {
      throw new FormatException(
          'The "author" field should be a string, but was '
          '${parsedPubspec["author"]}.');
    }

    if (parsedPubspec.containsKey('authors')) {
      var authors = parsedPubspec['authors'];
      if (authors is List) {
        // All of the elements must be strings.
        if (!authors.every((author) => author is String)) {
          throw new FormatException('The "authors" field should be a string '
              'or a list of strings, but was "$authors".');
        }
      } else if (authors is! String) {
        throw new FormatException('The pubspec "authors" field should be a '
            'string or a list of strings, but was "$authors".');
      }

      if (parsedPubspec.containsKey('author')) {
        throw new FormatException('A pubspec should not have both an "author" '
            'and an "authors" field.');
      }
    }

    return new Pubspec(name, version, dependencies);
  }
}

List<PackageRef> _parseDependencies(SourceRegistry sources, yaml) {
  var dependencies = <PackageRef>[];

  // Allow an empty dependencies key.
  if (yaml == null) return dependencies;

  if (yaml is! Map || yaml.keys.some((e) => e is! String)) {
    throw new FormatException(
        'The pubspec dependencies should be a map of package names, but '
        'was ${yaml}.');
  }

  yaml.forEach((name, spec) {
    var description, source;
    var versionConstraint = new VersionRange();
    if (spec == null) {
      description = name;
      source = sources.defaultSource;
    } else if (spec is String) {
      description = name;
      source = sources.defaultSource;
      versionConstraint = new VersionConstraint.parse(spec);
    } else if (spec is Map) {
      if (spec.containsKey('version')) {
        versionConstraint = new VersionConstraint.parse(spec.remove('version'));
      }

      var sourceNames = spec.keys;
      if (sourceNames.length > 1) {
        throw new FormatException(
            'Dependency $name may only have one source: $sourceNames.');
      }

      var sourceName = only(sourceNames);
      if (sourceName is! String) {
        throw new FormatException(
            'Source name $sourceName should be a string.');
      }

      source = sources[sourceName];
      description = spec[sourceName];
    } else {
      throw new FormatException(
          'Dependency specification $spec should be a string or a mapping.');
    }

    source.validateDescription(description, fromLockFile: false);

    dependencies.add(new PackageRef(
        name, source, versionConstraint, description));
  });

  return dependencies;
}