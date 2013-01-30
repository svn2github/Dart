// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of dart2js;

class TypeCheckerTask extends CompilerTask {
  TypeCheckerTask(Compiler compiler) : super(compiler);
  String get name => "Type checker";

  static const bool LOG_FAILURES = false;

  void check(Node tree, TreeElements elements) {
    measure(() {
      Visitor visitor =
          new TypeCheckerVisitor(compiler, elements, compiler.types);
      try {
        tree.accept(visitor);
      } on CancelTypeCheckException catch (e) {
        if (LOG_FAILURES) {
          // Do not warn about unimplemented features; log message instead.
          compiler.log("'${e.node}': ${e.reason}");
        }
      }
    });
  }
}

class CancelTypeCheckException {
  final Node node;
  final String reason;

  CancelTypeCheckException(this.node, this.reason);
}

class TypeCheckerVisitor implements Visitor<DartType> {
  final Compiler compiler;
  final TreeElements elements;
  final Types types;

  Node lastSeenNode;
  DartType expectedReturnType;
  ClassElement currentClass;

  Link<DartType> cascadeTypes = const Link<DartType>();

  DartType intType;
  DartType doubleType;
  DartType boolType;
  DartType stringType;
  DartType objectType;
  DartType listType;

  TypeCheckerVisitor(this.compiler, this.elements, this.types) {
    intType = compiler.intClass.computeType(compiler);
    doubleType = compiler.doubleClass.computeType(compiler);
    boolType = compiler.boolClass.computeType(compiler);
    stringType = compiler.stringClass.computeType(compiler);
    objectType = compiler.objectClass.computeType(compiler);
    listType = compiler.listClass.computeType(compiler);
  }

  DartType fail(node, [reason]) {
    String message = 'cannot type-check';
    if (reason != null) {
      message = '$message: $reason';
    }
    throw new CancelTypeCheckException(node, message);
  }

  reportTypeWarning(Node node, MessageKind kind, [Map arguments = const {}]) {
    compiler.reportWarning(node, new TypeWarning(kind, arguments));
  }

  // TODO(karlklose): remove these functions.
  DartType unhandledStatement() => StatementType.NOT_RETURNING;
  DartType unhandledExpression() => types.dynamicType;

  DartType analyzeNonVoid(Node node) {
    DartType type = analyze(node);
    if (type == types.voidType) {
      reportTypeWarning(node, MessageKind.VOID_EXPRESSION);
    }
    return type;
  }

  DartType analyzeWithDefault(Node node, DartType defaultValue) {
    return node != null ? analyze(node) : defaultValue;
  }

  DartType analyze(Node node) {
    if (node == null) {
      final String error = 'internal error: unexpected node: null';
      if (lastSeenNode != null) {
        fail(null, error);
      } else {
        compiler.cancel(error);
      }
    } else {
      lastSeenNode = node;
    }
    DartType result = node.accept(this);
    // TODO(karlklose): record type?
    if (result == null) {
      fail(node, 'internal error: type is null');
    }
    return result;
  }

  /**
   * Check if a value of type t can be assigned to a variable,
   * parameter or return value of type s.
   */
  checkAssignable(Node node, DartType s, DartType t) {
    if (!types.isAssignable(s, t)) {
      reportTypeWarning(node, MessageKind.NOT_ASSIGNABLE,
                        {'fromType': s, 'toType': t});
    }
  }

  checkCondition(Expression condition) {
    checkAssignable(condition, boolType, analyze(condition));
  }

  void pushCascadeType(DartType type) {
    cascadeTypes = cascadeTypes.prepend(type);
  }

  DartType popCascadeType() {
    DartType type = cascadeTypes.head;
    cascadeTypes = cascadeTypes.tail;
    return type;
  }

  DartType visitBlock(Block node) {
    return analyze(node.statements);
  }

  DartType visitCascade(Cascade node) {
    analyze(node.expression);
    return popCascadeType();
  }

  DartType visitCascadeReceiver(CascadeReceiver node) {
    DartType type = analyze(node.expression);
    pushCascadeType(type);
    return type;
  }

  DartType visitClassNode(ClassNode node) {
    fail(node);
  }

  DartType visitMixinApplication(MixinApplication node) {
    fail(node);
  }

  DartType visitNamedMixinApplication(NamedMixinApplication node) {
    fail(node);
  }

  DartType visitDoWhile(DoWhile node) {
    StatementType bodyType = analyze(node.body);
    checkCondition(node.condition);
    return bodyType.join(StatementType.NOT_RETURNING);
  }

  DartType visitExpressionStatement(ExpressionStatement node) {
    analyze(node.expression);
    return StatementType.NOT_RETURNING;
  }

  /** Dart Programming Language Specification: 11.5.1 For Loop */
  DartType visitFor(For node) {
    analyzeWithDefault(node.initializer, StatementType.NOT_RETURNING);
    checkCondition(node.condition);
    analyzeWithDefault(node.update, StatementType.NOT_RETURNING);
    StatementType bodyType = analyze(node.body);
    return bodyType.join(StatementType.NOT_RETURNING);
  }

  DartType visitFunctionDeclaration(FunctionDeclaration node) {
    analyze(node.function);
    return StatementType.NOT_RETURNING;
  }

  DartType visitFunctionExpression(FunctionExpression node) {
    DartType type;
    DartType returnType;
    DartType previousType;
    final FunctionElement element = elements[node];
    if (Elements.isUnresolved(element)) return types.dynamicType;
    if (identical(element.kind, ElementKind.GENERATIVE_CONSTRUCTOR) ||
        identical(element.kind, ElementKind.GENERATIVE_CONSTRUCTOR_BODY)) {
      type = types.dynamicType;
      returnType = types.voidType;
    } else {
      FunctionType functionType = computeType(element);
      returnType = functionType.returnType;
      type = functionType;
    }
    DartType previous = expectedReturnType;
    expectedReturnType = returnType;
    if (element.isMember()) currentClass = element.getEnclosingClass();
    StatementType bodyType = analyze(node.body);
    if (returnType != types.voidType && returnType != types.dynamicType
        && bodyType != StatementType.RETURNING) {
      MessageKind kind;
      if (bodyType == StatementType.MAYBE_RETURNING) {
        kind = MessageKind.MAYBE_MISSING_RETURN;
      } else {
        kind = MessageKind.MISSING_RETURN;
      }
      reportTypeWarning(node.name, kind);
    }
    expectedReturnType = previous;
    return type;
  }

  DartType visitIdentifier(Identifier node) {
    if (node.isThis()) {
      return currentClass.computeType(compiler);
    } else {
      // This is an identifier of a formal parameter.
      return types.dynamicType;
    }
  }

  DartType visitIf(If node) {
    checkCondition(node.condition);
    StatementType thenType = analyze(node.thenPart);
    StatementType elseType = node.hasElsePart ? analyze(node.elsePart)
                                              : StatementType.NOT_RETURNING;
    return thenType.join(elseType);
  }

  DartType visitLoop(Loop node) {
    return unhandledStatement();
  }

  DartType lookupMethodType(Node node, ClassElement classElement,
                            SourceString name) {
    Element member = classElement.lookupLocalMember(name);
    if (member == null) {
      classElement.ensureResolved(compiler);
      for (Link<DartType> supertypes = classElement.allSupertypes;
           !supertypes.isEmpty && member == null;
           supertypes = supertypes.tail) {
        ClassElement lookupTarget = supertypes.head.element;
        member = lookupTarget.lookupLocalMember(name);
      }
    }
    if (member != null && member.kind == ElementKind.FUNCTION) {
      return computeType(member);
    }
    reportTypeWarning(node, MessageKind.METHOD_NOT_FOUND,
                      {'className': classElement.name, 'methodName': name});
    return types.dynamicType;
  }

  void analyzeArguments(Send send, DartType type) {
    Link<Node> arguments = send.arguments;
    if (type == null || identical(type, types.dynamicType)) {
      while(!arguments.isEmpty) {
        analyze(arguments.head);
        arguments = arguments.tail;
      }
    } else {
      FunctionType funType = type;
      Link<DartType> parameterTypes = funType.parameterTypes;
      while (!arguments.isEmpty && !parameterTypes.isEmpty) {
        checkAssignable(arguments.head, parameterTypes.head,
                        analyze(arguments.head));
        arguments = arguments.tail;
        parameterTypes = parameterTypes.tail;
      }
      if (!arguments.isEmpty) {
        reportTypeWarning(arguments.head, MessageKind.ADDITIONAL_ARGUMENT);
      } else if (!parameterTypes.isEmpty) {
        reportTypeWarning(send, MessageKind.MISSING_ARGUMENT,
                          {'argumentType': parameterTypes.head});
      }
    }
  }

  DartType visitSend(Send node) {
    Element element = elements[node];

    if (Elements.isClosureSend(node, element)) {
      // TODO(karlklose): Finish implementation.
      return types.dynamicType;
    }

    Identifier selector = node.selector.asIdentifier();
    String name = selector.source.stringValue;

    if (node.isOperator && identical(name, 'is')) {
      analyze(node.receiver);
      return boolType;
    } else if (node.isOperator) {
      final Node firstArgument = node.receiver;
      final DartType firstArgumentType = analyze(node.receiver);
      final arguments = node.arguments;
      final Node secondArgument = arguments.isEmpty ? null : arguments.head;
      final DartType secondArgumentType =
          analyzeWithDefault(secondArgument, null);

      if (identical(name, '+') || identical(name, '=') || identical(name, '-')
          || identical(name, '*') || identical(name, '/') || identical(name, '%')
          || identical(name, '~/') || identical(name, '|') || identical(name, '&')
          || identical(name, '^') || identical(name, '~')|| identical(name, '<<')
          || identical(name, '>>') || identical(name, '[]')) {
        return types.dynamicType;
      } else if (identical(name, '<') || identical(name, '>') || identical(name, '<=')
                 || identical(name, '>=') || identical(name, '==') || identical(name, '!=')
                 || identical(name, '===') || identical(name, '!==')) {
        return boolType;
      } else if (identical(name, '||') || identical(name, '&&') || identical(name, '!')) {
        checkAssignable(firstArgument, boolType, firstArgumentType);
        if (!arguments.isEmpty) {
          // TODO(karlklose): check number of arguments in validator.
          checkAssignable(secondArgument, boolType, secondArgumentType);
        }
        return boolType;
      }
      fail(selector, 'unexpected operator ${name}');

    } else if (node.isPropertyAccess) {
      if (node.receiver != null) {
        // TODO(karlklose): we cannot handle fields.
        return unhandledExpression();
      }
      if (element == null) return types.dynamicType;
      return computeType(element);

    } else if (node.isFunctionObjectInvocation) {
      fail(node.receiver, 'function object invocation unimplemented');

    } else {
      FunctionType computeFunType() {
        if (node.receiver != null) {
          DartType receiverType = analyze(node.receiver);
          if (receiverType.element == compiler.dynamicClass) return null;
          if (receiverType == null) {
            fail(node.receiver, 'receivertype is null');
          }
          if (identical(receiverType.element.kind, ElementKind.GETTER)) {
            FunctionType getterType  = receiverType;
            receiverType = getterType.returnType;
          }
          ElementKind receiverKind = receiverType.element.kind;
          if (identical(receiverKind, ElementKind.TYPEDEF)) {
            // TODO(karlklose): handle typedefs.
            return null;
          }
          if (identical(receiverKind, ElementKind.TYPE_VARIABLE)) {
            // TODO(karlklose): handle type variables.
            return null;
          }
          if (!identical(receiverKind, ElementKind.CLASS)) {
            fail(node.receiver, 'unexpected receiver kind: ${receiverKind}');
          }
          ClassElement classElement = receiverType.element;
          // TODO(karlklose): substitute type arguments.
          DartType memberType =
            lookupMethodType(selector, classElement, selector.source);
          if (identical(memberType.element, compiler.dynamicClass)) return null;
          return memberType;
        } else {
          if (Elements.isUnresolved(element)) {
            fail(node, 'unresolved ${node.selector}');
          } else if (identical(element.kind, ElementKind.FUNCTION)) {
            return computeType(element);
          } else if (element.isForeign(compiler)) {
            return null;
          } else if (identical(element.kind, ElementKind.VARIABLE)
                     || identical(element.kind, ElementKind.FIELD)) {
            // TODO(karlklose): handle object invocations.
            return null;
          } else {
            fail(node, 'unexpected element kind ${element.kind}');
          }
        }
      }
      FunctionType funType = computeFunType();
      analyzeArguments(node, funType);
      return (funType != null) ? funType.returnType : types.dynamicType;
    }
  }

  visitSendSet(SendSet node) {
    Identifier selector = node.selector;
    final name = node.assignmentOperator.source.stringValue;
    if (identical(name, '++') || identical(name, '--')) {
      final Element element = elements[node.selector];
      final DartType receiverType = computeType(element);
      // TODO(karlklose): this should be the return type instead of int.
      return node.isPrefix ? intType : receiverType;
    } else {
      DartType targetType = computeType(elements[node]);
      Node value = node.arguments.head;
      checkAssignable(value, targetType, analyze(value));
      return targetType;
    }
  }

  DartType visitLiteralInt(LiteralInt node) {
    return intType;
  }

  DartType visitLiteralDouble(LiteralDouble node) {
    return doubleType;
  }

  DartType visitLiteralBool(LiteralBool node) {
    return boolType;
  }

  DartType visitLiteralString(LiteralString node) {
    return stringType;
  }

  DartType visitStringJuxtaposition(StringJuxtaposition node) {
    analyze(node.first);
    analyze(node.second);
    return stringType;
  }

  DartType visitLiteralNull(LiteralNull node) {
    return types.dynamicType;
  }

  DartType visitNewExpression(NewExpression node) {
    Element element = elements[node.send];
    analyzeArguments(node.send, computeType(element));
    return analyze(node.send.selector);
  }

  DartType visitLiteralList(LiteralList node) {
    return listType;
  }

  DartType visitNodeList(NodeList node) {
    DartType type = StatementType.NOT_RETURNING;
    bool reportedDeadCode = false;
    for (Link<Node> link = node.nodes; !link.isEmpty; link = link.tail) {
      DartType nextType = analyze(link.head);
      if (type == StatementType.RETURNING) {
        if (!reportedDeadCode) {
          reportTypeWarning(link.head, MessageKind.UNREACHABLE_CODE);
          reportedDeadCode = true;
        }
      } else if (type == StatementType.MAYBE_RETURNING){
        if (nextType == StatementType.RETURNING) {
          type = nextType;
        }
      } else {
        type = nextType;
      }
    }
    return type;
  }

  DartType visitOperator(Operator node) {
    fail(node, 'internal error');
  }

  /** Dart Programming Language Specification: 11.10 Return */
  DartType visitReturn(Return node) {
    if (identical(node.getBeginToken().stringValue, 'native')) {
      return StatementType.RETURNING;
    }
    if (node.isRedirectingFactoryBody) {
      // TODO(lrn): Typecheck the body. It must refer to the constructor
      // of a subtype.
      return StatementType.RETURNING;
    }

    final expression = node.expression;
    final isVoidFunction = (identical(expectedReturnType, types.voidType));

    // Executing a return statement return e; [...] It is a static type warning
    // if the type of e may not be assigned to the declared return type of the
    // immediately enclosing function.
    if (expression != null) {
      final expressionType = analyze(expression);
      if (isVoidFunction
          && !types.isAssignable(expressionType, types.voidType)) {
        reportTypeWarning(expression, MessageKind.RETURN_VALUE_IN_VOID);
      } else {
        checkAssignable(expression, expectedReturnType, expressionType);
      }

    // Let f be the function immediately enclosing a return statement of the
    // form 'return;' It is a static warning if both of the following conditions
    // hold:
    // - f is not a generative constructor.
    // - The return type of f may not be assigned to void.
    } else if (!types.isAssignable(expectedReturnType, types.voidType)) {
      reportTypeWarning(node, MessageKind.RETURN_NOTHING,
                        {'returnType': expectedReturnType});
    }
    return StatementType.RETURNING;
  }

  DartType visitThrow(Throw node) {
    if (node.expression != null) analyze(node.expression);
    return StatementType.RETURNING;
  }

  DartType computeType(Element element) {
    if (Elements.isUnresolved(element)) return types.dynamicType;
    DartType result = element.computeType(compiler);
    return (result != null) ? result : types.dynamicType;
  }

  DartType visitTypeAnnotation(TypeAnnotation node) {
    return elements.getType(node);
  }

  visitTypeVariable(TypeVariable node) {
    return types.dynamicType;
  }

  DartType visitVariableDefinitions(VariableDefinitions node) {
    DartType type = analyzeWithDefault(node.type, types.dynamicType);
    if (type == types.voidType) {
      reportTypeWarning(node.type, MessageKind.VOID_VARIABLE);
      type = types.dynamicType;
    }
    for (Link<Node> link = node.definitions.nodes; !link.isEmpty;
         link = link.tail) {
      Node initialization = link.head;
      compiler.ensure(initialization is Identifier
                      || initialization is Send);
      if (initialization is Send) {
        DartType initializer = analyzeNonVoid(link.head);
        checkAssignable(node, type, initializer);
      }
    }
    return StatementType.NOT_RETURNING;
  }

  DartType visitWhile(While node) {
    checkCondition(node.condition);
    StatementType bodyType = analyze(node.body);
    Expression cond = node.condition.asParenthesizedExpression().expression;
    if (cond.asLiteralBool() != null && cond.asLiteralBool().value == true) {
      // If the condition is a constant boolean expression denoting true,
      // control-flow always enters the loop body.
      // TODO(karlklose): this should be StatementType.RETURNING unless there
      // is a break in the loop body that has the loop or a label outside the
      // loop as a target.
      return bodyType;
    } else {
      return bodyType.join(StatementType.NOT_RETURNING);
    }
  }

  DartType visitParenthesizedExpression(ParenthesizedExpression node) {
    return analyze(node.expression);
  }

  DartType visitConditional(Conditional node) {
    checkCondition(node.condition);
    DartType thenType = analyzeNonVoid(node.thenExpression);
    DartType elseType = analyzeNonVoid(node.elseExpression);
    if (types.isSubtype(thenType, elseType)) {
      return thenType;
    } else if (types.isSubtype(elseType, thenType)) {
      return elseType;
    } else {
      return objectType;
    }
  }

  DartType visitModifiers(Modifiers node) {}

  visitStringInterpolation(StringInterpolation node) {
    node.visitChildren(this);
    return stringType;
  }

  visitStringInterpolationPart(StringInterpolationPart node) {
    node.visitChildren(this);
    return stringType;
  }

  visitEmptyStatement(EmptyStatement node) {
    return StatementType.NOT_RETURNING;
  }

  visitBreakStatement(BreakStatement node) {
    return StatementType.NOT_RETURNING;
  }

  visitContinueStatement(ContinueStatement node) {
    return StatementType.NOT_RETURNING;
  }

  visitForIn(ForIn node) {
    analyze(node.expression);
    StatementType bodyType = analyze(node.body);
    return bodyType.join(StatementType.NOT_RETURNING);
  }

  visitLabel(Label node) { }

  visitLabeledStatement(LabeledStatement node) {
    return node.statement.accept(this);
  }

  visitLiteralMap(LiteralMap node) {
    return unhandledExpression();
  }

  visitLiteralMapEntry(LiteralMapEntry node) {
    return unhandledExpression();
  }

  visitNamedArgument(NamedArgument node) {
    return unhandledExpression();
  }

  visitSwitchStatement(SwitchStatement node) {
    return unhandledStatement();
  }

  visitSwitchCase(SwitchCase node) {
    return unhandledStatement();
  }

  visitCaseMatch(CaseMatch node) {
    return unhandledStatement();
  }

  visitTryStatement(TryStatement node) {
    return unhandledStatement();
  }

  visitScriptTag(ScriptTag node) {
    return unhandledExpression();
  }

  visitCatchBlock(CatchBlock node) {
    return unhandledStatement();
  }

  visitTypedef(Typedef node) {
    return unhandledStatement();
  }

  DartType visitNode(Node node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitCombinator(Combinator node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitExport(Export node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitExpression(Expression node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitGotoStatement(GotoStatement node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitImport(Import node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitLibraryName(LibraryName node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitLibraryTag(LibraryTag node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitLiteral(Literal node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitPart(Part node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitPartOf(PartOf node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitPostfix(Postfix node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitPrefix(Prefix node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitStatement(Statement node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitStringNode(StringNode node) {
    compiler.unimplemented('visitNode', node: node);
  }

  DartType visitLibraryDependency(LibraryDependency node) {
    compiler.unimplemented('visitNode', node: node);
  }
}
