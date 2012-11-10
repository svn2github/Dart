// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library pubspec_test;

import '../../../pkg/unittest/lib/unittest.dart';
import '../../pub/pubspec.dart';
import '../../pub/source.dart';
import '../../pub/source_registry.dart';
import '../../pub/utils.dart';
import '../../pub/version.dart';

class MockSource extends Source {
  final String name = "mock";
  final bool shouldCache = false;
  void validateDescription(description, {bool fromLockFile: false}) {
    if (description != 'ok') throw new FormatException('Bad');
  }
  String packageName(description) => 'foo';
}

main() {
  group('Pubspec', () {
    group('parse()', () {
      var sources = new SourceRegistry();
      sources.register(new MockSource());

      test("allows a version constraint for dependencies", () {
        var pubspec = new Pubspec.parse('''
dependencies:
  foo:
    mock: ok
    version: ">=1.2.3 <3.4.5"
''', sources);

        var foo = pubspec.dependencies[0];
        expect(foo.name, equals('foo'));
        expect(foo.constraint.allows(new Version(1, 2, 3)), isTrue);
        expect(foo.constraint.allows(new Version(1, 2, 5)), isTrue);
        expect(foo.constraint.allows(new Version(3, 4, 5)), isFalse);
      });

      test("allows an empty dependencies map", () {
        var pubspec = new Pubspec.parse('''
dependencies:
''', sources);

        expect(pubspec.dependencies, isEmpty);
      });

      test("throws if the description isn't valid", () {
        expect(() {
          new Pubspec.parse('''
dependencies:
  foo:
    mock: bad
''', sources);
        }, throwsFormatException);
      });

      test("throws if 'name' is not a string", () {
        expect(() => new Pubspec.parse('name: [not, a, string]', sources),
            throwsFormatException);
      });

      test("throws if 'homepage' is not a string", () {
        expect(() => new Pubspec.parse('homepage: [not, a, string]', sources),
            throwsFormatException);
      });

      test("throws if 'authors' is not a string or a list of strings", () {
        new Pubspec.parse('authors: ok fine', sources);
        new Pubspec.parse('authors: [also, ok, fine]', sources);

        expect(() => new Pubspec.parse('authors: 123', sources),
            throwsFormatException);

        expect(() => new Pubspec.parse('authors: {not: {a: string}}', sources),
            throwsFormatException);

        expect(() => new Pubspec.parse('authors: [ok, {not: ok}]', sources),
            throwsFormatException);
      });

      test("throws if 'author' is not a string", () {
        new Pubspec.parse('author: ok fine', sources);

        expect(() => new Pubspec.parse('author: 123', sources),
            throwsFormatException);

        expect(() => new Pubspec.parse('author: {not: {a: string}}', sources),
            throwsFormatException);

        expect(() => new Pubspec.parse('author: [not, ok]', sources),
            throwsFormatException);
      });

      test("throws if both 'author' and 'authors' are present", () {
        expect(() => new Pubspec.parse('{author: abe, authors: ted}', sources),
            throwsFormatException);
      });

      test("allows comment-only files", () {
        var pubspec = new Pubspec.parse('''
# No external dependencies yet
# Including for completeness
# ...and hoping the spec expands to include details about author, version, etc
# See http://www.dartlang.org/docs/pub-package-manager/ for details
''', sources);
        expect(pubspec.version, equals(Version.none));
        expect(pubspec.dependencies, isEmpty);
      });
    });
  });
}
