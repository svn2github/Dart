// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "package:expect/expect.dart";
import 'dart:async';
import "package:async_helper/async_helper.dart";
import 'memory_compiler.dart' show compilerFor;
import '../../../sdk/lib/_internal/compiler/implementation/apiimpl.dart' show
    Compiler;
import
    '../../../sdk/lib/_internal/compiler/implementation/elements/elements.dart'
show
    Element, LibraryElement, ClassElement;
import
    '../../../sdk/lib/_internal/compiler/implementation/tree/tree.dart'
show
    Block, ExpressionStatement, FunctionExpression, Node, Send;
import
    '../../../sdk/lib/_internal/compiler/implementation/dart_backend/dart_backend.dart'
show
    DartBackend, ElementAst;
import
    '../../../sdk/lib/_internal/compiler/implementation/mirror_renamer/mirror_renamer.dart'
show
    MirrorRenamer;


main() {
  testWithMirrorRenaming(minify: true);
  testWithMirrorRenaming(minify: false);
  testWithoutMirrorRenaming(minify: true);
  testWithoutMirrorRenaming(minify: false);
}

Future<Compiler> runCompiler({useMirrorHelperLibrary: false, minify: false}) {
  List<String> options = ['--output-type=dart'];
  if (minify) {
    options.add('--minify');
  }
  Compiler compiler = compilerFor(MEMORY_SOURCE_FILES, options: options);
  DartBackend backend = compiler.backend;
  backend.useMirrorHelperLibrary = useMirrorHelperLibrary;
  return
      compiler.runCompiler(Uri.parse('memory:main.dart')).then((_) => compiler);
}

void testWithMirrorRenaming({bool minify}) {
  asyncTest(() => runCompiler(useMirrorHelperLibrary: true, minify: minify).
      then((Compiler compiler) {

    DartBackend backend = compiler.backend;
    MirrorRenamer mirrorRenamer = backend.mirrorRenamer;
    Map<Node, String> renames = backend.renames;
    Map<LibraryElement, String> imports = backend.imports;

    FunctionExpression node = backend.memberNodes.values.first.first;
    Block block = node.body;
    ExpressionStatement getNameFunctionNode = block.statements.nodes.head;
    Send send = getNameFunctionNode.expression;

    Expect.equals(renames[mirrorRenamer.mirrorHelperGetNameFunctionNode.name],
                  renames[send.selector]);
    Expect.equals("",
                  renames[send.receiver]);
    Expect.equals(1, imports.keys.length);
  }));
}

void testWithoutMirrorRenaming({bool minify}) {
  asyncTest(() => runCompiler(useMirrorHelperLibrary: false, minify: minify).
      then((Compiler compiler) {

    DartBackend backend = compiler.backend;
    Map<Node, String> renames = backend.renames;
    Map<LibraryElement, String> imports = backend.imports;

    FunctionExpression node = backend.memberNodes.values.first.first;
    Block block = node.body;
    ExpressionStatement getNameFunctionNode = block.statements.nodes.head;
    Send send = getNameFunctionNode.expression;

    Expect.isFalse(renames.containsKey(send.selector));
    Expect.isFalse(renames.containsKey(send.receiver));
    Expect.equals(1, imports.keys.length);
  }));
}

const MEMORY_SOURCE_FILES = const <String, String> {
  'main.dart': """
import 'dart:mirrors';

class Foo {
  noSuchMethod(Invocation invocation) {
    MirrorSystem.getName(invocation.memberName);
  }
}

void main() {
  new Foo().fisk();
}
"""};
