// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library reexport_handled_test;

import "package:expect/expect.dart";
import "package:async_helper/async_helper.dart";
import 'mock_compiler.dart';
import '../../../sdk/lib/_internal/compiler/implementation/elements/elements.dart'
    show Element,
         LibraryElement;

final exportingLibraryUri = Uri.parse('exporting.dart');
const String EXPORTING_LIBRARY_SOURCE = '''
library exporting;
var foo;
''';

final reexportingLibraryUri = Uri.parse('reexporting.dart');
const String REEXPORTING_LIBRARY_SOURCE = '''
library reexporting;
export 'exporting.dart';
''';

void main() {
  var compiler = new MockCompiler();
  compiler.registerSource(exportingLibraryUri, EXPORTING_LIBRARY_SOURCE);
  compiler.registerSource(reexportingLibraryUri, REEXPORTING_LIBRARY_SOURCE);

  // Load exporting library before the reexporting library.
  asyncTest(() => compiler.libraryLoader.loadLibrary(
      exportingLibraryUri, null, exportingLibraryUri).then((exportingLibrary) {
    Expect.isTrue(exportingLibrary.exportsHandled);
    var foo = findInExports(exportingLibrary, 'foo');
    Expect.isNotNull(foo);
    Expect.isTrue(foo.isField());

    // Load reexporting library when exports are handled on the exporting library.
    return compiler.libraryLoader.loadLibrary(
        reexportingLibraryUri, null, reexportingLibraryUri);
  }).then((reexportingLibrary) {
    var foo = findInExports(reexportingLibrary, 'foo');
    Expect.isNotNull(foo);
    Expect.isTrue(foo.isField());
  }));
}

Element findInExports(LibraryElement library, String name) {
  for (var export in library.exports) {
    if (export.name == name) {
      return export;
    }
  }
  return null;
}
