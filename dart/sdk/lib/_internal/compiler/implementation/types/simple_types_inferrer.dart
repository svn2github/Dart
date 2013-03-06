// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library simple_types_inferrer;

import '../closure.dart' show ClosureClassMap, ClosureScope;
import '../native_handler.dart' as native;
import '../elements/elements.dart';
import '../tree/tree.dart';
import '../util/util.dart' show Link;
import 'types.dart' show TypesInferrer, ConcreteType, ClassBaseType;

// BUG(8802): There's a bug in the analyzer that makes the re-export
// of Selector from dart2jslib.dart fail. For now, we work around that
// by importing universe.dart explicitly and disabling the re-export.
import '../dart2jslib.dart' hide Selector;
import '../universe/universe.dart' show Selector;

/**
 * A work queue that ensures there are no duplicates, and adds and
 * removes in LIFO.
 */
class WorkSet<E extends Element> {
  final List<E> queue = new List<E>();
  final Set<E> elementsInQueue = new Set<E>();

  void add(E element) {
    element = element.implementation;
    if (elementsInQueue.contains(element)) return;
    queue.add(element);
    elementsInQueue.add(element);
  }

  E remove() {
    E element = queue.removeLast();
    elementsInQueue.remove(element);
    return element;
  }

  bool get isEmpty => queue.isEmpty;
}

/**
 * Placeholder for type information of final fields of classes.
 */
class ClassInfoForFinalFields {
  /**
   * Maps a final field to a map from generative constructor to the
   * inferred type of the field in that generative constructor.
   */
  final Map<Element, Map<Element, Element>> typesOfFinalFields =
      new Map<Element, Map<Element, Element>>();

  /**
   * The number of generative constructors that need to be visited
   * before we can take any decision on the type of the fields.
   * Given that all generative constructors must be analyzed before
   * re-analyzing one, we know that once [constructorsToVisitCount]
   * reaches to 0, all generative constructors have been analyzed.
   */
  int constructorsToVisitCount;

  ClassInfoForFinalFields(this.constructorsToVisitCount);

  /**
   * Records that the generative [constructor] has inferred [type]
   * for the final [field].
   */
  void recordFinalFieldType(Element constructor, Element field, Element type) {
    Map<Element, Element> typesFor = typesOfFinalFields.putIfAbsent(
        field, () => new Map<Element, Element>());
    typesFor[constructor] = type;
  }

  /**
   * Records that [constructor] has been analyzed. If not at 0,
   * decrement [constructorsToVisitCount].
   */
  void doneAnalyzingGenerativeConstructor(Element constructor) {
    if (constructorsToVisitCount != 0) constructorsToVisitCount--;
  }

  /**
   * Returns whether all generative constructors of the class have
   * been analyzed.
   */
  bool get isDone => constructorsToVisitCount == 0;
}

class SimpleTypesInferrer extends TypesInferrer {
  /**
   * Maps an element to its callers.
   */
  final Map<Element, Set<Element>> callersOf =
      new Map<Element, Set<Element>>();

  /**
   * Maps an element to its return type.
   */
  final Map<Element, Element> returnTypeOf =
      new Map<Element, Element>();

  /**
   * Maps an element to its type.
   */
  final Map<Element, Element> typeOf = new Map<Element, Element>();

  /**
   * Maps an element to the number of times this type inferrer
   * analyzed it.
   */
  final Map<Element, int> analyzeCount = new Map<Element, int>();

  /**
   * Maps a class to a [ClassInfoForFinalFields] to help collect type
   * information of final fields.
   */
  final Map<ClassElement, ClassInfoForFinalFields> classInfoForFinalFields =
      new Map<ClassElement, ClassInfoForFinalFields>();

  /**
   * The work list of the inferrer.
   */
  final WorkSet<Element> workSet = new WorkSet<Element>();

  /**
   * Heuristic for avoiding too many re-analysis of an element.
   */
  final int MAX_ANALYSIS_COUNT_PER_ELEMENT = 5;

  /**
   * Sentinal used by the inferrer to notify that it gave up finding a type
   * on a specific element.
   */
  Element giveUpType;

  final Compiler compiler;

  // Times the computation of re-analysis of methods.
  final Stopwatch recomputeWatch = new Stopwatch();
  // Number of re-analysis.
  int recompiles = 0;

  SimpleTypesInferrer(this.compiler);

  /**
   * Main entry point of the inferrer. Analyzes all elements that the
   * resolver found as reachable. Returns whether it succeeded.
   */
  bool analyzeMain(Element element) {
    // We use the given element as the sentinel. This is a temporary
    // situation as long as this inferrer is using [ClassElement] for
    // expressing types.
    giveUpType = element;
    buildWorkQueue();
    int analyzed = 0;
    compiler.progress.reset();
    do {
      if (compiler.progress.elapsedMilliseconds > 500) {
        compiler.log('Inferred $analyzed methods.');
        compiler.progress.reset();
      }
      element = workSet.remove();
      if (element.isErroneous()) continue;

      bool wasAnalyzed = analyzeCount.containsKey(element);
      if (wasAnalyzed) {
        recompiles++;
        recomputeWatch.start();
      }
      bool changed = analyze(element);
      analyzed++;
      if (wasAnalyzed) {
        recomputeWatch.stop();
      }
      if (!changed) continue;
      // If something changed during the analysis of [element],
      // put back callers of it in the work list.
      Set<Element> methodCallers = callersOf[element];
      if (methodCallers != null) {
        methodCallers.forEach(enqueueAgain);
      }
    } while (!workSet.isEmpty);
    dump();
    clear();
    return true;
  }

  /**
   * Query method after the analysis to know the type of [element].
   */
  getConcreteReturnTypeOfElement(element) {
    return getTypeIfValuable(returnTypeOf[element]);
  }

  getConcreteTypeOfElement(element) {
    return getTypeIfValuable(typeOf[element]);
  }

  getTypeIfValuable(returnType) {
    if (returnType == null
        || returnType == compiler.dynamicClass
        || returnType == giveUpType) {
      return null;
    }
    return new ConcreteType.singleton(
        compiler.maxConcreteTypeSize, new ClassBaseType(returnType));
  }

  /**
   * Query method after the analysis to know the type of [node],
   * defined in the context of [owner].
   */
  getConcreteTypeOfNode(Element owner, Node node) {
    var elements = compiler.enqueuer.resolution.resolvedElements[owner];
    // TODO(ngeoffray): Not sure why the resolver would put a null
    // mapping.
    if (elements == null) return null;
    Selector selector = elements.getSelector(node);
    // TODO(ngeoffray): Should the builder call this method with a
    // SendSet?
    if (selector == null || selector.isSetter() || selector.isIndexSet()) {
      return null;
    }
    return getTypeIfValuable(returnTypeOfSelector(selector));
  }

  /**
   * Enqueues [e] in the work queue if it is valuable.
   */
  void enqueueAgain(Element e) {
    Element type = e.isField() ? typeOf[e] : returnTypeOf[e];
    // If we have found a type for [e], no need to re-analyze it.
    if (type != compiler.dynamicClass) return;
    if (analyzeCount[e] > MAX_ANALYSIS_COUNT_PER_ELEMENT) return;
    workSet.add(e);
  }

  /**
   * Builds the initial work queue by adding all resolved elements in
   * the work queue, ordered by the number of selectors they use. This
   * order is benficial for the analysis of return types, but we may
   * have to refine it once we analyze parameter types too.
   */
  void buildWorkQueue() {
    int max = 0;
    Map<int, Set<Element>> methodSizes = new Map<int, Set<Element>>();
    compiler.enqueuer.resolution.resolvedElements.forEach(
      (Element element, TreeElementMapping mapping) {
        // TODO(ngeoffray): Not sure why the resolver would put a null
        // mapping.
        if (mapping == null) return;
        if (element.isAbstract(compiler)) return;
        int length = mapping.selectors.length;
        max = length > max ? length : max;
        Set<Element> set = methodSizes.putIfAbsent(
            length, () => new Set<Element>());
        set.add(element);
    });
    
    // This iteration assumes the [WorkSet] is LIFO.
    for (int i = max; i >= 0; i--) {
      Set<Element> set = methodSizes[i];
      if (set != null) {
        set.forEach((e) { workSet.add(e); });
      }
    }

    // Build the [classInfoForFinalFields] map by iterating over all
    // seen classes and counting the number of their generative
    // constructors.
    // We iterate over the seen classes and not the instantiated ones,
    // because we also need to analyze the final fields of super
    // classes that are not instantiated.
    compiler.enqueuer.resolution.seenClasses.forEach((ClassElement cls) {
      int constructorCount = 0;
      cls.forEachMember((_, member) {
        if (member.isGenerativeConstructor()
            && compiler.enqueuer.resolution.isProcessed(member)) {
          constructorCount++;
        }
      });
      classInfoForFinalFields[cls.implementation] =
          new ClassInfoForFinalFields(constructorCount);
    });
  }

  dump() {
    int interestingTypes = 0;
    int giveUpTypes = 0;
    returnTypeOf.forEach((Element method, Element type) {
      if (type == giveUpType) {
        giveUpTypes++;
      } else if (type != compiler.nullClass && type != compiler.dynamicClass) {
        interestingTypes++;
      }
    });
    compiler.log('Type inferrer re-analyzed methods $recompiles times '
                 'in ${recomputeWatch.elapsedMilliseconds} ms.');
    compiler.log('Type inferrer found $interestingTypes interesting '
                 'return types and gave up on $giveUpTypes methods.');
  }

  /**
   * Clear data structures that are not used after the analysis.
   */
  void clear() {
    callersOf.clear();
    analyzeCount.clear();
    classInfoForFinalFields.clear();
  }

  bool analyze(Element element) {
    SimpleTypeInferrerVisitor visitor =
        new SimpleTypeInferrerVisitor(element, compiler, this);
    Element returnType = visitor.run();
    if (analyzeCount.containsKey(element)) {
      analyzeCount[element]++;
    } else {
      analyzeCount[element] = 1;
    }
    if (element.isGenerativeConstructor()) {
      // We always know the return type of a generative constructor.
      return false;
    } else if (element.isField()) {
      if (element.modifiers.isFinal() || element.modifiers.isConst()) {
        if (element.parseNode(compiler).asSendSet() != null) {
          // If [element] is final and has an initializer, we record
          // the inferred type.
          return recordType(element, returnType);
        }
      }
      // We don't record anything for non-final fields.
      return false;
    } else {
      return recordReturnType(element, returnType);
    }
  }

  bool recordType(analyzedElement, type) {
    return internalRecordType(analyzedElement, type, typeOf);
  }

  /**
   * Records [returnType] as the return type of [analyzedElement].
   * Returns whether the new type is worth recompiling the callers of
   * [analyzedElement].
   */
  bool recordReturnType(Element analyzedElement, returnType) {
    return internalRecordType(analyzedElement, returnType, returnTypeOf);
  }

  bool internalRecordType(analyzedElement, type, Map<Element, Element> types) {
    assert(type != null);
    Element existing = types[analyzedElement];
    Element newType = existing == compiler.dynamicClass
        ? type // Previous analysis did not find any type.
        : computeLUB(existing, type);
    types[analyzedElement] = newType;
    // If the return type is useful, say it has changed.
    return existing != newType
        && newType != compiler.dynamicClass
        && newType != compiler.nullClass;
  }

  /**
   * Returns the return type of [element]. Returns [:Dynamic:] if
   * [element] has not been analyzed yet.
   */
  ClassElement returnTypeOfElement(Element element) {
    element = element.implementation;
    if (element.isGenerativeConstructor()) return element.getEnclosingClass();
    Element returnType = returnTypeOf[element];
    if (returnType == null || returnType == giveUpType) {
      return compiler.dynamicClass;
    }
    assert(returnType != null);
    return returnType;
  }

  /**
   * Returns the type of [element]. Returns [:Dynamic:] if
   * [element] has not been analyzed yet.
   */
  ClassElement typeOfElement(Element element) {
    element = element.implementation;
    Element type = typeOf[element];
    if (type == null || type == giveUpType) {
      return compiler.dynamicClass;
    }
    assert(type != null);
    return type;
  }

  /**
   * Returns the union of the return types of all elements that match
   * the called [selector].
   */
  ClassElement returnTypeOfSelector(Selector selector) {
    ClassElement result;
    iterateOverElements(selector, (Element element) {
      assert(element.isImplementation);
      Element cls;
      if (selector.isGetter()) {
        if (element.isFunction()) {
          cls = compiler.functionClass;
        } else if (element.isField()) {
          cls = typeOf[element];
        } else if (element.isGetter()) {
          cls = returnTypeOf[element];
        }
      } else {
        cls = returnTypeOf[element];
      }
      if (cls == null
          || cls == compiler.dynamicClass
          || cls == giveUpType
          || (cls != result && result != null)) {
        result = compiler.dynamicClass;
        return false;
      } else {
        result = cls;
        return true;
      }
    });
    if (result == null) {
      result = compiler.dynamicClass;
    }
    return result;
  }

  bool isNotClosure(Element element) {
    // If the outermost enclosing element of [element] is [element]
    // itself, we know it cannot be a closure.
    Element outermost = element.getOutermostEnclosingMemberOrTopLevel();
    return outermost.declaration == element.declaration;
  }

  /**
   * Registers that [caller] calls [callee] with the given
   * [arguments].
   */
  void registerCalledElement(Element caller,
                             Element callee,
                             ArgumentsTypes arguments) {
    assert(isNotClosure(caller));
    if (analyzeCount.containsKey(caller)) return;
    callee = callee.implementation;
    Set<Element> callers = callersOf.putIfAbsent(
        callee, () => new Set<Element>());
    callers.add(caller);
  }

  /**
   * Registers that [caller] accesses [callee] through a property
   * access.
   */
  void registerGetterOnElement(Element caller,
                               Element callee) {
    assert(isNotClosure(caller));
    if (analyzeCount.containsKey(caller)) return;
    callee = callee.implementation;
    Set<Element> callers = callersOf.putIfAbsent(
        callee, () => new Set<Element>());
    callers.add(caller);
  }

  /**
   * Registers that [caller] calls an element matching [selector]
   * with the given [arguments].
   */
  void registerCalledSelector(Element caller,
                              Selector selector,
                              ArgumentsTypes arguments) {
    assert(isNotClosure(caller));
    if (analyzeCount.containsKey(caller)) return;
    iterateOverElements(selector, (Element element) {
      assert(element.isImplementation);
      Set<Element> callers = callersOf.putIfAbsent(
          element, () => new Set<Element>());
      callers.add(caller);
      return true;
    });
  }

  /**
   * Registers that [caller] accesses an element matching [selector]
   * through a property access.
   */
  void registerGetterOnSelector(Element caller, Selector selector) {
    assert(isNotClosure(caller));
    if (analyzeCount.containsKey(caller)) return;
    iterateOverElements(selector, (Element element) {
      assert(element.isImplementation);
      Set<Element> callers = callersOf.putIfAbsent(
          element, () => new Set<Element>());
      callers.add(caller);
      return true;
    });
  }

  /**
   * Registers that [caller] closurizes [function].
   */
  void registerGetFunction(Element caller, Element function) {
    assert(isNotClosure(caller));
    assert(caller.isImplementation);
    if (analyzeCount.containsKey(caller)) return;
    // We don't register that [caller] calls [function] because we
    // don't know if the code is going to call it, and if it is, then
    // the inferrer has lost track of its identity anyway.
  }

  /**
   * Applies [f] to all elements in the universe that match
   * [selector]. If [f] returns false, aborts the iteration.
   */
  void iterateOverElements(Selector selector, bool f(Element element)) {
    Iterable<Element> elements = compiler.world.allFunctions.filter(selector);
    for (Element e in elements) {
      if (!f(e.implementation)) return;
    }
  }

  /**
   * Records in [classInfoForFinalFields] that [constructor] has
   * inferred [type] for the final [field].
   */
  void recordFinalFieldType(Element constructor, Element field, Element type) {
    // If the field is being set at its declaration site, it is not
    // being tracked in the [classInfoForFinalFields] map.
    if (constructor == field) return;
    assert(field.modifiers.isFinal() || field.modifiers.isConst());
    ClassElement cls = constructor.getEnclosingClass();
    ClassInfoForFinalFields info = classInfoForFinalFields[cls.implementation];
    info.recordFinalFieldType(constructor, field, type);
  }

  /**
   * Records that we are done analyzing [constructor]. If all
   * generative constructors of its enclosing class have already been
   * analyzed, this method updates the types of final fields.
   */
  void doneAnalyzingGenerativeConstructor(Element constructor) {
    ClassElement cls = constructor.getEnclosingClass();
    ClassInfoForFinalFields info = classInfoForFinalFields[cls.implementation];
    info.doneAnalyzingGenerativeConstructor(constructor);
    if (info.isDone) {
      updateFieldTypes(info);
    }
  }

  /**
   * Updates types of final fields listed in [info].
   */
  void updateFieldTypes(ClassInfoForFinalFields info) {
    assert(info.isDone);
    info.typesOfFinalFields.forEach((Element field,
                                     Map<Element, Element> types) {
      assert(field.modifiers.isFinal());
      Element fieldType;
      types.forEach((_, type) {
        fieldType = computeLUB(fieldType, type);
      });
      typeOf[field] = fieldType;
    });
  }

  /**
   * Returns the least upper bound between [firstType] and
   * [secondType].
   */
  Element computeLUB(Element firstType, Element secondType) {
    bool isNumber(type) {
      return type == compiler.numClass
          || type == compiler.doubleClass
          || type == compiler.intClass;
    }
    assert(secondType != null);
    if (firstType == null) {
      return secondType;
    } else if (firstType == giveUpType) {
      return firstType;
    } else if (secondType == compiler.dynamicClass) {
      return secondType;
    } else if (firstType == compiler.dynamicClass) {
      return firstType;
    } else if (firstType != secondType) {
      if (isNumber(firstType) && isNumber(secondType)) {
        // The JavaScript backend knows how to deal with numbers.
        return compiler.numClass;
      }
      // TODO(ngeoffray): Actually compute the least upper bound.
      return giveUpType;
    } else {
      assert(firstType == secondType);
      return firstType;
    }
  }
}

/**
 * Placeholder for inferred arguments types on sends.
 */
class ArgumentsTypes {
  final List<Element> positional;
  final Map<Identifier, Element> named;
  ArgumentsTypes(this.positional, this.named);
  int get length => positional.length + named.length;
  toString() => "{ positional = $positional, named = $named }";
}

/**
 * Placeholder for inferred types of local variables.
 */
class LocalsHandler {
  final SimpleTypesInferrer inferrer;
  final Map<Element, Element> locals;
  final Set<Element> capturedAndBoxed;
  final bool inTryBlock;

  LocalsHandler(this.inferrer)
      : locals = new Map<Element, Element>(),
        capturedAndBoxed = new Set<Element>(),
        inTryBlock = false;
  LocalsHandler.from(LocalsHandler other, {bool inTryBlock: false})
      : locals = new Map<Element, Element>.from(other.locals),
        capturedAndBoxed = new Set<Element>.from(other.capturedAndBoxed),
        inTryBlock = other.inTryBlock || inTryBlock,
        inferrer = other.inferrer;

  Element use(Element local) {
    if (capturedAndBoxed.contains(local)) {
      return inferrer.typeOfElement(local);
    }
    return locals[local];
  }

  void update(Element local, Element type) {
    assert(type != null);
    if (capturedAndBoxed.contains(local) || inTryBlock) {
      // If a local is captured and boxed, or is set in a try block,
      // we compute the LUB of its assignments.
      //
      // We don't know if an assignment in a try block
      // will be executed, so all assigments in that block are
      // potential types after we have left it.
      type = inferrer.computeLUB(locals[local], type);
      if (type == inferrer.giveUpType) type = inferrer.compiler.dynamicClass;
    }
    locals[local] = type;
  }

  void setCapturedAndBoxed(Element local) {
    capturedAndBoxed.add(local);
  }

  /**
   * Merge handlers [first] and [second] into [:this:] and returns
   * whether the merge changed one of the variables types in [first].
   */
  bool merge(LocalsHandler other) {
    bool changed = false;
    List<Element> toRemove = <Element>[];
    // Iterating over a map and just updating its entries is OK.
    locals.forEach((local, oldType) {
      Element otherType = other.locals[local];
      if (otherType == null) {
        if (!capturedAndBoxed.contains(local)) {
          // If [local] is not in the other map and is not captured
          // and boxed, we know it is not a
          // local we want to keep. For example, in an if/else, we don't
          // want to keep variables declared in the if or in the else
          // branch at the merge point.
          toRemove.add(local);
        }
        return;
      }
      var type = inferrer.computeLUB(oldType, otherType);
      if (type == inferrer.giveUpType) type = inferrer.compiler.dynamicClass;
      if (type != oldType) changed = true;
      locals[local] = type;
    });

    // Remove locals that will not be used anymore.
    toRemove.forEach((Element element) {
      locals.remove(element);
    });

    // Update the locals that are captured and boxed. We
    // unconditionally add them to [this] because we register the type
    // of boxed variables after analyzing all closures.
    other.capturedAndBoxed.forEach((Element element) {
      capturedAndBoxed.add(element);
      // If [element] is not in our [locals], we need to update it.
      // Otherwise, we have already computed the LUB of it.
      if (locals[element] == null) {
        locals[element] = other.locals[element];
      }
    });
    return changed;
  }
}

class SimpleTypeInferrerVisitor extends ResolvedVisitor {
  final Element analyzedElement;
  final Element outermostElement;
  final SimpleTypesInferrer inferrer;
  final Compiler compiler;
  LocalsHandler locals;
  Element returnType;

  SimpleTypeInferrerVisitor.internal(TreeElements mapping,
                                     this.analyzedElement,
                                     this.outermostElement,
                                     this.inferrer,
                                     this.compiler,
                                     this.locals)
    : super(mapping);

  factory SimpleTypeInferrerVisitor(Element element,
                                    Compiler compiler,
                                    SimpleTypesInferrer inferrer,
                                    [LocalsHandler handler]) {
    Element outermostElement =
        element.getOutermostEnclosingMemberOrTopLevel().implementation;
    TreeElements elements = compiler.enqueuer.resolution.resolvedElements[
        outermostElement.declaration];
    assert(elements != null);
    assert(outermostElement != null);
    handler = handler != null ? handler : new LocalsHandler(inferrer);
    return new SimpleTypeInferrerVisitor.internal(
        elements, element, outermostElement, inferrer, compiler, handler);
  }

  Element run() {
    var node = analyzedElement.parseNode(compiler);
    if (analyzedElement.isField() && node.asSendSet() == null) {
      // Eagerly bailout, because computing the closure data only
      // works for functions and field assignments.
      return compiler.dynamicClass;
    }
    // Update the locals that are boxed in [locals]. These locals will
    // be handled specially, in that we are computing their LUB at
    // each update, and reading them yields the type that was found in a
    // previous analysis ouf [outermostElement].
    ClosureClassMap closureData =
        compiler.closureToClassMapper.computeClosureToClassMapping(
            analyzedElement, node, elements);
    ClosureScope scopeData = closureData.capturingScopes[node];
    if (scopeData != null) {
      scopeData.capturedVariableMapping.forEach((Element variable, _) {
        locals.setCapturedAndBoxed(variable);
      });
    }
    if (analyzedElement.isField()) {
      returnType = visit(node.asSendSet().arguments.head);
    } else if (analyzedElement.isGenerativeConstructor()) {
      FunctionElement function = analyzedElement;
      FunctionSignature signature = function.computeSignature(compiler);
      signature.forEachParameter((element) {
        // We don't track argument types yet, so just set the fields
        // and parameters as dynamic.
        if (element.kind == ElementKind.FIELD_PARAMETER
            && element.fieldElement.modifiers.isFinal()) {
          inferrer.recordFinalFieldType(
              analyzedElement, element.fieldElement, compiler.dynamicClass);
        } else {
          locals.update(element, compiler.dynamicClass);
        }
      });
      visit(node.initializers);
      visit(node.body);
      inferrer.doneAnalyzingGenerativeConstructor(analyzedElement);
      returnType = analyzedElement.getEnclosingClass();
    } else if (analyzedElement.isNative()) {
      // Native methods do not have a body, and we currently just say
      // they return dynamic.
      returnType = compiler.dynamicClass;
    } else {
      FunctionElement function = analyzedElement;
      FunctionSignature signature = function.computeSignature(compiler);
      signature.forEachParameter((element) {
        // We don't track argument types yet, so just set the
        // parameters as dynamic.
        locals.update(element, compiler.dynamicClass);
      });
      visit(node.body);
      if (returnType == null) {
        // No return in the body.
        returnType = compiler.nullClass;
      }
    }

    if (analyzedElement == outermostElement) {
      bool changed = false;
      locals.capturedAndBoxed.forEach((Element local) {
        if (inferrer.recordType(local, locals.locals[local])) {
          changed = true;
        }
      });
      // TODO(ngeoffray): Re-analyze method if [changed]?
    }

    return returnType;
  }

  recordReturnType(ClassElement cls) {
    returnType = inferrer.computeLUB(returnType, cls);
  }

  visitNode(Node node) {
    node.visitChildren(this);
    return compiler.dynamicClass;
  }

  visitNewExpression(NewExpression node) {
    return node.send.accept(this);
  }

  visit(Node node) {
    return node == null ? compiler.dynamicClass : node.accept(this);
  }

  visitFunctionExpression(FunctionExpression node) {
    Element element = elements[node];
    // We don't put the closure in the work queue of the
    // inferrer, because it will share information with its enclosing
    // method, like for example the types of local variables.
    LocalsHandler closureLocals = new LocalsHandler.from(locals);
    SimpleTypeInferrerVisitor visitor = new SimpleTypeInferrerVisitor(
        element, compiler, inferrer, closureLocals);
    visitor.run();
    inferrer.recordReturnType(element, visitor.returnType);
    locals.merge(visitor.locals);

    // Record the types of captured non-boxed variables. Types of
    // these variables may already be there, because of an analysis of
    // a previous closure. Note that analyzing the same closure multiple
    // times closure will refine the type of those variables, therefore
    // [:inferrer.typeOf[variable]:] is not necessarilly null, nor the
    // same as [newType].
    ClosureClassMap nestedClosureData =
        compiler.closureToClassMapper.getMappingForNestedFunction(node);
    nestedClosureData.forEachNonBoxedCapturedVariable((Element variable) {
      // The type may be null for instance contexts (this and type
      // parameters), as well as captured argument checks.
      if (locals.locals[variable] == null) return;
      inferrer.recordType(variable, locals.locals[variable]);
      assert(inferrer.typeOf[variable] != inferrer.giveUpType);
    });

    return compiler.functionClass;
  }

  visitFunctionDeclaration(FunctionDeclaration node) {
    locals.update(elements[node], compiler.functionClass);
    return visit(node.function);
  }

  visitLiteralString(LiteralString node) {
    return compiler.stringClass;
  }

  visitStringInterpolation(StringInterpolation node) {
    return compiler.stringClass;
  }

  visitStringJuxtaposition(StringJuxtaposition node) {
    return compiler.stringClass;
  }

  visitLiteralBool(LiteralBool node) {
    return compiler.boolClass;
  }

  visitLiteralDouble(LiteralDouble node) {
    return compiler.doubleClass;
  }

  visitLiteralInt(LiteralInt node) {
    return compiler.intClass;
  }

  visitLiteralList(LiteralList node) {
    return compiler.listClass;
  }

  visitLiteralMap(LiteralMap node) {
    return compiler.mapClass;
  }

  visitLiteralNull(LiteralNull node) {
    return compiler.nullClass;
  }

  visitTypeReferenceSend(Send node) {
    return compiler.typeClass;
  }

  visitSendSet(SendSet node) {
    Element element = elements[node];
    if (!Elements.isUnresolved(element) && element.impliesType()) {
      node.visitChildren(this);
      return compiler.dynamicClass;
    }

    Selector getterSelector =
        elements.getGetterSelectorInComplexSendSet(node);
    Selector operatorSelector = 
        elements.getOperatorSelectorInComplexSendSet(node);
    Selector setterSelector = elements.getSelector(node);

    String op = node.assignmentOperator.source.stringValue;
    bool isIncrementOrDecrement = op == '++' || op == '--';

    Element receiverType = element != null
        ? compiler.dynamicClass
        : visit(node.receiver);
      
    Element rhsType = isIncrementOrDecrement
        ? compiler.intClass
        : node.isIndex
            ? visit(node.arguments.tail.head)
            : visit(node.arguments.head);

    if (node.isIndex) {
      Element index = visit(node.arguments.head);
      if (op == '=') {
        // [: foo[0] = 42 :]
        handleDynamicSend(
            setterSelector,
            receiverType,
            new ArgumentsTypes([index, rhsType], null));
        return rhsType;
      } else {
        // [: foo[0] += 42 :] or [: foo[0]++ :].
        Element getterType = handleDynamicSend(
            getterSelector,
            receiverType,
            new ArgumentsTypes([index], null));
        Element returnType = handleDynamicSend(
            operatorSelector,
            getterType,
            new ArgumentsTypes([rhsType], null));
        handleDynamicSend(
            setterSelector,
            receiverType,
            new ArgumentsTypes([index, returnType], null));

        if (node.isPostfix) {
          return getterType;
        } else {
          return returnType;
        }
      }
    } else if (op == '=') {
      // [: foo = 42 :] or [: foo.bar = 42 :].
      if (Elements.isUnresolved(element) || element.isSetter()) {
        handleDynamicSend(
            setterSelector, receiverType, new ArgumentsTypes([rhsType], null));
      } else if (element.isField()) {
        if (element.modifiers.isFinal()) {
          inferrer.recordFinalFieldType(outermostElement, element, rhsType);
        }
      } else if (Elements.isLocal(element)) {
        locals.update(element, rhsType);
      }
      return rhsType;
    } else {
      // [: foo++ :] or [: foo += 1 :].
      Element getterType;
      Element newType;
      ArgumentsTypes operatorArguments = new ArgumentsTypes([rhsType], null);
      if (Elements.isUnresolved(element) || element.isSetter()) {
        getterType = handleDynamicSend(
            getterSelector, receiverType, null);
        newType = handleDynamicSend(
            operatorSelector, getterType, operatorArguments);
        handleDynamicSend(
            setterSelector, receiverType, new ArgumentsTypes([newType], null));
      } else if (element.isField()) {
        assert(!element.modifiers.isFinal());
        getterType = compiler.dynamicClass; // The type of the field.
        newType = handleDynamicSend(
            operatorSelector, getterType, operatorArguments);
      } else if (Elements.isLocal(element)) {
        getterType = locals.use(element);
        newType = handleDynamicSend(
            operatorSelector, getterType, operatorArguments);
        locals.update(element, newType);
      } else {
        // Bogus SendSet, for example [: myMethod += 42 :].
        getterType = compiler.dynamicClass;
        newType = handleDynamicSend(
            operatorSelector, getterType, operatorArguments);
      }

      if (node.isPostfix) {
        return getterType;
      } else {
        return newType;
      }
    }
  }

  visitIdentifier(Identifier node) {
    if (node.isThis() || node.isSuper()) {
      // TODO(ngeoffray): Represent subclasses.
      return compiler.dynamicClass;
    }
    return compiler.dynamicClass;
  }

  visitSuperSend(Send node) {
    Element element = elements[node];
    if (Elements.isUnresolved(element)) {
      return compiler.dynamicClass;
    }
    Selector selector = elements.getSelector(node);
    if (node.isPropertyAccess) {
      inferrer.registerGetterOnElement(outermostElement, element);
      return inferrer.typeOfElement(element);
    } else if (element.isFunction()) {
      ArgumentsTypes arguments = analyzeArguments(node.arguments);
      inferrer.registerCalledElement(outermostElement, element, arguments);
      return inferrer.returnTypeOfElement(element);
    } else {
      analyzeArguments(node.arguments);
      // Closure call on a getter. We don't have function types yet,
      // so we just return [:Dynamic:].
      return compiler.dynamicClass;
    }
  }

  visitStaticSend(Send node) {
    Element element = elements[node];
    if (Elements.isUnresolved(element)) {
      return compiler.dynamicClass;
    }
    if (element.isForeign(compiler)) {
      return handleForeignSend(node);
    }
    ArgumentsTypes arguments = analyzeArguments(node.arguments);
    inferrer.registerCalledElement(outermostElement, element, arguments);
    return inferrer.returnTypeOfElement(element);
  }

  handleForeignSend(Send node) {
    node.visitChildren(this);
    Selector selector = elements.getSelector(node);
    SourceString name = selector.name;
    if (name == const SourceString('JS')) {
      native.NativeBehavior nativeBehavior =
          compiler.enqueuer.resolution.nativeEnqueuer.getNativeBehaviorOf(node);
      if (nativeBehavior == null) return compiler.dynamicClass;
      List typesReturned = nativeBehavior.typesReturned;
      if (typesReturned.isEmpty) return compiler.dynamicClass;
      ClassElement returnType;
      for (var type in typesReturned) {
        ClassElement mappedType;
        if (type == native.SpecialType.JsObject) {
          mappedType = compiler.objectClass;
        } else if (type == native.SpecialType.JsArray) {
          mappedType = compiler.listClass;
        } else {
          mappedType = type.element;
          // For primitive types, we know how to handle them here and
          // in the backend.
          if (mappedType != compiler.stringClass
              && mappedType != compiler.intClass
              && mappedType != compiler.doubleClass
              && mappedType != compiler.boolClass
              && mappedType != compiler.numClass) {
            Set<ClassElement> subtypes = compiler.world.subtypes[mappedType];
            // TODO(ngeoffray): Handle subtypes and subclasses.
            if (subtypes != null && !subtypes.isEmpty) {
              return compiler.dynamicClass;
            }
          }
        }
        if (returnType == null) {
          returnType = mappedType;
        } else {
          return compiler.dynamicClass;
        }
      }
      return returnType;
    } else if (name == const SourceString('JS_OPERATOR_IS_PREFIX')) {
      return compiler.stringClass;
    } else {
      return compiler.dynamicClass;
    }
  }

  analyzeArguments(Link<Node> arguments) {
    List<ClassElement> positional = [];
    Map<Identifier, ClassElement> named = new Map<Identifier, ClassElement>();
    for (Node argument in arguments) {
      NamedArgument namedArgument = argument.asNamedArgument();
      if (namedArgument != null) {
        named[namedArgument.name] = namedArgument.expression.accept(this);
      } else {
        positional.add(argument.accept(this));
      }
    }
    return new ArgumentsTypes(positional, named);
  }

  visitOperatorSend(Send node) {
    Operator op = node.selector;
    if (const SourceString("[]") == op.source) {
      return visitDynamicSend(node);
    } else if (const SourceString("&&") == op.source ||
               const SourceString("||") == op.source) {
      visit(node.receiver);
      LocalsHandler saved = new LocalsHandler.from(locals);
      visit(node.arguments.head);
      saved.merge(locals);
      locals = saved;
      return compiler.boolClass;
    } else if (const SourceString("!") == op.source) {
      node.visitChildren(this);
      return compiler.boolClass;
    } else if (const SourceString("is") == op.source) {
      node.visitChildren(this);
      return compiler.boolClass;
    } else if (const SourceString("as") == op.source) {
      node.visitChildren(this);
      return compiler.dynamicClass;
    } else if (node.isParameterCheck) {
      node.visitChildren(this);
      return compiler.boolClass;
    } else if (node.argumentsNode is Prefix) {
      // Unary operator.
      return visitDynamicSend(node);
    } else if (const SourceString('===') == op.source
               || const SourceString('!==') == op.source) {
      node.visitChildren(this);
      return compiler.boolClass;
    } else {
      // Binary operator.
      return visitDynamicSend(node);
    }
  }

  // Because some nodes just visit their children, we may end up
  // visiting a type annotation, that may contain a send in case of a
  // prefixed type. Therefore we explicitly visit the type annotation
  // to avoid confusing the [ResolvedVisitor].
  visitTypeAnnotation(TypeAnnotation node) {}

  visitGetterSend(Send node) {
    Element element = elements[node];
    if (Elements.isStaticOrTopLevelField(element)) {
      inferrer.registerGetterOnElement(outermostElement, element);
      return inferrer.typeOfElement(element);
    } else if (Elements.isInstanceSend(node, elements)) {
      ClassElement receiverType;
      if (node.receiver == null) {
        receiverType = outermostElement.getEnclosingClass();
      } else {
        receiverType = node.receiver.accept(this);
      }
      Selector selector = elements.getSelector(node);
      return handleDynamicSend(selector, receiverType, null);
    } else if (Elements.isStaticOrTopLevelFunction(element)) {
      inferrer.registerGetFunction(outermostElement, element);
      return compiler.functionClass;
    } else if (Elements.isErroneousElement(element)) {
      return compiler.dynamicClass;
    } else if (Elements.isLocal(element)) {
      assert(locals.use(element) != null);
      return locals.use(element);
    } else {
      node.visitChildren(this);
      return compiler.dynamicClass;
    }
  }

  visitClosureSend(Send node) {
    node.visitChildren(this);
    Element element = elements[node];
    if (element != null && element.isFunction()) {
      assert(Elements.isLocal(element));
      // This only works for function statements. We need a
      // more sophisticated type system with function types to support
      // more.
      return inferrer.returnTypeOfElement(element);
    }
    return compiler.dynamicClass;
  }

  handleDynamicSend(Selector selector,
                    Element receiver,
                    ArgumentsTypes arguments) {
    if (selector.isGetter()) {
      assert(arguments == null);
      inferrer.registerGetterOnSelector(outermostElement, selector);
    } else if (selector.isSetter()) {
      // TODO(ngeoffray): Register called setter.
    } else {
      inferrer.registerCalledSelector(outermostElement, selector, arguments);
    }
    return inferrer.returnTypeOfSelector(selector);
  }

  visitDynamicSend(Send node) {
    ClassElement receiverType;
    if (node.receiver == null) {
      receiverType = outermostElement.getEnclosingClass();
    } else {
      receiverType = visit(node.receiver);
    }
    ArgumentsTypes arguments = analyzeArguments(node.arguments);
    Selector selector = elements.getSelector(node);
    return handleDynamicSend(selector, receiverType, arguments);
  }

  visitReturn(Return node) {
    Node expression = node.expression;
    recordReturnType(expression == null
        ? compiler.nullClass
        : expression.accept(this));
  }

  visitConditional(Conditional node) {
    node.condition.accept(this);
    LocalsHandler saved = new LocalsHandler.from(locals);
    Element firstType = node.thenExpression.accept(this);
    LocalsHandler thenLocals = locals;
    locals = saved;
    Element secondType = node.elseExpression.accept(this);
    locals.merge(thenLocals);
    Element type = inferrer.computeLUB(firstType, secondType);
    if (type == inferrer.giveUpType) type = compiler.dynamicClass;
    return type;
  }

  visitVariableDefinitions(VariableDefinitions node) {
    for (Link<Node> link = node.definitions.nodes;
         !link.isEmpty;
         link = link.tail) {
      Node definition = link.head;
      if (definition is Identifier) {
        locals.update(elements[definition], compiler.nullClass);
      } else {
        assert(definition.asSendSet() != null);
        visit(definition);
      }
    }
  }

  visitIf(If node) {
    visit(node.condition);
    LocalsHandler saved = new LocalsHandler.from(locals);
    visit(node.thenPart);
    LocalsHandler thenLocals = locals;
    locals = saved;
    visit(node.elsePart);
    locals.merge(thenLocals);
    return compiler.dynamicClass;
  }

  visitWhile(While node) {
    bool changed = false;
    do {
      LocalsHandler saved = new LocalsHandler.from(locals);
      visit(node.condition);
      visit(node.body);
      changed = saved.merge(locals);
      locals = saved;
    } while (changed);

    return compiler.dynamicClass;
  }

  visitDoWhile(DoWhile node) {
    bool changed = false;
    do {
      LocalsHandler saved = new LocalsHandler.from(locals);
      visit(node.body);
      visit(node.condition);
      changed = saved.merge(locals);
      locals = saved;
    } while (changed);

    return compiler.dynamicClass;
  }

  visitFor(For node) {
    bool changed = false;
    visit(node.initializer);
    do {
      LocalsHandler saved = new LocalsHandler.from(locals);
      visit(node.condition);
      visit(node.body);
      visit(node.update);
      changed = saved.merge(locals);
      locals = saved;
    } while (changed);

    return compiler.dynamicClass;
  }

  visitForIn(ForIn node) {
    bool changed = false;
    visit(node.expression);
    Element variable;
    if (node.declaredIdentifier.asSend() != null) {
      variable = elements[node.declaredIdentifier];
    } else {
      assert(node.declaredIdentifier.asVariableDefinitions() != null);
      VariableDefinitions variableDefinitions = node.declaredIdentifier;
      variable = elements[variableDefinitions.definitions.nodes.head];
    }
    locals.update(variable, compiler.dynamicClass);
    do {
      LocalsHandler saved = new LocalsHandler.from(locals);
      visit(node.body);
      changed = saved.merge(locals);
      locals = saved;
    } while (changed);

    return compiler.dynamicClass;
  }

  visitTryStatement(TryStatement node) {
    LocalsHandler saved = locals;
    locals = new LocalsHandler.from(locals, inTryBlock: true);
    visit(node.tryBlock);
    saved.merge(locals);
    locals = saved;
    for (Node catchBlock in node.catchBlocks) {
      saved = new LocalsHandler.from(locals);
      visit(catchBlock);
      saved.merge(locals);
      locals = saved;
    }
    visit(node.finallyBlock);
  }

  internalError(String reason, {Node node}) {
    compiler.internalError(reason, node: node);
  }
}
