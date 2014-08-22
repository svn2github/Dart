// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of dart2js_incremental;

/// Do not call this method directly. It will be made private.
// TODO(ahe): Make this method private.
Compiler reuseCompiler(
    {DiagnosticHandler diagnosticHandler,
     CompilerInputProvider inputProvider,
     CompilerOutputProvider outputProvider,
     List<String> options: const [],
     Compiler cachedCompiler,
     Uri libraryRoot,
     Uri packageRoot,
     bool packagesAreImmutable: false,
     Map<String, dynamic> environment}) {
  UserTag oldTag = new UserTag('_reuseCompiler').makeCurrent();
  if (libraryRoot == null) {
    throw 'Missing libraryRoot';
  }
  if (inputProvider == null) {
    throw 'Missing inputProvider';
  }
  if (diagnosticHandler == null) {
    throw 'Missing diagnosticHandler';
  }
  if (outputProvider == null) {
    outputProvider = NullSink.outputProvider;
  }
  if (environment == null) {
    environment = {};
  }
  Compiler compiler = cachedCompiler;
  if (compiler == null ||
      compiler.libraryRoot != libraryRoot ||
      !compiler.hasIncrementalSupport ||
      compiler.hasCrashed ||
      compiler.compilerWasCancelled ||
      compiler.enqueuer.resolution.hasEnqueuedReflectiveElements ||
      compiler.deferredLoadTask.isProgramSplit) {
    if (compiler != null && compiler.hasIncrementalSupport) {
      print('***FLUSH***');
      if (compiler.hasCrashed) {
        print('Unable to reuse compiler due to crash.');
      } else if (compiler.compilerWasCancelled) {
        print('Unable to reuse compiler due to cancel.');
      } else if (compiler.enqueuer.resolution.hasEnqueuedReflectiveElements) {
        print('Unable to reuse compiler due to dart:mirrors.');
      } else if (compiler.deferredLoadTask.splitProgram) {
        print('Unable to reuse compiler due to deferred loading.');
      } else {
        print('Unable to reuse compiler.');
      }
    }
    compiler = new Compiler(
        inputProvider,
        outputProvider,
        diagnosticHandler,
        libraryRoot,
        packageRoot,
        options,
        environment);
  } else {
    compiler
        ..outputProvider = outputProvider
        ..provider = inputProvider
        ..handler = diagnosticHandler
        ..enqueuer.resolution.queueIsClosed = false
        ..enqueuer.resolution.hasEnqueuedReflectiveElements = false
        ..enqueuer.resolution.hasEnqueuedReflectiveStaticFields = false
        ..enqueuer.codegen.queueIsClosed = false
        ..enqueuer.codegen.hasEnqueuedReflectiveElements = false
        ..enqueuer.codegen.hasEnqueuedReflectiveStaticFields = false
        ..assembledCode = null
        ..compilationFailed = false;
    JavaScriptBackend backend = compiler.backend;

    backend.emitter.cachedElements.addAll(backend.generatedCode.keys);

    compiler.enqueuer.codegen.newlyEnqueuedElements.clear();

    backend.emitter.containerBuilder
        ..staticGetters.clear()
        ..methodClosures.clear();

    backend.emitter.nsmEmitter
        ..trivialNsmHandlers.clear();

    backend.emitter.typeTestEmitter
        ..checkedClasses = null
        ..checkedFunctionTypes = null
        ..checkedGenericFunctionTypes.clear()
        ..checkedNonGenericFunctionTypes.clear()
        ..rtiNeededClasses.clear()
        ..cachedClassesUsingTypeVariableTests = null;

    backend.emitter.interceptorEmitter
        ..interceptorInvocationNames.clear();

    backend.emitter.metadataEmitter
        ..globalMetadata.clear()
        ..globalMetadataMap.clear();

    backend.emitter.nativeEmitter
        ..nativeBuffer.clear()
        ..nativeClasses.clear()
        ..nativeMethods.clear();

    backend.emitter
        ..outputBuffers.clear()
        ..deferredConstants.clear()
        ..isolateProperties = null
        ..classesCollector = null
        ..neededClasses.clear()
        ..outputClassLists.clear()
        ..nativeClasses.clear()
        ..mangledFieldNames.clear()
        ..mangledGlobalFieldNames.clear()
        ..recordedMangledNames.clear()
        ..additionalProperties.clear()
        ..readTypeVariables.clear()
        ..instantiatedClasses = null
        ..precompiledFunction.clear()
        ..precompiledConstructorNames.clear()
        ..hasMakeConstantList = false
        ..elementDescriptors.clear();

    backend
        ..preMirrorsMethodCount = 0;

    compiler.libraryLoader.reset(reuseLibrary: (LibraryElement library) {
      return library.isPlatformLibrary ||
             (packagesAreImmutable && library.isPackageLibrary);
    });
  }
  oldTag.makeCurrent();
  return compiler;
}
