// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// VM-specific implementation of the dart:mirrors library.

import "dart:collection";

// These values are allowed to be passed directly over the wire.
bool _isSimpleValue(var value) {
  return (value == null || value is num || value is String || value is bool);
}

Map _filterMap(Map<Symbol, dynamic> old_map, bool filter(Symbol key, value)) {
  Map new_map = new Map<Symbol, dynamic>();
  old_map.forEach((key, value) {
    if (filter(key, value)) {
      new_map[key] = value;
    }
  });
  return new_map;
}

Map _makeMemberMap(List mirrors) {
  Map result = new Map<Symbol, dynamic>();
  mirrors.forEach((mirror) => result[mirror.simpleName] = mirror);
  return result;
}

String _n(Symbol symbol) => _symbol_dev.Symbol.getName(symbol);

Symbol _s(String name) {
  if (name == null) return null;
  return new _symbol_dev.Symbol.unvalidated(name);
}

Symbol _computeQualifiedName(DeclarationMirror owner, Symbol simpleName) {
  if (owner == null) return simpleName;
  return _s('${_n(owner.qualifiedName)}.${_n(simpleName)}');
}

Map<Symbol, dynamic> _convertStringToSymbolMap(Map<String, dynamic> map) {
  if (map == null) return null;
  Map<Symbol, dynamic> result = new Map<Symbol, dynamic>();
  map.forEach((name, value) => result[_s(name)] = value);
  return result;
}

String _makeSignatureString(TypeMirror returnType,
                            List<ParameterMirror> parameters) {
  StringBuffer buf = new StringBuffer();
  buf.write(_n(returnType.qualifiedName));
  buf.write(' (');
  bool found_optional_param = false;
  for (int i = 0; i < parameters.length; i++) {
    var param = parameters[i];
    if (param.isOptional && !found_optional_param) {
      buf.write('[');
      found_optional_param = true;
    }
    buf.write(_n(param.type.qualifiedName));
    if (i < (parameters.length - 1)) {
      buf.write(', ');
    }
  }
  if (found_optional_param) {
    buf.write(']');
  }
  buf.write(')');
  return buf.toString();
}

Map<Uri, LibraryMirror> _createLibrariesMap(List<LibraryMirror> list) {
  var map = new Map<Uri, LibraryMirror>();
  list.forEach((LibraryMirror mirror) => map[mirror.uri] = mirror);
  return map;
}

List _metadata(reflectee)
  native 'DeclarationMirror_metadata';

// This will verify the argument types, unwrap them, and ensure we have a fixed
// array.
List _unwarpAsyncPositionals(wrappedArgs){
  List unwrappedArgs = new List(wrappedArgs.length);
  for(int i = 0; i < wrappedArgs.length; i++){
    var wrappedArg = wrappedArgs[i];
    if(_isSimpleValue(wrappedArg)) {
      unwrappedArgs[i] = wrappedArg;
    } else if(wrappedArg is InstanceMirror) {
      unwrappedArgs[i] = wrappedArg._reflectee;
    } else { 
      throw "positional argument $i ($arg) was not a simple value or InstanceMirror";
    }
  }
  return unwrappedArgs;
}

class _LocalMirrorSystemImpl extends MirrorSystem {
  // Change parameter back to "this.libraries" when native code is changed.
  _LocalMirrorSystemImpl(List<LibraryMirror> libraries, this.isolate)
      : this.libraries = _createLibrariesMap(libraries),
        _functionTypes = new Map<String, FunctionTypeMirror>();

  final Map<Uri, LibraryMirror> libraries;
  final IsolateMirror isolate;

  TypeMirror _dynamicType = null;

  TypeMirror get dynamicType {
    if (_dynamicType == null) {
      _dynamicType = new _SpecialTypeMirrorImpl('dynamic');
    }
    return _dynamicType;
  }

  TypeMirror _voidType = null;

  TypeMirror get voidType {
    if (_voidType == null) {
      _voidType = new _SpecialTypeMirrorImpl('void');
    }
    return _voidType;
  }

  final Map<String, FunctionTypeMirror> _functionTypes;
  FunctionTypeMirror _lookupFunctionTypeMirror(
      reflectee,
      TypeMirror returnType,
      List<ParameterMirror> parameters) {
    var sigString = _makeSignatureString(returnType, parameters);
    var mirror = _functionTypes[sigString];
    if (mirror == null) {
      mirror = new _LocalFunctionTypeMirrorImpl(reflectee,
                                                sigString,
                                                returnType,
                                                parameters);
      _functionTypes[sigString] = mirror;
    }
    return mirror;
  }

  String toString() => "MirrorSystem for isolate '${isolate.debugName}'";
}

abstract class _LocalMirrorImpl implements Mirror {
  int get hashCode {
    throw new UnimplementedError('Mirror.hashCode is not implemented');
  }

  // Local mirrors always return the same MirrorSystem.  This field
  // is more interesting once we implement remote mirrors.
  MirrorSystem get mirrors => _Mirrors.currentMirrorSystem();
}

class _LocalIsolateMirrorImpl extends _LocalMirrorImpl
    implements IsolateMirror {
  _LocalIsolateMirrorImpl(this.debugName, this.rootLibrary) {}

  final String debugName;
  final bool isCurrent = true;
  final LibraryMirror rootLibrary;

  String toString() => "IsolateMirror on '$debugName'";
}

abstract class _LocalObjectMirrorImpl extends _LocalMirrorImpl
    implements ObjectMirror {
  _LocalObjectMirrorImpl(this._reflectee);

  final _reflectee; // May be a MirrorReference or an ordinary object.

  InstanceMirror invoke(Symbol memberName,
                        List positionalArguments,
                        [Map<Symbol, dynamic> namedArguments]) {
    if (namedArguments != null) {
      throw new UnimplementedError(
          'named argument support is not implemented');
    }
    return reflect(this._invoke(_reflectee,
                                _n(memberName),
                                positionalArguments.toList(growable:false))); 
  }

  InstanceMirror getField(Symbol memberName) {
    return reflect(this._invokeGetter(_reflectee,
                                      _n(memberName))); 
  }

  InstanceMirror setField(Symbol memberName, Object value) {
    this._invokeSetter(_reflectee,
                       _n(memberName),
                       value);
    return reflect(value);
  }

  Future<InstanceMirror> invokeAsync(Symbol memberName,
                                     List positionalArguments,
                                     [Map<Symbol, dynamic> namedArguments]) {
    if (namedArguments != null) {
      throw new UnimplementedError(
          'named argument support is not implemented');
    }

    try {
      var result = this._invoke(_reflectee,
                                _n(memberName),
                                _unwarpAsyncPositionals(positionalArguments));
      return new Future.value(reflect(result)); 
    } catch(e) {
      return new Future.error(e);
    }
  }

  Future<InstanceMirror> getFieldAsync(Symbol memberName) {
   try {
      var result = this._invokeGetter(_reflectee,
                                      _n(memberName));
      return new Future.value(reflect(result)); 
    } catch(e) {
      return new Future.error(e);
    }
  }

  Future<InstanceMirror> setFieldAsync(Symbol memberName, Object value) {
    try {
      var unwrappedValue;
      if(_isSimpleValue(value)) {
        unwrappedValue = value;
      } else if(value is InstanceMirror) {
        unwrappedValue = value._reflectee;
      } else { 
        throw "setter argument ($value) must be"
              "a simple value or InstanceMirror";
      }

      this._invokeSetter(_reflectee,
                         _n(memberName),
                         unwrappedValue);
      return new Future.value(reflect(unwrappedValue)); 
    } catch(e) {
      return new Future.error(e);
    }
  }

  static _validateArgument(int i, Object arg)
  {
    if (arg is Mirror) {
        if (arg is! InstanceMirror) {
          throw new MirrorException(
              'positional argument $i ($arg) was not an InstanceMirror');
        }
      } else if (!_isSimpleValue(arg)) {
        throw new MirrorException(
            'positional argument $i ($arg) was not a simple value');
      }
  }
}

class _LocalInstanceMirrorImpl extends _LocalObjectMirrorImpl
    implements InstanceMirror {
  // TODO(ahe): This is a hack, see delegate below.
  static Function _invokeOnClosure;

  _LocalInstanceMirrorImpl(reflectee) : super(reflectee);

  ClassMirror _type;
  ClassMirror get type {
    if (_type == null) {
      // Note it not safe to use reflectee.runtimeType because runtimeType may
      // be overridden.
      _type = reflectClass(_computeType(reflectee));
    }
    return _type;
  }

  // LocalInstanceMirrors always reflect local instances
  bool hasReflectee = true;

  get reflectee => _reflectee;

  delegate(Invocation invocation) {
    if (_invokeOnClosure == null) {
      // TODO(ahe): This is a total hack.  We're using the mirror
      // system to access a private field in a different library.  For
      // some reason, that works.  On the other hand, calling a
      // private method does not work.
      
      _LocalInstanceMirrorImpl mirror =
          reflect(invocation);
      _invokeOnClosure = reflectClass(invocation.runtimeType)
          .getField(const Symbol('_invokeOnClosure')).reflectee;
    }
    return _invokeOnClosure(reflectee, invocation);
  }

  String toString() => 'InstanceMirror on ${Error.safeToString(_reflectee)}';

  _invoke(reflectee, functionName, positionalArguments)
      native 'InstanceMirror_invoke';

  _invokeGetter(reflectee, getterName)
      native 'InstanceMirror_invokeGetter';

  _invokeSetter(reflectee, setterName, value)
      native 'InstanceMirror_invokeSetter';

  static _computeType(reflectee)
      native 'Object_runtimeType';
}

class _LocalClosureMirrorImpl extends _LocalInstanceMirrorImpl
    implements ClosureMirror {
  _LocalClosureMirrorImpl(reflectee) : super(reflectee);

  MethodMirror _function;
  MethodMirror get function {
    if (_function == null) {
      _function = _computeFunction(reflectee);
    }
    return _function;
  }

  String get source {
    throw new UnimplementedError(
        'ClosureMirror.source is not implemented');
  }

  InstanceMirror apply(List<Object> positionalArguments,
                       [Map<Symbol, Object> namedArguments]) {
    if (namedArguments != null) {
      throw new UnimplementedError(
          'named argument support is not implemented');
    }
    // It is tempting to implement this in terms of Function.apply, but then
    // lazy compilation errors would be fatal.
    return reflect(_apply(_reflectee,
                          positionalArguments.toList(growable:false)));
  }

  Future<InstanceMirror> applyAsync(List positionalArguments,
                                    [Map<Symbol, dynamic> namedArguments]) {
    if (namedArguments != null) {
      throw new UnimplementedError(
          'named argument support is not implemented');
    }

    try {
      var result = _apply(_reflectee,
                          _unwarpAsyncPositionals(positionalArguments));
      return new Future.value(reflect(result)); 
    } on MirroredError catch(e) {
      return new Future.error(e);
    }
  }

  Future<InstanceMirror> findInContext(Symbol name) {
    throw new UnimplementedError(
        'ClosureMirror.findInContext() is not implemented');
  }

  String toString() => "ClosureMirror on '${Error.safeToString(_reflectee)}'";

  static _apply(reflectee, positionalArguments)
      native 'ClosureMirror_apply';

  static _computeFunction(reflectee)
      native 'ClosureMirror_function';
}

class _LazyTypeMirror {
  _LazyTypeMirror(String this.libraryUrl, String typeName)
      : this.typeName = _s(typeName);

  TypeMirror resolve(MirrorSystem mirrors) {
    if (libraryUrl == null) {
      if (typeName == const Symbol('dynamic')) {
        return mirrors.dynamicType;
      } else if (typeName == const Symbol('void')) {
        return mirrors.voidType;
      } else {
        throw new UnimplementedError(
            "Mirror for type '$typeName' is not implemented");
      }
    }
    var resolved = mirrors.libraries[Uri.parse(libraryUrl)].members[typeName];
    if (resolved == null) {
      throw new UnimplementedError(
          "Mirror for type '$typeName' is not implemented");
    }
    return resolved;
  }

  final String libraryUrl;
  final Symbol typeName;
}

class _LocalClassMirrorImpl extends _LocalObjectMirrorImpl
    implements ClassMirror {
  _LocalClassMirrorImpl(reflectee,
                        String simpleName)
      : this._simpleName = _s(simpleName),
        super(reflectee);

  Symbol _simpleName;
  Symbol get simpleName {
    // dynamic, void and the function types have their names set eagerly in the
    // constructor.
    if(_simpleName == null) {
      _simpleName = _s(_name(_reflectee));
    }
    return _simpleName;
  }

  Symbol _qualifiedName = null;
  Symbol get qualifiedName {
    if (_qualifiedName == null) {
      _qualifiedName = _computeQualifiedName(owner, simpleName);
    }
    return _qualifiedName;
  }

  var _owner;
  DeclarationMirror get owner {
    if (_owner == null) {
      _owner = _library(_reflectee);
    }
    return _owner;
  }

  bool get isPrivate => _n(simpleName).startsWith('_');

  final bool isTopLevel = true;

  SourceLocation get location {
    throw new UnimplementedError(
        'ClassMirror.location is not implemented');
  }

  // TODO(rmacnak): Remove these left-overs from the days of separate interfaces
  // once we send out a breaking change.
  bool get isClass => true;
  ClassMirror get defaultFactory => null;

  var _superclass;
  ClassMirror get superclass {
    if (_superclass == null) {
      Type supertype = _supertype(_reflectee);
      if (supertype == null) {
        // Object has no superclass.
        return null;
      }
      _superclass = reflectClass(supertype);
    }
    if (_superclass is! Mirror) {
      _superclass = _superclass.resolve(mirrors);
    }
    return _superclass;
  }

  var _superinterfaces;
  List<ClassMirror> get superinterfaces {
    if (_superinterfaces == null) {
      _superinterfaces = _interfaces(_reflectee)
          .map((i) => reflectClass(i)).toList(growable:false);
    }
    return _superinterfaces;
  }

  Map<Symbol, Mirror> _members;

  Map<Symbol, Mirror> get members {
    if (_members == null) {
      _members = _makeMemberMap(_computeMembers(_reflectee));
    }
    return _members;
  }

  Map<Symbol, MethodMirror> _methods = null;
  Map<Symbol, MethodMirror> _getters = null;
  Map<Symbol, MethodMirror> _setters = null;
  Map<Symbol, VariableMirror> _variables = null;

  Map<Symbol, MethodMirror> get methods {
    if (_methods == null) {
      _methods = _filterMap(
          members,
          (key, value) => (value is MethodMirror && value.isRegularMethod));
    }
    return _methods;
  }

  Map<Symbol, MethodMirror> get getters {
    if (_getters == null) {
      _getters = _filterMap(
          members,
          (key, value) => (value is MethodMirror && value.isGetter));
    }
    return _getters;
  }

  Map<Symbol, MethodMirror> get setters {
    if (_setters == null) {
      _setters = _filterMap(
          members,
          (key, value) => (value is MethodMirror && value.isSetter));
    }
    return _setters;
  }

  Map<Symbol, VariableMirror> get variables {
    if (_variables == null) {
      _variables = _filterMap(
          members,
          (key, value) => (value is VariableMirror));
    }
    return _variables;
  }

  Map<Symbol, MethodMirror> _constructors;

  Map<Symbol, MethodMirror> get constructors {
    if (_constructors == null) {
      _constructors = _makeMemberMap(_computeConstructors(_reflectee));
    }
    return _constructors;
  }

  Map<Symbol, TypeVariableMirror> _typeVariables = null;

  Map<Symbol, TypeVariableMirror> get typeVariables {
    if (_typeVariables == null) {
      List params = _ClassMirror_type_variables(_reflectee);
      _typeVariables = new LinkedHashMap<Symbol, TypeVariableMirror>();
      var mirror;
      for (var i = 0; i < params.length; i += 2) {
        mirror = new _LocalTypeVariableMirrorImpl(
            params[i + 1], params[i], this);
        _typeVariables[mirror.simpleName] = mirror;
      }
    }
    return _typeVariables;
  }

  Map<Symbol, TypeMirror> get typeArguments {
    throw new UnimplementedError(
        'ClassMirror.typeArguments is not implemented');
  }

  bool get isOriginalDeclaration {
    throw new UnimplementedError(
        'ClassMirror.isOriginalDeclaration is not implemented');
  }

  ClassMirror get genericDeclaration {
    throw new UnimplementedError(
        'ClassMirror.originalDeclaration is not implemented');
  }

  String toString() {
    return "ClassMirror on '${_n(simpleName)}'";
  }

  InstanceMirror newInstance(Symbol constructorName,
                             List positionalArguments,
                             [Map<Symbol, dynamic> namedArguments]) {
    if (namedArguments != null) {
      throw new UnimplementedError(
          'named argument support is not implemented');
    }
    return reflect(_invokeConstructor(_reflectee,
                                      _n(constructorName),
                                      positionalArguments.toList(growable:false)));
  }

  Future<InstanceMirror> newInstanceAsync(Symbol constructorName,
                                          List positionalArguments,
                                          [Map<Symbol, dynamic> namedArguments]) {
    if (namedArguments != null) {
      throw new UnimplementedError(
          'named argument support is not implemented');
    }

    try {
      var result = _invokeConstructor(_reflectee,
                                      _n(constructorName),
                                      _unwarpAsyncPositionals(positionalArguments));
      return new Future.value(reflect(result)); 
    } catch(e) {
      return new Future.error(e);
    }
  }

  List<InstanceMirror> get metadata {
    // Get the metadata objects, convert them into InstanceMirrors using
    // reflect() and then make them into a Dart list.
    return _metadata(_reflectee).map(reflect).toList(growable:false);
  }

  static _name(reflectee)
      native "ClassMirror_name";

  static _library(reflectee)
      native "ClassMirror_library";

  static _supertype(reflectee)
      native "ClassMirror_supertype";

  static _interfaces(reflectee)
      native "ClassMirror_interfaces";

  _computeMembers(reflectee)
      native "ClassMirror_members";
  
  _computeConstructors(reflectee)
      native "ClassMirror_constructors";

  _invoke(reflectee, memberName, positionalArguments)
      native 'ClassMirror_invoke';

  _invokeGetter(reflectee, getterName)
      native 'ClassMirror_invokeGetter';

  _invokeSetter(reflectee, setterName, value)
      native 'ClassMirror_invokeSetter';

  static _invokeConstructor(reflectee, constructorName, positionalArguments)
      native 'ClassMirror_invokeConstructor';

  static _ClassMirror_type_variables(reflectee)
      native "ClassMirror_type_variables";
}

class _LazyFunctionTypeMirror {
  _LazyFunctionTypeMirror(this.reflectee, this.returnType, this.parameters) {}

  ClassMirror resolve(MirrorSystem mirrors) {
    return mirrors._lookupFunctionTypeMirror(reflectee,
                                             returnType.resolve(mirrors),
                                             parameters);
  }

  final reflectee;
  final returnType;
  final List<ParameterMirror> parameters;
}

class _LocalFunctionTypeMirrorImpl extends _LocalClassMirrorImpl
    implements FunctionTypeMirror {
  _LocalFunctionTypeMirrorImpl(reflectee,
                               simpleName,
                               this._returnType,
                               this.parameters)
      : super(reflectee,
              simpleName);

  Map<Symbol, Mirror> get members => new Map<Symbol,Mirror>();
  Map<Symbol, MethodMirror> get constructors => new Map<Symbol,MethodMirror>();

  var _returnType;
  TypeMirror get returnType {
    if (_returnType is! Mirror) {
      _returnType = _returnType.resolve(mirrors);
    }
    return _returnType;
  }

  final List<ParameterMirror> parameters;
  final Map<Symbol, TypeVariableMirror> typeVariables = const {};

  String toString() => "FunctionTypeMirror on '${_n(simpleName)}'";
}

abstract class _LocalDeclarationMirrorImpl extends _LocalMirrorImpl
    implements DeclarationMirror {
  _LocalDeclarationMirrorImpl(this._reflectee, this.simpleName);

  final _reflectee;

  final Symbol simpleName;

  Symbol _qualifiedName = null;
  Symbol get qualifiedName {
    if (_qualifiedName == null) {
      _qualifiedName = _computeQualifiedName(owner, simpleName);
    }
    return _qualifiedName;
  }

  List<InstanceMirror> get metadata {
    // Get the metadata objects, convert them into InstanceMirrors using
    // reflect() and then make them into a Dart list.
    return _metadata(_reflectee).map(reflect).toList(growable:false);
  }
}

class _LazyTypeVariableMirror {
  _LazyTypeVariableMirror(String variableName, this._owner)
      : this._variableName = _s(variableName);

  TypeVariableMirror resolve(MirrorSystem mirrors) {
    ClassMirror owner = _owner.resolve(mirrors);
    return owner.typeVariables[_variableName];
  }

  final Symbol _variableName;
  final _LazyTypeMirror _owner;
}

class _LocalTypeVariableMirrorImpl extends _LocalDeclarationMirrorImpl
    implements TypeVariableMirror {
  _LocalTypeVariableMirrorImpl(reflectee,
                               String simpleName,
                               this._owner)
      : super(reflectee, _s(simpleName));

  var _owner;
  DeclarationMirror get owner {
    if (_owner == null) {
      _owner = _LocalTypeVariableMirror_owner(_reflectee);
    }
    // TODO(11897): This will go away, as soon as lazy mirrors go away.
    if (_owner is! Mirror) {
      _owner = _owner.resolve(mirrors);
    }
    return _owner;
  }

  bool get isPrivate => false;

  final bool isTopLevel = false;

  SourceLocation get location {
    throw new UnimplementedError(
        'TypeVariableMirror.location is not implemented');
  }

  TypeMirror _upperBound = null;
  TypeMirror get upperBound {
    if (_upperBound == null) {
      _upperBound = _LocalTypeVariableMirror_upper_bound(_reflectee);
    }
    return _upperBound;
  }

  List<InstanceMirror> get metadata {
    throw new UnimplementedError(
        'TypeVariableMirror.metadata is not implemented');
  }

  String toString() => "TypeVariableMirror on '${_n(simpleName)}'";

  static DeclarationMirror _LocalTypeVariableMirror_owner(reflectee)
      native "LocalTypeVariableMirror_owner";

  static TypeMirror _LocalTypeVariableMirror_upper_bound(reflectee)
      native "LocalTypeVariableMirror_upper_bound";
}


class _LocalTypedefMirrorImpl extends _LocalDeclarationMirrorImpl
    implements TypedefMirror {
  _LocalTypedefMirrorImpl(reflectee,
                          String simpleName,
                          this._owner,
                          this._referent)
      : super(reflectee, _s(simpleName));

  var _owner;
  DeclarationMirror get owner {
    if (_owner == null) {
      _owner = _LocalClassMirrorImpl._library(_reflectee);
    }
    if (_owner is! Mirror) {
      _owner = _owner.resolve(mirrors);
    }
    return _owner;
  }

  bool get isPrivate => false;

  final bool isTopLevel = true;

  SourceLocation get location {
    throw new UnimplementedError(
        'TypedefMirror.location is not implemented');
  }

  var _referent;
  TypeMirror get referent {
    if (_referent is! Mirror) {
      _referent = _referent.resolve(mirrors);
    }
    return _referent;
  }

  String toString() => "TypedefMirror on '${_n(simpleName)}'";
}


class _LazyLibraryMirror {
  _LazyLibraryMirror(String this.libraryUrl);

  LibraryMirror resolve(MirrorSystem mirrors) {
    return mirrors.libraries[Uri.parse(libraryUrl)];
  }

  final String libraryUrl;
}

class _LocalLibraryMirrorImpl extends _LocalObjectMirrorImpl
    implements LibraryMirror {
  _LocalLibraryMirrorImpl(reflectee,
                          String simpleName,
                          String url)
      : this.simpleName = _s(simpleName),
        this.uri = Uri.parse(url),
        super(reflectee);

  final Symbol simpleName;

  // The simple name and the qualified name are the same for a library.
  Symbol get qualifiedName => simpleName;

  // Always null for libraries.
  final DeclarationMirror owner = null;

  // Always false for libraries.
  final bool isPrivate = false;

  // Always false for libraries.
  final bool isTopLevel = false;

  SourceLocation get location {
    throw new UnimplementedError(
        'LibraryMirror.location is not implemented');
  }

  final Uri uri;

  Map<Symbol, Mirror> _members;

  Map<Symbol, Mirror> get members {
    if (_members == null) {
      _members = _makeMemberMap(_computeMembers(_reflectee));
    }
    return _members;
  }

  Map<Symbol, ClassMirror> _classes = null;
  Map<Symbol, MethodMirror> _functions = null;
  Map<Symbol, MethodMirror> _getters = null;
  Map<Symbol, MethodMirror> _setters = null;
  Map<Symbol, VariableMirror> _variables = null;

  Map<Symbol, ClassMirror> get classes {
    if (_classes == null) {
      _classes = _filterMap(members,
                            (key, value) => (value is ClassMirror));
    }
    return _classes;
  }

  Map<Symbol, MethodMirror> get functions {
    if (_functions == null) {
      _functions = _filterMap(members,
                              (key, value) => (value is MethodMirror));
    }
    return _functions;
  }

  Map<Symbol, MethodMirror> get getters {
    if (_getters == null) {
      _getters = _filterMap(functions,
                            (key, value) => (value.isGetter));
    }
    return _getters;
  }

  Map<Symbol, MethodMirror> get setters {
    if (_setters == null) {
      _setters = _filterMap(functions,
                            (key, value) => (value.isSetter));
    }
    return _setters;
  }

  Map<Symbol, VariableMirror> get variables {
    if (_variables == null) {
      _variables = _filterMap(members,
                              (key, value) => (value is VariableMirror));
    }
    return _variables;
  }

  List<InstanceMirror> get metadata {
    // Get the metadata objects, convert them into InstanceMirrors using
    // reflect() and then make them into a Dart list.
    return _metadata(_reflectee).map(reflect).toList(growable:false);
  }

  String toString() => "LibraryMirror on '${_n(simpleName)}'";

  _invoke(reflectee, memberName, positionalArguments)
      native 'LibraryMirror_invoke';

  _invokeGetter(reflectee, getterName)
      native 'LibraryMirror_invokeGetter';

  _invokeSetter(reflectee, setterName, value)
      native 'LibraryMirror_invokeSetter';

  _computeMembers(reflectee)
      native "LibraryMirror_members";
}

class _LocalMethodMirrorImpl extends _LocalDeclarationMirrorImpl
    implements MethodMirror {
  _LocalMethodMirrorImpl(reflectee,
                         String simpleName,
                         this._owner,
                         this.isStatic,
                         this.isAbstract,
                         this.isGetter,
                         this.isSetter,
                         this.isConstructor,
                         this.isConstConstructor,
                         this.isGenerativeConstructor,
                         this.isRedirectingConstructor,
                         this.isFactoryConstructor)
      : super(reflectee, _s(simpleName));

  var _owner;
  DeclarationMirror get owner {
    // For nested closures it is possible, that the mirror for the owner has not
    // been created yet.
    if (_owner == null) {
      _owner = _MethodMirror_owner(_reflectee);
    }
    // TODO(11897): This will go away, as soon as lazy mirrors go away.
    if (_owner is! Mirror) {
      _owner = _owner.resolve(mirrors);
    }
    return _owner;
  }

  bool get isPrivate {
    return _n(simpleName).startsWith('_') ||
        _n(constructorName).startsWith('_');
  }

  bool get isTopLevel =>  owner is LibraryMirror;

  SourceLocation get location {
    throw new UnimplementedError(
        'MethodMirror.location is not implemented');
  }

  TypeMirror _returnType = null;
  TypeMirror get returnType {
    if (_returnType == null) {
      if (isConstructor) {
        _returnType = owner;
      } else {
        _returnType = _MethodMirror_return_type(_reflectee);
      }
    }
    return _returnType;
  }

  List<ParameterMirror> _parameters = null;
  List<ParameterMirror> get parameters {
    if (_parameters == null) {
      _parameters = _MethodMirror_parameters(_reflectee);
    }
    return _parameters;
  }

  final bool isStatic;
  final bool isAbstract;

  bool get isRegularMethod => !isGetter && !isSetter && !isConstructor;

  TypeMirror get isOperator {
    throw new UnimplementedError(
        'MethodMirror.isOperator is not implemented');
  }

  final bool isGetter;
  final bool isSetter;
  final bool isConstructor;

  Symbol _constructorName = null;
  Symbol get constructorName {
    if (_constructorName == null) {
      if (!isConstructor) {
        _constructorName = _s('');
      } else {
        var parts = _n(simpleName).split('.');
        if (parts.length > 2) {
          throw new MirrorException(
              'Internal error in MethodMirror.constructorName: '
              'malformed name <$simpleName>');
        } else if (parts.length == 2) {
          _constructorName = _s(parts[1]);
        } else {
          _constructorName = _s('');
        }
      }
    }
    return _constructorName;
  }

  final bool isConstConstructor;
  final bool isGenerativeConstructor;
  final bool isRedirectingConstructor;
  final bool isFactoryConstructor;

  String toString() => "MethodMirror on '${_n(simpleName)}'";

  static dynamic _MethodMirror_owner(reflectee)
      native "MethodMirror_owner";

  static dynamic _MethodMirror_return_type(reflectee)
      native "MethodMirror_return_type";

  static List<MethodMirror> _MethodMirror_parameters(reflectee)
      native "MethodMirror_parameters";
}

class _LocalVariableMirrorImpl extends _LocalDeclarationMirrorImpl
    implements VariableMirror {
  _LocalVariableMirrorImpl(reflectee,
                           String simpleName,
                           this._owner,
                           this._type,
                           this.isStatic,
                           this.isFinal)
      : super(reflectee, _s(simpleName));

  var _owner;
  DeclarationMirror get owner {
    if (_owner is! Mirror) {
      _owner = _owner.resolve(mirrors);
    }
    return _owner;
  }

  bool get isPrivate {
    return _n(simpleName).startsWith('_');
  }

  bool get isTopLevel {
    return owner is LibraryMirror;
  }

  SourceLocation get location {
    throw new UnimplementedError(
        'VariableMirror.location is not implemented');
  }

  var _type;
  TypeMirror get type {
    if (_type == null) {
       _type = _VariableMirror_type(_reflectee);
    }
    if (_type is! Mirror) {
      _type = _type.resolve(mirrors);
    }
    return _type;
  }

  final bool isStatic;
  final bool isFinal;

  String toString() => "VariableMirror on '${_n(simpleName)}'";

  static _VariableMirror_type(reflectee)
      native "VariableMirror_type";
}

class _LocalParameterMirrorImpl extends _LocalVariableMirrorImpl
    implements ParameterMirror {
  _LocalParameterMirrorImpl(this._reflectee,
                            this._position,
                            this.isOptional)
      : super(null, '<TODO:unnamed>', null, null, false, false);

  final _MirrorReference _reflectee;
  final int _position;
  final bool isOptional;

  String get defaultValue {
    throw new UnimplementedError(
        'ParameterMirror.defaultValue is not implemented');
  }

  bool get hasDefaultValue {
    throw new UnimplementedError(
        'ParameterMirror.hasDefaultValue is not implemented');
  }

  // TODO(11418): Implement.
  List<InstanceMirror> get metadata {
    throw new UnimplementedError(
        'ParameterMirror.metadata is not implemented');
  }

  TypeMirror _type = null;
  TypeMirror get type {
    if (_type == null) {
      _type = _ParameterMirror_type(_reflectee, _position);
    }
    return _type;
  }

  static TypeMirror _ParameterMirror_type(_reflectee, _position)
      native "ParameterMirror_type";
}

class _SpecialTypeMirrorImpl extends _LocalMirrorImpl
    implements TypeMirror, DeclarationMirror {
  _SpecialTypeMirrorImpl(String name) : simpleName = _s(name);

  final bool isPrivate = false;
  final bool isTopLevel = true;

  // Fixed length 0, therefore immutable.
  final List<InstanceMirror> metadata = new List(0);

  final DeclarationMirror owner = null;
  final Symbol simpleName;

  SourceLocation get location {
    throw new UnimplementedError(
        'TypeMirror.location is not implemented');
  }

  Symbol get qualifiedName {
    return simpleName;
  }

  String toString() => "TypeMirror on '${_n(simpleName)}'";

  // TODO(11955): Remove once dynamicType and voidType are canonical objects in
  // the object store.
  operator ==(other) {
    if (other is! _SpecialTypeMirrorImpl) {
      return false;
    }
    return this.simpleName == other.simpleName;
  }

  int get hashCode => simpleName.hashCode;
}

class _Mirrors {
  // Does a port refer to our local isolate?
  static bool isLocalPort(SendPort port) native 'Mirrors_isLocalPort';

  static MirrorSystem _currentMirrorSystem = null;

  // Creates a new local MirrorSystem.
  static MirrorSystem makeLocalMirrorSystem()
      native 'Mirrors_makeLocalMirrorSystem';

  // The MirrorSystem for the current isolate.
  static MirrorSystem currentMirrorSystem() {
    if (_currentMirrorSystem == null) {
      _currentMirrorSystem = makeLocalMirrorSystem();
    }
    return _currentMirrorSystem;
  }

  static Future<MirrorSystem> mirrorSystemOf(SendPort port) {
    if (isLocalPort(port)) {
      // Make a local mirror system.
      try {
        return new Future<MirrorSystem>.value(currentMirrorSystem());
      } catch (exception) {
        return new Future<MirrorSystem>.error(exception);
      }
    } else {
      // Make a remote mirror system
      throw new UnimplementedError(
          'Remote mirror support is not implemented');
    }
  }

  // Creates a new local mirror for some Object.
  static InstanceMirror reflect(Object reflectee) {
    return reflectee is Function 
        ? new _LocalClosureMirrorImpl(reflectee)
        : new _LocalInstanceMirrorImpl(reflectee);
  }

  // Creates a new local ClassMirror.
  static ClassMirror makeLocalClassMirror(Type key)
      native "Mirrors_makeLocalClassMirror";

  static Expando<ClassMirror> _classMirrorCache = new Expando("ClassMirror");
  static ClassMirror reflectClass(Type key) {
    var classMirror = _classMirrorCache[key];
    if (classMirror == null) {
      classMirror = makeLocalClassMirror(key);
      _classMirrorCache[key] = classMirror;
    }
    return classMirror;
  }
}
