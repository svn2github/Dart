// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library engine.incremental_resolver;

import 'dart:collection';

import 'ast.dart';
import 'element.dart';
import 'error.dart';
import 'java_engine.dart';
import 'resolver.dart';
import 'scanner.dart';
import 'source.dart';


/**
 * Instances of the class [DeclarationMatcher] determine whether the element
 * model defined by a given AST structure matches an existing element model.
 */
class DeclarationMatcher extends RecursiveAstVisitor {
  /**
   * The compilation unit containing the AST nodes being visited.
   */
  CompilationUnitElement _enclosingUnit;

  /**
   * The function type alias containing the AST nodes being visited, or `null` if we are not
   * in the scope of a function type alias.
   */
  FunctionTypeAliasElement _enclosingAlias;

  /**
   * The class containing the AST nodes being visited, or `null` if we are not in the scope of
   * a class.
   */
  ClassElement _enclosingClass;

  /**
   * The method or function containing the AST nodes being visited, or `null` if we are not in
   * the scope of a method or function.
   */
  ExecutableElement _enclosingExecutable;

  /**
   * The parameter containing the AST nodes being visited, or `null` if we are not in the
   * scope of a parameter.
   */
  ParameterElement _enclosingParameter;

  bool _inTopLevelVariableDeclaration = false;

  /**
   * Is `true` if the current class declaration has a constructor.
   */
  bool _hasConstructor = false;

  /**
   * A set containing all of the elements in the element model that were defined by the old AST node
   * corresponding to the AST node being visited.
   */
  HashSet<Element> _allElements = new HashSet<Element>();

  /**
   * A set containing all of the elements in the element model that were defined by the old AST node
   * corresponding to the AST node being visited that have not already been matched to nodes in the
   * AST structure being visited.
   */
  HashSet<Element> _unmatchedElements = new HashSet<Element>();

  /**
   * Return `true` if the declarations within the given AST structure define an element model
   * that is equivalent to the corresponding elements rooted at the given element.
   *
   * @param node the AST structure being compared to the element model
   * @param element the root of the element model being compared to the AST structure
   * @return `true` if the AST structure defines the same elements as those in the given
   *         element model
   */
  bool matches(AstNode node, Element element) {
    _captureEnclosingElements(element);
    _gatherElements(element);
    try {
      node.accept(this);
    } on _DeclarationMismatchException catch (exception) {
      return false;
    }
    return _unmatchedElements.isEmpty;
  }

  @override
  visitBlockFunctionBody(BlockFunctionBody node) {
    // ignore bodies
  }

  @override
  visitClassDeclaration(ClassDeclaration node) {
    String name = node.name.name;
    ClassElement clazz = _findElement(_enclosingUnit.types, name);
    _enclosingClass = clazz;
    _processElement(clazz);
    // check for missing clauses
    if (node.extendsClause == null) {
      _assertTrue(clazz.supertype.name == 'Object');
    }
    if (node.implementsClause == null) {
      _assertTrue(clazz.interfaces.isEmpty);
    }
    if (node.withClause == null) {
      _assertTrue(clazz.mixins.isEmpty);
    }
    // process clauses and members
    _hasConstructor = false;
    super.visitClassDeclaration(node);
    // process default constructor
    if (!_hasConstructor) {
      ConstructorElement constructor = clazz.unnamedConstructor;
      _processElement(constructor);
      if (!constructor.isSynthetic) {
        _assertEquals(constructor.parameters.length, 0);
      }
    }
  }

  @override
  visitClassTypeAlias(ClassTypeAlias node) {
    ClassElement outerClass = _enclosingClass;
    try {
      SimpleIdentifier className = node.name;
      _enclosingClass = _findIdentifier(_enclosingUnit.types, className);
      _processElement(_enclosingClass);
      super.visitClassTypeAlias(node);
    } finally {
      _enclosingClass = outerClass;
    }
  }

  @override
  visitCompilationUnit(CompilationUnit node) {
    _processElement(_enclosingUnit);
    super.visitCompilationUnit(node);
  }

  @override
  visitConstructorDeclaration(ConstructorDeclaration node) {
    ExecutableElement outerExecutable = _enclosingExecutable;
    try {
      SimpleIdentifier constructorName = node.name;
      if (constructorName == null) {
        _enclosingExecutable = _enclosingClass.unnamedConstructor;
      } else {
        _enclosingExecutable =
            _enclosingClass.getNamedConstructor(constructorName.name);
      }
      _processElement(_enclosingExecutable);
      super.visitConstructorDeclaration(node);
    } finally {
      _enclosingExecutable = outerExecutable;
    }
  }

  @override
  visitDefaultFormalParameter(DefaultFormalParameter node) {
    SimpleIdentifier parameterName = node.parameter.identifier;
    ParameterElement element = _getElementForParameter(node, parameterName);
    Expression defaultValue = node.defaultValue;
    if (defaultValue != null) {
      ExecutableElement outerExecutable = _enclosingExecutable;
      try {
        if (element == null) {
          // TODO(brianwilkerson) Report this internal error.
        } else {
          _enclosingExecutable = element.initializer;
        }
        defaultValue.accept(this);
      } finally {
        _enclosingExecutable = outerExecutable;
      }
      _processElement(_enclosingExecutable);
    }
    ParameterElement outerParameter = _enclosingParameter;
    try {
      _enclosingParameter = element;
      _processElement(_enclosingParameter);
      super.visitDefaultFormalParameter(node);
    } finally {
      _enclosingParameter = outerParameter;
    }
  }

  @override
  visitEnumDeclaration(EnumDeclaration node) {
    ClassElement enclosingEnum =
        _findIdentifier(_enclosingUnit.enums, node.name);
    _processElement(enclosingEnum);
    List<FieldElement> constants = enclosingEnum.fields;
    for (EnumConstantDeclaration constant in node.constants) {
      FieldElement constantElement = _findIdentifier(constants, constant.name);
      _processElement(constantElement);
    }
    super.visitEnumDeclaration(node);
  }

  @override
  visitExportDirective(ExportDirective node) {
    String uri = _getStringValue(node.uri);
    if (uri != null) {
      LibraryElement library = _enclosingUnit.library;
      ExportElement exportElement = _findExport(
          library.exports,
          _enclosingUnit.context.sourceFactory.resolveUri(_enclosingUnit.source, uri));
      _processElement(exportElement);
    }
    super.visitExportDirective(node);
  }

  @override
  visitExpressionFunctionBody(ExpressionFunctionBody node) {
    // ignore bodies
  }

  @override
  visitExtendsClause(ExtendsClause node) {
    _assertSameType(node.superclass, _enclosingClass.supertype);
  }

  @override
  visitFieldFormalParameter(FieldFormalParameter node) {
    if (node.parent is! DefaultFormalParameter) {
      SimpleIdentifier parameterName = node.identifier;
      ParameterElement element = _getElementForParameter(node, parameterName);
      ParameterElement outerParameter = _enclosingParameter;
      try {
        _enclosingParameter = element;
        _processElement(_enclosingParameter);
        super.visitFieldFormalParameter(node);
      } finally {
        _enclosingParameter = outerParameter;
      }
    } else {
      super.visitFieldFormalParameter(node);
    }
  }

  @override
  visitFunctionDeclaration(FunctionDeclaration node) {
    ExecutableElement outerExecutable = _enclosingExecutable;
    try {
      SimpleIdentifier functionName = node.name;
      Token property = node.propertyKeyword;
      if (property == null) {
        if (_enclosingExecutable != null) {
          _enclosingExecutable =
              _findIdentifier(_enclosingExecutable.functions, functionName);
        } else {
          _enclosingExecutable =
              _findIdentifier(_enclosingUnit.functions, functionName);
        }
      } else {
        PropertyAccessorElement accessor =
            _findIdentifier(_enclosingUnit.accessors, functionName);
        if ((property as KeywordToken).keyword == Keyword.SET) {
          accessor = accessor.variable.setter;
        }
        _enclosingExecutable = accessor;
      }
      _processElement(_enclosingExecutable);
      super.visitFunctionDeclaration(node);
    } finally {
      _enclosingExecutable = outerExecutable;
    }
  }

  @override
  visitFunctionTypeAlias(FunctionTypeAlias node) {
    FunctionTypeAliasElement outerAlias = _enclosingAlias;
    try {
      SimpleIdentifier aliasName = node.name;
      _enclosingAlias =
          _findIdentifier(_enclosingUnit.functionTypeAliases, aliasName);
      _processElement(_enclosingAlias);
      super.visitFunctionTypeAlias(node);
    } finally {
      _enclosingAlias = outerAlias;
    }
  }

  @override
  visitFunctionTypedFormalParameter(FunctionTypedFormalParameter node) {
    if (node.parent is! DefaultFormalParameter) {
      SimpleIdentifier parameterName = node.identifier;
      ParameterElement element = _getElementForParameter(node, parameterName);
      ParameterElement outerParameter = _enclosingParameter;
      try {
        _enclosingParameter = element;
        _processElement(_enclosingParameter);
        super.visitFunctionTypedFormalParameter(node);
      } finally {
        _enclosingParameter = outerParameter;
      }
    } else {
      super.visitFunctionTypedFormalParameter(node);
    }
  }

  @override
  visitImplementsClause(ImplementsClause node) {
    List<TypeName> nodes = node.interfaces;
    List<InterfaceType> types = _enclosingClass.interfaces;
    _assertSameTypes(nodes, types);
  }

  @override
  visitImportDirective(ImportDirective node) {
    String uri = _getStringValue(node.uri);
    if (uri != null) {
      LibraryElement library = _enclosingUnit.library;
      ImportElement importElement = _findImport(
          library.imports,
          _enclosingUnit.context.sourceFactory.resolveUri(_enclosingUnit.source, uri),
          node.prefix);
      _processElement(importElement);
    }
    super.visitImportDirective(node);
  }

  @override
  visitMethodDeclaration(MethodDeclaration node) {
    ExecutableElement outerExecutable = _enclosingExecutable;
    try {
      Token property = node.propertyKeyword;
      SimpleIdentifier methodName = node.name;
      String nameOfMethod = methodName.name;
      if (nameOfMethod == TokenType.MINUS.lexeme &&
          node.parameters.parameters.length == 0) {
        nameOfMethod = "unary-";
      }
      if (property == null) {
        _enclosingExecutable = _findWithNameAndOffset(
            _enclosingClass.methods,
            nameOfMethod,
            methodName.offset);
        methodName.staticElement = _enclosingExecutable;
      } else {
        PropertyAccessorElement accessor =
            _findIdentifier(_enclosingClass.accessors, methodName);
        if ((property as KeywordToken).keyword == Keyword.SET) {
          accessor = accessor.variable.setter;
          methodName.staticElement = accessor;
        }
        _enclosingExecutable = accessor;
      }
      _processElement(_enclosingExecutable);
      super.visitMethodDeclaration(node);
    } finally {
      _enclosingExecutable = outerExecutable;
    }
  }

  @override
  visitPartDirective(PartDirective node) {
    String uri = _getStringValue(node.uri);
    if (uri != null) {
      Source partSource =
          _enclosingUnit.context.sourceFactory.resolveUri(_enclosingUnit.source, uri);
      CompilationUnitElement element =
          _findPart(_enclosingUnit.library.parts, partSource);
      _processElement(element);
    }
    super.visitPartDirective(node);
  }

  @override
  visitSimpleFormalParameter(SimpleFormalParameter node) {
    if (node.parent is! DefaultFormalParameter) {
      SimpleIdentifier parameterName = node.identifier;
      ParameterElement element = _getElementForParameter(node, parameterName);
      ParameterElement outerParameter = _enclosingParameter;
      try {
        _enclosingParameter = element;
        _processElement(_enclosingParameter);
        super.visitSimpleFormalParameter(node);
      } finally {
        _enclosingParameter = outerParameter;
      }
    } else {
    }
    super.visitSimpleFormalParameter(node);
  }

  @override
  visitTopLevelVariableDeclaration(TopLevelVariableDeclaration node) {
    _inTopLevelVariableDeclaration = true;
    try {
      super.visitTopLevelVariableDeclaration(node);
    } finally {
      _inTopLevelVariableDeclaration = false;
    }
  }

  @override
  visitTypeParameter(TypeParameter node) {
    SimpleIdentifier parameterName = node.name;
    TypeParameterElement element = null;
    if (_enclosingClass != null) {
      element = _findIdentifier(_enclosingClass.typeParameters, parameterName);
    } else if (_enclosingAlias != null) {
      element = _findIdentifier(_enclosingAlias.typeParameters, parameterName);
    }
    _processElement(element);
    super.visitTypeParameter(node);
  }

  @override
  visitVariableDeclaration(VariableDeclaration node) {
    String name = node.name.name;
    if (_inTopLevelVariableDeclaration) {
      TopLevelVariableElement variable =
          _findElement(_enclosingUnit.topLevelVariables, name);
      _assertNotNull(variable);
      _assertFalse(variable.isSynthetic);
      _assertEquals(node.isConst, variable.isConst);
      _assertEquals(node.isFinal, variable.isFinal);
      _assertSameType(
          (node.parent as VariableDeclarationList).type,
          variable.type);
      _processElement(variable);
      return;
    }
    VariableElement element;
    if (_enclosingExecutable != null) {
      element = _findElement(_enclosingExecutable.localVariables, name);
    }
    if (element == null && _enclosingClass != null) {
      element = _findElement(_enclosingClass.fields, name);
    }
    super.visitVariableDeclaration(node);
  }

  @override
  visitWithClause(WithClause node) {
    List<TypeName> nodes = node.mixinTypes;
    List<InterfaceType> types = _enclosingClass.mixins;
    _assertSameTypes(nodes, types);
  }

  void _assertEquals(Object a, Object b) {
    if (a != b) {
      throw new _DeclarationMismatchException();
    }
  }

  void _assertFalse(bool condition) {
    if (condition) {
      throw new _DeclarationMismatchException();
    }
  }

  void _assertNotNull(Element element) {
    if (element == null) {
      throw new _DeclarationMismatchException();
    }
  }

  void _assertSameType(TypeName node, DartType type) {
    String nodeName = node.name.name;
    if (type is InterfaceType) {
      _assertEquals(nodeName, type.name);
      // check arguments
      TypeArgumentList nodeArgumentList = node.typeArguments;
      List<DartType> typeArguments = type.typeArguments;
      if (nodeArgumentList == null) {
        _assertTrue(typeArguments.isEmpty);
      } else {
        List<TypeName> nodeArguments = nodeArgumentList.arguments;
        _assertSameTypes(nodeArguments, typeArguments);
      }
    } else {
      // TODO(scheglov) support other types
      _assertTrue(false);
    }
  }

  void _assertSameTypes(List<TypeName> nodes, List<DartType> type) {
    int length = nodes.length;
    _assertEquals(length, type.length);
    for (int i = 0; i < length; i++) {
      _assertSameType(nodes[i], type[i]);
    }
  }

  void _assertTrue(bool condition) {
    if (!condition) {
      throw new _DeclarationMismatchException();
    }
  }

  /**
   * Given that the comparison is to begin with the given element, capture the enclosing elements
   * that might be used while performing the comparison.
   *
   * @param element the element corresponding to the AST structure to be compared
   */
  void _captureEnclosingElements(Element element) {
    Element parent =
        element is CompilationUnitElement ? element : element.enclosingElement;
    while (parent != null) {
      if (parent is CompilationUnitElement) {
        _enclosingUnit = parent as CompilationUnitElement;
      } else if (parent is ClassElement) {
        if (_enclosingClass == null) {
          _enclosingClass = parent as ClassElement;
        }
      } else if (parent is FunctionTypeAliasElement) {
        if (_enclosingAlias == null) {
          _enclosingAlias = parent as FunctionTypeAliasElement;
        }
      } else if (parent is ExecutableElement) {
        if (_enclosingExecutable == null) {
          _enclosingExecutable = parent as ExecutableElement;
        }
      } else if (parent is ParameterElement) {
        if (_enclosingParameter == null) {
          _enclosingParameter = parent as ParameterElement;
        }
      }
      parent = parent.enclosingElement;
    }
  }

  /**
   * Return the [Element] in [elements] with the given [name].
   */
  Element _findElement(List<Element> elements, String name) {
    for (Element element in elements) {
      if (element.displayName == name) {
        return element;
      }
    }
    return null;
  }

  /**
   * Return the export element from the given array whose library has the given source, or
   * `null` if there is no such export.
   *
   * @param exports the export elements being searched
   * @param source the source of the library associated with the export element to being searched
   *          for
   * @return the export element whose library has the given source
   */
  ExportElement _findExport(List<ExportElement> exports, Source source) {
    for (ExportElement export in exports) {
      if (export.exportedLibrary.source == source) {
        return export;
      }
    }
    return null;
  }

  /**
   * Return the element in the given array of elements that was created for the declaration with the
   * given name.
   *
   * @param elements the elements of the appropriate kind that exist in the current context
   * @param identifier the name node in the declaration of the element to be returned
   * @return the element created for the declaration with the given name
   */
  Element _findIdentifier(List<Element> elements,
      SimpleIdentifier identifier) =>
      _findWithNameAndOffset(elements, identifier.name, identifier.offset);

  /**
   * Return the import element from the given array whose library has the given source and that has
   * the given prefix, or `null` if there is no such import.
   *
   * @param imports the import elements being searched
   * @param source the source of the library associated with the import element to being searched
   *          for
   * @param prefix the prefix with which the library was imported
   * @return the import element whose library has the given source and prefix
   */
  ImportElement _findImport(List<ImportElement> imports, Source source,
      SimpleIdentifier prefix) {
    for (ImportElement element in imports) {
      if (element.importedLibrary.source == source) {
        PrefixElement prefixElement = element.prefix;
        if (prefix == null) {
          if (prefixElement == null) {
            return element;
          }
        } else {
          if (prefixElement != null &&
              prefix.name == prefixElement.displayName) {
            return element;
          }
        }
      }
    }
    return null;
  }

  /**
   * Return the element for the part with the given source, or `null` if there is no element
   * for the given source.
   *
   * @param parts the elements for the parts
   * @param partSource the source for the part whose element is to be returned
   * @return the element for the part with the given source
   */
  CompilationUnitElement _findPart(List<CompilationUnitElement> parts,
      Source partSource) {
    for (CompilationUnitElement part in parts) {
      if (part.source == partSource) {
        return part;
      }
    }
    return null;
  }

  /**
   * Return the element in the given array of elements that was created for the declaration with the
   * given name at the given offset.
   *
   * @param elements the elements of the appropriate kind that exist in the current context
   * @param name the name of the element to be returned
   * @param offset the offset of the name of the element to be returned
   * @return the element with the given name and offset
   */
  Element _findWithNameAndOffset(List<Element> elements, String name,
      int offset) {
    for (Element element in elements) {
      if (element.displayName == name && element.nameOffset == offset) {
        return element;
      }
    }
    return null;
  }

  void _gatherElements(Element element) {
    element.accept(new _ElementsGatherer(this));
  }

  /**
   * Search the most closely enclosing list of parameters for a parameter with the given name.
   *
   * @param node the node defining the parameter with the given name
   * @param parameterName the name of the parameter being searched for
   * @return the element representing the parameter with that name
   */
  ParameterElement _getElementForParameter(FormalParameter node,
      SimpleIdentifier parameterName) {
    List<ParameterElement> parameters = null;
    if (_enclosingParameter != null) {
      parameters = _enclosingParameter.parameters;
    }
    if (parameters == null && _enclosingExecutable != null) {
      parameters = _enclosingExecutable.parameters;
    }
    if (parameters == null && _enclosingAlias != null) {
      parameters = _enclosingAlias.parameters;
    }
    return parameters == null ?
        null :
        _findIdentifier(parameters, parameterName);
  }

  /**
   * Return the value of the given string literal, or `null` if the string is not a constant
   * string without any string interpolation.
   *
   * @param literal the string literal whose value is to be returned
   * @return the value of the given string literal
   */
  String _getStringValue(StringLiteral literal) {
    if (literal is StringInterpolation) {
      return null;
    }
    return literal.stringValue;
  }

  void _processElement(Element element) {
    _assertNotNull(element);
    if (!_allElements.contains(element)) {
      throw new _DeclarationMismatchException();
    }
    _unmatchedElements.remove(element);
  }
}


/**
 * Instances of the class [IncrementalResolver] resolve the smallest portion of
 * an AST structure that we currently know how to resolve.
 */
class IncrementalResolver {
  /**
   * The error listener that will be informed of any errors that are found
   * during resolution.
   */
  final AnalysisErrorListener _errorListener;

  /**
   * The object used to access the types from the core library.
   */
  final TypeProvider _typeProvider;

  /**
   * The element for the library containing the compilation unit being resolved.
   */
  final LibraryElement _definingLibrary;

  /**
   * The element of the compilation unit being resolved.
   */
  final CompilationUnitElement _definingUnit;

  /**
   * The source representing the compilation unit being visited.
   */
  final Source _source;

  /**
   * The offset of the changed contents.
   */
  final int _updateOffset;

  /**
   * The number of characters in the original contents that were replaced.
   */
  final int _updateOldLength;

  /**
   * The number of characters in the replacement text.
   */
  final int _updateNewLength;

  /**
   * Initialize a newly created incremental resolver to resolve a node in the
   * given source in the given library, reporting errors to the given error
   * listener.
   */
  IncrementalResolver(this._errorListener, this._typeProvider,
      this._definingLibrary, this._definingUnit, this._source, this._updateOffset,
      this._updateOldLength, this._updateNewLength);

  /**
   * Resolve [node], reporting any errors or warnings to the given listener.
   *
   * [node] - the root of the AST structure to be resolved.
   */
  void resolve(AstNode node) {
    AstNode rootNode = _findResolutionRoot(node);
    Scope scope = ScopeBuilder.scopeFor(rootNode, _errorListener);
    if (_elementModelChanged(rootNode.parent)) {
      throw new AnalysisException("Cannot resolve node: element model changed");
    }
    _definingUnit.accept(
        new _ElementNameOffsetUpdater(
            _updateOffset,
            _updateNewLength - _updateOldLength));
    _resolveTypes(node, scope);
    _resolveVariables(node, scope);
    _resolveReferences(node, scope);
  }

  /**
   * Return `true` if the given node can be resolved independently of any other
   * nodes.
   *
   * *Note*: This method needs to be kept in sync with
   * [ScopeBuilder.scopeForAstNode].
   *
   * [node] - the node being tested.
   */
  bool _canBeResolved(AstNode node) =>
      node is ClassDeclaration ||
          node is ClassTypeAlias ||
          node is CompilationUnit ||
          node is ConstructorDeclaration ||
          node is FunctionDeclaration ||
          node is FunctionTypeAlias ||
          node is MethodDeclaration;

  /**
   * Return `true` if the portion of the element model defined by the given node
   * has changed.
   *
   * [node] - the node defining the portion of the element model being tested.
   *
   * Throws [AnalysisException] if the correctness of the element model cannot
   * be determined.
   */
  bool _elementModelChanged(AstNode node) {
    Element element = _getElement(node);
    if (element == null) {
      throw new AnalysisException(
          "Cannot resolve node: a ${node.runtimeType} does not define an element");
    }
    DeclarationMatcher matcher = new DeclarationMatcher();
    return !matcher.matches(node, element);
  }

  /**
   * Starting at [node], find the smallest AST node that can be resolved
   * independently of any other nodes. Return the node that was found.
   *
   * [node] - the node at which the search is to begin
   *
   * Throws [AnalysisException] if there is no such node.
   */
  AstNode _findResolutionRoot(AstNode node) {
    AstNode result = node;
    AstNode parent = result.parent;
    while (parent != null && !_canBeResolved(parent)) {
      result = parent;
      parent = result.parent;
    }
    if (parent == null) {
      throw new AnalysisException("Cannot resolve node: no resolvable node");
    }
    return result;
  }

  /**
   * Return the element defined by [node], or `null` if the node does not
   * define an element.
   */
  Element _getElement(AstNode node) {
    if (node is Declaration) {
      return node.element;
    } else if (node is CompilationUnit) {
      return node.element;
    }
    return null;
  }

  void _resolveReferences(AstNode node, Scope scope) {
    ResolverVisitor visitor = new ResolverVisitor.con3(
        _definingLibrary,
        _source,
        _typeProvider,
        scope,
        _errorListener);
    node.accept(visitor);
  }

  void _resolveTypes(AstNode node, Scope scope) {
    TypeResolverVisitor visitor = new TypeResolverVisitor.con3(
        _definingLibrary,
        _source,
        _typeProvider,
        scope,
        _errorListener);
    node.accept(visitor);
  }

  void _resolveVariables(AstNode node, Scope scope) {
    VariableResolverVisitor visitor = new VariableResolverVisitor.con2(
        _definingLibrary,
        _source,
        _typeProvider,
        scope,
        _errorListener);
    node.accept(visitor);
  }
}


/**
 * Instances of the class [ScopeBuilder] build the scope for a given node in an
 * AST structure. At the moment, this class only handles top-level and
 * class-level declarations.
 */
class ScopeBuilder {
  /**
   * The listener to which analysis errors will be reported.
   */
  final AnalysisErrorListener _errorListener;

  /**
   * Initialize a newly created scope builder to generate a scope that will report errors to the
   * given listener.
   *
   * @param errorListener the listener to which analysis errors will be reported
   */
  ScopeBuilder(this._errorListener);

  /**
   * Return the scope in which the given AST structure should be resolved.
   *
   * <b>Note:</b> This method needs to be kept in sync with
   * [IncrementalResolver.canBeResolved].
   *
   * @param node the root of the AST structure to be resolved
   * @return the scope in which the given AST structure should be resolved
   * @throws AnalysisException if the AST structure has not been resolved or is not part of a
   *           [CompilationUnit]
   */
  Scope _scopeForAstNode(AstNode node) {
    if (node is CompilationUnit) {
      return _scopeForCompilationUnit(node);
    }
    AstNode parent = node.parent;
    if (parent == null) {
      throw new AnalysisException(
          "Cannot create scope: node is not part of a CompilationUnit");
    }
    Scope scope = _scopeForAstNode(parent);
    if (node is ClassDeclaration) {
      ClassElement element = node.element;
      if (element == null) {
        throw new AnalysisException(
            "Cannot build a scope for an unresolved class");
      }
      scope = new ClassScope(new TypeParameterScope(scope, element), element);
    } else if (node is ClassTypeAlias) {
      ClassElement element = node.element;
      if (element == null) {
        throw new AnalysisException(
            "Cannot build a scope for an unresolved class type alias");
      }
      scope = new ClassScope(new TypeParameterScope(scope, element), element);
    } else if (node is ConstructorDeclaration) {
      ConstructorElement element = node.element;
      if (element == null) {
        throw new AnalysisException(
            "Cannot build a scope for an unresolved constructor");
      }
      FunctionScope functionScope = new FunctionScope(scope, element);
      functionScope.defineParameters();
      scope = functionScope;
    } else if (node is FunctionDeclaration) {
      ExecutableElement element = node.element;
      if (element == null) {
        throw new AnalysisException(
            "Cannot build a scope for an unresolved function");
      }
      FunctionScope functionScope = new FunctionScope(scope, element);
      functionScope.defineParameters();
      scope = functionScope;
    } else if (node is FunctionTypeAlias) {
      scope = new FunctionTypeScope(scope, node.element);
    } else if (node is MethodDeclaration) {
      ExecutableElement element = node.element;
      if (element == null) {
        throw new AnalysisException(
            "Cannot build a scope for an unresolved method");
      }
      FunctionScope functionScope = new FunctionScope(scope, element);
      functionScope.defineParameters();
      scope = functionScope;
    }
    return scope;
  }

  Scope _scopeForCompilationUnit(CompilationUnit node) {
    CompilationUnitElement unitElement = node.element;
    if (unitElement == null) {
      throw new AnalysisException(
          "Cannot create scope: compilation unit is not resolved");
    }
    LibraryElement libraryElement = unitElement.library;
    if (libraryElement == null) {
      throw new AnalysisException(
          "Cannot create scope: compilation unit is not part of a library");
    }
    return new LibraryScope(libraryElement, _errorListener);
  }

  /**
   * Return the scope in which the given AST structure should be resolved.
   *
   * @param node the root of the AST structure to be resolved
   * @param errorListener the listener to which analysis errors will be reported
   * @return the scope in which the given AST structure should be resolved
   * @throws AnalysisException if the AST structure has not been resolved or is not part of a
   *           [CompilationUnit]
   */
  static Scope scopeFor(AstNode node, AnalysisErrorListener errorListener) {
    if (node == null) {
      throw new AnalysisException("Cannot create scope: node is null");
    } else if (node is CompilationUnit) {
      ScopeBuilder builder = new ScopeBuilder(errorListener);
      return builder._scopeForAstNode(node);
    }
    AstNode parent = node.parent;
    if (parent == null) {
      throw new AnalysisException(
          "Cannot create scope: node is not part of a CompilationUnit");
    }
    ScopeBuilder builder = new ScopeBuilder(errorListener);
    return builder._scopeForAstNode(parent);
  }
}


/**
 * Instances of the class [_DeclarationMismatchException] represent an exception
 * that is thrown when the element model defined by a given AST structure does
 * not match an existing element model.
 */
class _DeclarationMismatchException {
}


class _ElementNameOffsetUpdater extends GeneralizingElementVisitor {
  final int updateOffset;
  final int updateDelta;

  _ElementNameOffsetUpdater(this.updateOffset, this.updateDelta);

  @override
  visitElement(Element element) {
    int nameOffset = element.nameOffset;
    if (nameOffset >= updateOffset) {
      (element as ElementImpl).nameOffset = nameOffset + updateDelta;
    }
    super.visitElement(element);
  }
}


class _ElementsGatherer extends GeneralizingElementVisitor {
  final DeclarationMatcher matcher;

  _ElementsGatherer(this.matcher);

  @override
  visitElement(Element element) {
    _addElement(element);
    super.visitElement(element);
  }

  @override
  visitPropertyAccessorElement(PropertyAccessorElement element) {
    if (!element.isSynthetic) {
      _addElement(element);
    }
    // Don't visit children (such as a synthetic setter parameter).
  }

  @override
  visitPropertyInducingElement(PropertyInducingElement element) {
    // TODO(scheglov) should we remove synthetic variable initializer?
//    _addElement(element);
//    element.getter.accept(this);
//    element.setter.accept(this);
//    _addElement(element.getter);
//    _addElement(element.setter);
  }

  @override
  visitTopLevelVariableElement(TopLevelVariableElement element) {
    if (!element.isSynthetic) {
      _addElement(element);
    }
  }

  void _addElement(Element element) {
    if (element != null) {
      matcher._allElements.add(element);
      matcher._unmatchedElements.add(element);
    }
  }
}
