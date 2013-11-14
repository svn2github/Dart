// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library path.test.windows_test;

import 'package:unittest/unittest.dart';
import 'package:path/path.dart' as path;

main() {
  var builder = new path.Builder(style: path.Style.windows,
                                 root: r'C:\root\path');

  if (new path.Builder().style == path.Style.windows) {
    group('absolute', () {
      expect(path.absolute(r'a\b.txt'), path.join(path.current, r'a\b.txt'));
      expect(path.absolute(r'C:\a\b.txt'), r'C:\a\b.txt');
      expect(path.absolute(r'\\a\b.txt'), r'\\a\b.txt');
    });
  }

  group('separator', () {
    expect(builder.separator, '\\');
  });

  test('extension', () {
    expect(builder.extension(''), '');
    expect(builder.extension('.'), '');
    expect(builder.extension('..'), '');
    expect(builder.extension('a/..'), '');
    expect(builder.extension('foo.dart'), '.dart');
    expect(builder.extension('foo.dart.js'), '.js');
    expect(builder.extension('foo bar\gule fisk.dart.js'), '.js');
    expect(builder.extension(r'a.b\c'), '');
    expect(builder.extension('a.b/c.d'), '.d');
    expect(builder.extension(r'~\.bashrc'), '');
    expect(builder.extension(r'a.b/c'), r'');
    expect(builder.extension(r'foo.dart\'), '.dart');
    expect(builder.extension(r'foo.dart\\'), '.dart');
  });

  test('rootPrefix', () {
    expect(builder.rootPrefix(''), '');
    expect(builder.rootPrefix('a'), '');
    expect(builder.rootPrefix(r'a\b'), '');
    expect(builder.rootPrefix(r'C:\a\c'), r'C:\');
    expect(builder.rootPrefix('C:\\'), r'C:\');
    expect(builder.rootPrefix('C:/'), 'C:/');
    expect(builder.rootPrefix(r'\\server\share\a\b'), r'\\server\share');
    expect(builder.rootPrefix(r'\a\b'), r'\');
    expect(builder.rootPrefix(r'/a/b'), r'/');
    expect(builder.rootPrefix(r'\'), r'\');
    expect(builder.rootPrefix(r'/'), r'/');
  });

  test('dirname', () {
    expect(builder.dirname(r''), '.');
    expect(builder.dirname(r'a'), '.');
    expect(builder.dirname(r'a\b'), 'a');
    expect(builder.dirname(r'a\b\c'), r'a\b');
    expect(builder.dirname(r'a\b.c'), 'a');
    expect(builder.dirname(r'a\'), '.');
    expect(builder.dirname('a/'), '.');
    expect(builder.dirname(r'a\.'), 'a');
    expect(builder.dirname(r'a\b/c'), r'a\b');
    expect(builder.dirname(r'C:\a'), r'C:\');
    expect(builder.dirname(r'C:\\\a'), r'C:\');
    expect(builder.dirname(r'C:\'), r'C:\');
    expect(builder.dirname(r'C:\\\'), r'C:\');
    expect(builder.dirname(r'a\b\'), r'a');
    expect(builder.dirname(r'a/b\c'), 'a/b');
    expect(builder.dirname(r'a\\'), r'.');
    expect(builder.dirname(r'a\b\\'), 'a');
    expect(builder.dirname(r'a\\b'), 'a');
    expect(builder.dirname(r'foo bar\gule fisk'), 'foo bar');
    expect(builder.dirname(r'\\server\share'), r'\\server\share');
    expect(builder.dirname(r'\\server\share\dir'), r'\\server\share');
    expect(builder.dirname(r'\a'), r'\');
    expect(builder.dirname(r'/a'), r'/');
    expect(builder.dirname(r'\'), r'\');
    expect(builder.dirname(r'/'), r'/');
  });

  test('basename', () {
    expect(builder.basename(r''), '');
    expect(builder.basename(r'.'), '.');
    expect(builder.basename(r'..'), '..');
    expect(builder.basename(r'.hest'), '.hest');
    expect(builder.basename(r'a'), 'a');
    expect(builder.basename(r'a\b'), 'b');
    expect(builder.basename(r'a\b\c'), 'c');
    expect(builder.basename(r'a\b.c'), 'b.c');
    expect(builder.basename(r'a\'), 'a');
    expect(builder.basename(r'a/'), 'a');
    expect(builder.basename(r'a\.'), '.');
    expect(builder.basename(r'a\b/c'), r'c');
    expect(builder.basename(r'C:\a'), 'a');
    expect(builder.basename(r'C:\'), r'C:\');
    expect(builder.basename(r'a\b\'), 'b');
    expect(builder.basename(r'a/b\c'), 'c');
    expect(builder.basename(r'a\\'), 'a');
    expect(builder.basename(r'a\b\\'), 'b');
    expect(builder.basename(r'a\\b'), 'b');
    expect(builder.basename(r'a\\b'), 'b');
    expect(builder.basename(r'a\fisk hest.ma pa'), 'fisk hest.ma pa');
    expect(builder.basename(r'\\server\share'), r'\\server\share');
    expect(builder.basename(r'\\server\share\dir'), r'dir');
    expect(builder.basename(r'\a'), r'a');
    expect(builder.basename(r'/a'), r'a');
    expect(builder.basename(r'\'), r'\');
    expect(builder.basename(r'/'), r'/');
  });

  test('basenameWithoutExtension', () {
    expect(builder.basenameWithoutExtension(''), '');
    expect(builder.basenameWithoutExtension('.'), '.');
    expect(builder.basenameWithoutExtension('..'), '..');
    expect(builder.basenameWithoutExtension('.hest'), '.hest');
    expect(builder.basenameWithoutExtension('a'), 'a');
    expect(builder.basenameWithoutExtension(r'a\b'), 'b');
    expect(builder.basenameWithoutExtension(r'a\b\c'), 'c');
    expect(builder.basenameWithoutExtension(r'a\b.c'), 'b');
    expect(builder.basenameWithoutExtension(r'a\'), 'a');
    expect(builder.basenameWithoutExtension(r'a\.'), '.');
    expect(builder.basenameWithoutExtension(r'a\b/c'), r'c');
    expect(builder.basenameWithoutExtension(r'a\.bashrc'), '.bashrc');
    expect(builder.basenameWithoutExtension(r'a\b\c.d.e'), 'c.d');
    expect(builder.basenameWithoutExtension(r'a\\'), 'a');
    expect(builder.basenameWithoutExtension(r'a\b\\'), 'b');
    expect(builder.basenameWithoutExtension(r'a\\b'), 'b');
    expect(builder.basenameWithoutExtension(r'a\b.c\'), 'b');
    expect(builder.basenameWithoutExtension(r'a\b.c\\'), 'b');
    expect(builder.basenameWithoutExtension(r'C:\f h.ma pa.f s'), 'f h.ma pa');
  });

  test('isAbsolute', () {
    expect(builder.isAbsolute(''), false);
    expect(builder.isAbsolute('.'), false);
    expect(builder.isAbsolute('..'), false);
    expect(builder.isAbsolute('a'), false);
    expect(builder.isAbsolute(r'a\b'), false);
    expect(builder.isAbsolute(r'\a\b'), true);
    expect(builder.isAbsolute(r'\'), true);
    expect(builder.isAbsolute(r'/a/b'), true);
    expect(builder.isAbsolute(r'/'), true);
    expect(builder.isAbsolute('~'), false);
    expect(builder.isAbsolute('.'), false);
    expect(builder.isAbsolute(r'..\a'), false);
    expect(builder.isAbsolute(r'a:/a\b'), true);
    expect(builder.isAbsolute(r'D:/a/b'), true);
    expect(builder.isAbsolute(r'c:\'), true);
    expect(builder.isAbsolute(r'B:\'), true);
    expect(builder.isAbsolute(r'c:\a'), true);
    expect(builder.isAbsolute(r'C:\a'), true);
    expect(builder.isAbsolute(r'\\server\share'), true);
    expect(builder.isAbsolute(r'\\server\share\path'), true);
  });

  test('isRelative', () {
    expect(builder.isRelative(''), true);
    expect(builder.isRelative('.'), true);
    expect(builder.isRelative('..'), true);
    expect(builder.isRelative('a'), true);
    expect(builder.isRelative(r'a\b'), true);
    expect(builder.isRelative(r'\a\b'), false);
    expect(builder.isRelative(r'\'), false);
    expect(builder.isRelative(r'/a/b'), false);
    expect(builder.isRelative(r'/'), false);
    expect(builder.isRelative('~'), true);
    expect(builder.isRelative('.'), true);
    expect(builder.isRelative(r'..\a'), true);
    expect(builder.isRelative(r'a:/a\b'), false);
    expect(builder.isRelative(r'D:/a/b'), false);
    expect(builder.isRelative(r'c:\'), false);
    expect(builder.isRelative(r'B:\'), false);
    expect(builder.isRelative(r'c:\a'), false);
    expect(builder.isRelative(r'C:\a'), false);
    expect(builder.isRelative(r'\\server\share'), false);
    expect(builder.isRelative(r'\\server\share\path'), false);
  });

  test('isRootRelative', () {
    expect(builder.isRootRelative(''), false);
    expect(builder.isRootRelative('.'), false);
    expect(builder.isRootRelative('..'), false);
    expect(builder.isRootRelative('a'), false);
    expect(builder.isRootRelative(r'a\b'), false);
    expect(builder.isRootRelative(r'\a\b'), true);
    expect(builder.isRootRelative(r'\'), true);
    expect(builder.isRootRelative(r'/a/b'), true);
    expect(builder.isRootRelative(r'/'), true);
    expect(builder.isRootRelative('~'), false);
    expect(builder.isRootRelative('.'), false);
    expect(builder.isRootRelative(r'..\a'), false);
    expect(builder.isRootRelative(r'a:/a\b'), false);
    expect(builder.isRootRelative(r'D:/a/b'), false);
    expect(builder.isRootRelative(r'c:\'), false);
    expect(builder.isRootRelative(r'B:\'), false);
    expect(builder.isRootRelative(r'c:\a'), false);
    expect(builder.isRootRelative(r'C:\a'), false);
    expect(builder.isRootRelative(r'\\server\share'), false);
    expect(builder.isRootRelative(r'\\server\share\path'), false);
  });

  group('join', () {
    test('allows up to eight parts', () {
      expect(builder.join('a'), 'a');
      expect(builder.join('a', 'b'), r'a\b');
      expect(builder.join('a', 'b', 'c'), r'a\b\c');
      expect(builder.join('a', 'b', 'c', 'd'), r'a\b\c\d');
      expect(builder.join('a', 'b', 'c', 'd', 'e'), r'a\b\c\d\e');
      expect(builder.join('a', 'b', 'c', 'd', 'e', 'f'), r'a\b\c\d\e\f');
      expect(builder.join('a', 'b', 'c', 'd', 'e', 'f', 'g'), r'a\b\c\d\e\f\g');
      expect(builder.join('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'),
          r'a\b\c\d\e\f\g\h');
    });

    test('does not add separator if a part ends or begins in one', () {
      expect(builder.join(r'a\', 'b', r'c\', 'd'), r'a\b\c\d');
      expect(builder.join('a/', 'b'), r'a/b');
    });

    test('ignores parts before an absolute path', () {
      expect(builder.join('a', r'\b', r'\c', 'd'), r'\c\d');
      expect(builder.join('a', '/b', '/c', 'd'), r'/c\d');
      expect(builder.join('a', r'c:\b', 'c', 'd'), r'c:\b\c\d');
      expect(builder.join('a', r'\\b\c', r'\\d\e', 'f'), r'\\d\e\f');
      expect(builder.join('a', r'c:\b', r'\c', 'd'), r'c:\c\d');
      expect(builder.join('a', r'\\b\c\d', r'\e', 'f'), r'\\b\c\e\f');
    });

    test('ignores trailing nulls', () {
      expect(builder.join('a', null), equals('a'));
      expect(builder.join('a', 'b', 'c', null, null), equals(r'a\b\c'));
    });

    test('ignores empty strings', () {
      expect(builder.join(''), '');
      expect(builder.join('', ''), '');
      expect(builder.join('', 'a'), 'a');
      expect(builder.join('a', '', 'b', '', '', '', 'c'), r'a\b\c');
      expect(builder.join('a', 'b', ''), r'a\b');
    });

    test('disallows intermediate nulls', () {
      expect(() => builder.join('a', null, 'b'), throwsArgumentError);
      expect(() => builder.join(null, 'a'), throwsArgumentError);
    });

    test('join does not modify internal ., .., or trailing separators', () {
      expect(builder.join('a/', 'b/c/'), 'a/b/c/');
      expect(builder.join(r'a\b\./c\..\\', r'd\..\.\..\\e\f\\'),
             r'a\b\./c\..\\d\..\.\..\\e\f\\');
      expect(builder.join(r'a\b', r'c\..\..\..\..'), r'a\b\c\..\..\..\..');
      expect(builder.join(r'a', 'b${builder.separator}'), r'a\b\');
    });
  });

  group('joinAll', () {
    test('allows more than eight parts', () {
      expect(builder.joinAll(['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i']),
          r'a\b\c\d\e\f\g\h\i');
    });

    test('does not add separator if a part ends or begins in one', () {
      expect(builder.joinAll([r'a\', 'b', r'c\', 'd']), r'a\b\c\d');
      expect(builder.joinAll(['a/', 'b']), r'a/b');
    });

    test('ignores parts before an absolute path', () {
      expect(builder.joinAll(['a', r'\b', r'\c', 'd']), r'\c\d');
      expect(builder.joinAll(['a', '/b', '/c', 'd']), r'/c\d');
      expect(builder.joinAll(['a', r'c:\b', 'c', 'd']), r'c:\b\c\d');
      expect(builder.joinAll(['a', r'\\b\c', r'\\d\e', 'f']), r'\\d\e\f');
      expect(builder.joinAll(['a', r'c:\b', r'\c', 'd']), r'c:\c\d');
      expect(builder.joinAll(['a', r'\\b\c\d', r'\e', 'f']), r'\\b\c\e\f');
    });
  });

  group('split', () {
    test('simple cases', () {
      expect(builder.split(''), []);
      expect(builder.split('.'), ['.']);
      expect(builder.split('..'), ['..']);
      expect(builder.split('foo'), equals(['foo']));
      expect(builder.split(r'foo\bar.txt'), equals(['foo', 'bar.txt']));
      expect(builder.split(r'foo\bar/baz'), equals(['foo', 'bar', 'baz']));
      expect(builder.split(r'foo\..\bar\.\baz'),
          equals(['foo', '..', 'bar', '.', 'baz']));
      expect(builder.split(r'foo\\bar\\\baz'), equals(['foo', 'bar', 'baz']));
      expect(builder.split(r'foo\/\baz'), equals(['foo', 'baz']));
      expect(builder.split('.'), equals(['.']));
      expect(builder.split(''), equals([]));
      expect(builder.split('foo/'), equals(['foo']));
      expect(builder.split(r'C:\'), equals([r'C:\']));
    });

    test('includes the root for absolute paths', () {
      expect(builder.split(r'C:\foo\bar\baz'),
          equals([r'C:\', 'foo', 'bar', 'baz']));
      expect(builder.split(r'C:\\'), equals([r'C:\']));

      expect(builder.split(r'\\server\share\foo\bar\baz'),
          equals([r'\\server\share', 'foo', 'bar', 'baz']));
      expect(builder.split(r'\\server\share'), equals([r'\\server\share']));

      expect(builder.split(r'\foo\bar\baz'),
          equals([r'\', 'foo', 'bar', 'baz']));
      expect(builder.split(r'\'), equals([r'\']));
    });
  });

  group('normalize', () {
    test('simple cases', () {
      expect(builder.normalize(''), '.');
      expect(builder.normalize('.'), '.');
      expect(builder.normalize('..'), '..');
      expect(builder.normalize('a'), 'a');
      expect(builder.normalize('/a/b'), r'\a\b');
      expect(builder.normalize(r'\'), r'\');
      expect(builder.normalize(r'\a\b'), r'\a\b');
      expect(builder.normalize('/'), r'\');
      expect(builder.normalize('C:/'), r'C:\');
      expect(builder.normalize(r'C:\'), r'C:\');
      expect(builder.normalize(r'\\server\share'), r'\\server\share');
      expect(builder.normalize('a\\.\\\xc5\u0bf8-;\u{1f085}\u{00}\\c\\d\\..\\'),
             'a\\\xc5\u0bf8-;\u{1f085}\u{00}\x5cc');
    });

    test('collapses redundant separators', () {
      expect(builder.normalize(r'a\b\c'), r'a\b\c');
      expect(builder.normalize(r'a\\b\\\c\\\\d'), r'a\b\c\d');
    });

    test('eliminates "." parts', () {
      expect(builder.normalize(r'.\'), '.');
      expect(builder.normalize(r'c:\.'), r'c:\');
      expect(builder.normalize(r'B:\.\'), r'B:\');
      expect(builder.normalize(r'\\server\share\.'), r'\\server\share');
      expect(builder.normalize(r'.\.'), '.');
      expect(builder.normalize(r'a\.\b'), r'a\b');
      expect(builder.normalize(r'a\.b\c'), r'a\.b\c');
      expect(builder.normalize(r'a\./.\b\.\c'), r'a\b\c');
      expect(builder.normalize(r'.\./a'), 'a');
      expect(builder.normalize(r'a/.\.'), 'a');
      expect(builder.normalize(r'\.'), r'\');
      expect(builder.normalize('/.'), r'\');
    });

    test('eliminates ".." parts', () {
      expect(builder.normalize('..'), '..');
      expect(builder.normalize(r'..\'), '..');
      expect(builder.normalize(r'..\..\..'), r'..\..\..');
      expect(builder.normalize(r'../..\..\'), r'..\..\..');
      expect(builder.normalize(r'\\server\share\..'), r'\\server\share');
      expect(builder.normalize(r'\\server\share\..\../..\a'),
          r'\\server\share\a');
      expect(builder.normalize(r'c:\..'), r'c:\');
      expect(builder.normalize(r'A:/..\..\..'), r'A:\');
      expect(builder.normalize(r'b:\..\..\..\a'), r'b:\a');
      expect(builder.normalize(r'b:\r\..\..\..\a\c\.\..'), r'b:\a');
      expect(builder.normalize(r'a\..'), '.');
      expect(builder.normalize(r'..\a'), r'..\a');
      expect(builder.normalize(r'c:\..\a'), r'c:\a');
      expect(builder.normalize(r'\..\a'), r'\a');
      expect(builder.normalize(r'a\b\..'), 'a');
      expect(builder.normalize(r'..\a\b\..'), r'..\a');
      expect(builder.normalize(r'a\..\b'), 'b');
      expect(builder.normalize(r'a\.\..\b'), 'b');
      expect(builder.normalize(r'a\b\c\..\..\d\e\..'), r'a\d');
      expect(builder.normalize(r'a\b\..\..\..\..\c'), r'..\..\c');
      expect(builder.normalize(r'a/b/c/../../..d/./.e/f././'), r'a\..d\.e\f.');
    });

    test('removes trailing separators', () {
      expect(builder.normalize(r'.\'), '.');
      expect(builder.normalize(r'.\\'), '.');
      expect(builder.normalize(r'a/'), 'a');
      expect(builder.normalize(r'a\b\'), r'a\b');
      expect(builder.normalize(r'a\b\\\'), r'a\b');
    });

    test('normalizes separators', () {
      expect(builder.normalize(r'a/b\c'), r'a\b\c');
    });
  });

  group('relative', () {
    group('from absolute root', () {
      test('given absolute path in root', () {
        expect(builder.relative(r'C:\'), r'..\..');
        expect(builder.relative(r'C:\root'), '..');
        expect(builder.relative(r'\root'), '..');
        expect(builder.relative(r'C:\root\path'), '.');
        expect(builder.relative(r'\root\path'), '.');
        expect(builder.relative(r'C:\root\path\a'), 'a');
        expect(builder.relative(r'\root\path\a'), 'a');
        expect(builder.relative(r'C:\root\path\a\b.txt'), r'a\b.txt');
        expect(builder.relative(r'C:\root\a\b.txt'), r'..\a\b.txt');
        expect(builder.relative(r'C:/'), r'..\..');
        expect(builder.relative(r'C:/root'), '..');
        expect(builder.relative(r'c:\'), r'..\..');
        expect(builder.relative(r'c:\root'), '..');
      });

      test('given absolute path outside of root', () {
        expect(builder.relative(r'C:\a\b'), r'..\..\a\b');
        expect(builder.relative(r'\a\b'), r'..\..\a\b');
        expect(builder.relative(r'C:\root\path\a'), 'a');
        expect(builder.relative(r'C:\root\path\a\b.txt'), r'a\b.txt');
        expect(builder.relative(r'C:\root\a\b.txt'), r'..\a\b.txt');
        expect(builder.relative(r'C:/a/b'), r'..\..\a\b');
        expect(builder.relative(r'C:/root/path/a'), 'a');
        expect(builder.relative(r'c:\a\b'), r'..\..\a\b');
        expect(builder.relative(r'c:\root\path\a'), 'a');
      });

      test('given absolute path on different drive', () {
        expect(builder.relative(r'D:\a\b'), r'D:\a\b');
      });

      test('given relative path', () {
        // The path is considered relative to the root, so it basically just
        // normalizes.
        expect(builder.relative(''), '.');
        expect(builder.relative('.'), '.');
        expect(builder.relative('a'), 'a');
        expect(builder.relative(r'a\b.txt'), r'a\b.txt');
        expect(builder.relative(r'..\a\b.txt'), r'..\a\b.txt');
        expect(builder.relative(r'a\.\b\..\c.txt'), r'a\c.txt');
      });

      // Regression
      test('from root-only path', () {
        expect(builder.relative(r'C:\', from: r'C:\'), '.');
        expect(builder.relative(r'C:\root\path', from: r'C:\'), r'root\path');
      });
    });

    group('from relative root', () {
      var r = new path.Builder(style: path.Style.windows, root: r'foo\bar');

      test('given absolute path', () {
        expect(r.relative(r'C:\'), equals(r'C:\'));
        expect(r.relative(r'C:\a\b'), equals(r'C:\a\b'));
        expect(r.relative(r'\'), equals(r'\'));
        expect(r.relative(r'\a\b'), equals(r'\a\b'));
      });

      test('given relative path', () {
        // The path is considered relative to the root, so it basically just
        // normalizes.
        expect(r.relative(''), '.');
        expect(r.relative('.'), '.');
        expect(r.relative('..'), '..');
        expect(r.relative('a'), 'a');
        expect(r.relative(r'a\b.txt'), r'a\b.txt');
        expect(r.relative(r'..\a/b.txt'), r'..\a\b.txt');
        expect(r.relative(r'a\./b\../c.txt'), r'a\c.txt');
      });
    });

    group('from root-relative root', () {
      var r = new path.Builder(style: path.Style.windows, root: r'\foo\bar');

      test('given absolute path', () {
        expect(r.relative(r'C:\'), equals(r'C:\'));
        expect(r.relative(r'C:\a\b'), equals(r'C:\a\b'));
        expect(r.relative(r'\'), equals(r'..\..'));
        expect(r.relative(r'\a\b'), equals(r'..\..\a\b'));
        expect(r.relative('/'), equals(r'..\..'));
        expect(r.relative('/a/b'), equals(r'..\..\a\b'));
      });

      test('given relative path', () {
        // The path is considered relative to the root, so it basically just
        // normalizes.
        expect(r.relative(''), '.');
        expect(r.relative('.'), '.');
        expect(r.relative('..'), '..');
        expect(r.relative('a'), 'a');
        expect(r.relative(r'a\b.txt'), r'a\b.txt');
        expect(r.relative(r'..\a/b.txt'), r'..\a\b.txt');
        expect(r.relative(r'a\./b\../c.txt'), r'a\c.txt');
      });
    });

    test('from a root with extension', () {
      var r = new path.Builder(style: path.Style.windows, root: r'C:\dir.ext');
      expect(r.relative(r'C:\dir.ext\file'), 'file');
    });

    test('with a root parameter', () {
      expect(builder.relative(r'C:\foo\bar\baz', from: r'C:\foo\bar'),
          equals('baz'));
      expect(builder.relative('..', from: r'C:\foo\bar'),
          equals(r'..\..\root'));
      expect(builder.relative('..', from: r'D:\foo\bar'), equals(r'C:\root'));
      expect(builder.relative(r'C:\foo\bar\baz', from: r'foo\bar'),
          equals(r'..\..\..\..\foo\bar\baz'));
      expect(builder.relative('..', from: r'foo\bar'), equals(r'..\..\..'));
    });

    test('with a root parameter and a relative root', () {
      var r = new path.Builder(style: path.Style.windows, root: r'relative\root');
      expect(r.relative(r'C:\foo\bar\baz', from: r'C:\foo\bar'), equals('baz'));
      expect(() => r.relative('..', from: r'C:\foo\bar'), throwsArgumentError);
      expect(r.relative(r'C:\foo\bar\baz', from: r'foo\bar'),
          equals(r'C:\foo\bar\baz'));
      expect(r.relative('..', from: r'foo\bar'), equals(r'..\..\..'));
    });

    test('given absolute with different root prefix', () {
      expect(builder.relative(r'D:\a\b'), r'D:\a\b');
      expect(builder.relative(r'\\server\share\a\b'), r'\\server\share\a\b');
    });

    test('from a . root', () {
      var r = new path.Builder(style: path.Style.windows, root: '.');
      expect(r.relative(r'C:\foo\bar\baz'), equals(r'C:\foo\bar\baz'));
      expect(r.relative(r'foo\bar\baz'), equals(r'foo\bar\baz'));
      expect(r.relative(r'\foo\bar\baz'), equals(r'\foo\bar\baz'));
    });
  });

  group('resolve', () {
    test('allows up to seven parts', () {
      expect(builder.resolve('a'), r'C:\root\path\a');
      expect(builder.resolve('a', 'b'), r'C:\root\path\a\b');
      expect(builder.resolve('a', 'b', 'c'), r'C:\root\path\a\b\c');
      expect(builder.resolve('a', 'b', 'c', 'd'), r'C:\root\path\a\b\c\d');
      expect(builder.resolve('a', 'b', 'c', 'd', 'e'),
          r'C:\root\path\a\b\c\d\e');
      expect(builder.resolve('a', 'b', 'c', 'd', 'e', 'f'),
          r'C:\root\path\a\b\c\d\e\f');
      expect(builder.resolve('a', 'b', 'c', 'd', 'e', 'f', 'g'),
          r'C:\root\path\a\b\c\d\e\f\g');
    });

    test('does not add separator if a part ends in one', () {
      expect(builder.resolve(r'a\', 'b', r'c\', 'd'), r'C:\root\path\a\b\c\d');
      expect(builder.resolve('a/', 'b'), r'C:\root\path\a/b');
    });

    test('ignores parts before an absolute path', () {
      expect(builder.resolve('a', '/b', '/c', 'd'), r'C:\c\d');
      expect(builder.resolve('a', r'\b', r'\c', 'd'), r'C:\c\d');
      expect(builder.resolve('a', r'c:\b', 'c', 'd'), r'c:\b\c\d');
      expect(builder.resolve('a', r'\\b\c', r'\\d\e', 'f'), r'\\d\e\f');
    });
  });

  test('withoutExtension', () {
    expect(builder.withoutExtension(''), '');
    expect(builder.withoutExtension('a'), 'a');
    expect(builder.withoutExtension('.a'), '.a');
    expect(builder.withoutExtension('a.b'), 'a');
    expect(builder.withoutExtension(r'a\b.c'), r'a\b');
    expect(builder.withoutExtension(r'a\b.c.d'), r'a\b.c');
    expect(builder.withoutExtension(r'a\'), r'a\');
    expect(builder.withoutExtension(r'a\b\'), r'a\b\');
    expect(builder.withoutExtension(r'a\.'), r'a\.');
    expect(builder.withoutExtension(r'a\.b'), r'a\.b');
    expect(builder.withoutExtension(r'a.b\c'), r'a.b\c');
    expect(builder.withoutExtension(r'a/b.c/d'), r'a/b.c/d');
    expect(builder.withoutExtension(r'a\b/c'), r'a\b/c');
    expect(builder.withoutExtension(r'a\b/c.d'), r'a\b/c');
    expect(builder.withoutExtension(r'a.b/c'), r'a.b/c');
    expect(builder.withoutExtension(r'a\b.c\'), r'a\b\');
  });

  test('fromUri', () {
    expect(builder.fromUri(Uri.parse('file:///C:/path/to/foo')),
        r'C:\path\to\foo');
    expect(builder.fromUri(Uri.parse('file://server/share/path/to/foo')),
        r'\\server\share\path\to\foo');
    expect(builder.fromUri(Uri.parse('file:///C:/')), r'C:\');
    expect(builder.fromUri(Uri.parse('file://server/share')),
        r'\\server\share');
    expect(builder.fromUri(Uri.parse('foo/bar')), r'foo\bar');
    expect(builder.fromUri(Uri.parse('/C:/path/to/foo')), r'C:\path\to\foo');
    expect(builder.fromUri(Uri.parse('///C:/path/to/foo')), r'C:\path\to\foo');
    expect(builder.fromUri(Uri.parse('//server/share/path/to/foo')),
        r'\\server\share\path\to\foo');
    expect(builder.fromUri(Uri.parse('file:///C:/path/to/foo%23bar')),
        r'C:\path\to\foo#bar');
    expect(builder.fromUri(Uri.parse('file://server/share/path/to/foo%23bar')),
        r'\\server\share\path\to\foo#bar');
    expect(builder.fromUri(Uri.parse('_%7B_%7D_%60_%5E_%20_%22_%25_')),
        r'_{_}_`_^_ _"_%_');
    expect(() => builder.fromUri(Uri.parse('http://dartlang.org')),
        throwsArgumentError);
  });

  test('toUri', () {
    expect(builder.toUri(r'C:\path\to\foo'),
        Uri.parse('file:///C:/path/to/foo'));
    expect(builder.toUri(r'C:\path\to\foo\'),
        Uri.parse('file:///C:/path/to/foo/'));
    expect(builder.toUri(r'C:\'), Uri.parse('file:///C:/'));
    expect(builder.toUri(r'\\server\share'), Uri.parse('file://server/share'));
    expect(builder.toUri(r'\\server\share\'),
        Uri.parse('file://server/share/'));
    expect(builder.toUri(r'foo\bar'), Uri.parse('foo/bar'));
    expect(builder.toUri(r'C:\path\to\foo#bar'),
        Uri.parse('file:///C:/path/to/foo%23bar'));
    expect(builder.toUri(r'\\server\share\path\to\foo#bar'),
        Uri.parse('file://server/share/path/to/foo%23bar'));
    expect(builder.toUri(r'C:\_{_}_`_^_ _"_%_'),
        Uri.parse('file:///C:/_%7B_%7D_%60_%5E_%20_%22_%25_'));
    expect(builder.toUri(r'_{_}_`_^_ _"_%_'),
        Uri.parse('_%7B_%7D_%60_%5E_%20_%22_%25_'));
  });
}
