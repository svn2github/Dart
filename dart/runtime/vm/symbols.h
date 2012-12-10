// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_SYMBOLS_H_
#define VM_SYMBOLS_H_

#include "vm/object.h"
#include "vm/snapshot_ids.h"

namespace dart {

// Forward declarations.
class Isolate;
class ObjectPointerVisitor;

#define PREDEFINED_SYMBOLS_LIST(V)                                             \
  V(Empty, "")                                                                 \
  V(Dot, ".")                                                                  \
  V(Equals, "=")                                                               \
  V(EqualOperator, "==")                                                       \
  V(Identical, "identical")                                                    \
  V(Length, "length")                                                          \
  V(IndexToken, "[]")                                                          \
  V(AssignIndexToken, "[]=")                                                   \
  V(TopLevel, "::")                                                            \
  V(DefaultLabel, ":L")                                                        \
  V(This, "this")                                                              \
  V(Super, "super")                                                            \
  V(Call, "call")                                                              \
  V(HasNext, "hasNext")                                                        \
  V(Next, "next")                                                              \
  V(Value, "value")                                                            \
  V(ExprTemp, ":expr_temp")                                                    \
  V(AnonymousClosure, "<anonymous closure>")                                   \
  V(ClosureParameter, ":closure")                                              \
  V(PhaseParameter, ":phase")                                                  \
  V(TypeArgumentsParameter, ":type_arguments")                                 \
  V(AssertionError, "AssertionErrorImplementation")                            \
  V(TypeError, "TypeErrorImplementation")                                      \
  V(FallThroughError, "FallThroughErrorImplementation")                        \
  V(AbstractClassInstantiationError,                                           \
    "AbstractClassInstantiationErrorImplementation")                           \
  V(NoSuchMethodError, "NoSuchMethodErrorImplementation")                      \
  V(ThrowNew, "_throwNew")                                                     \
  V(List, "List")                                                              \
  V(ListLiteralFactory, "List._fromLiteral")                                   \
  V(ListFactory, "List.")                                                      \
  V(Map, "Map")                                                                \
  V(MapLiteralFactory, "Map._fromLiteral")                                     \
  V(ImmutableMap, "ImmutableMap")                                              \
  V(ImmutableMapConstructor, "ImmutableMap._create")                           \
  V(StringBase, "_StringBase")                                                 \
  V(Interpolate, "_interpolate")                                               \
  V(GetIterator, "iterator")                                                   \
  V(NoSuchMethod, "noSuchMethod")                                              \
  V(SavedArgDescVarPrefix, ":saved_args_desc_var")                             \
  V(SavedEntryContextVar, ":saved_entry_context_var")                          \
  V(SavedContextVar, ":saved_context_var")                                     \
  V(ExceptionVar, ":exception_var")                                            \
  V(StacktraceVar, ":stacktrace_var")                                          \
  V(ListLiteralElement, "list literal element")                                \
  V(ForInIter, ":for-in-iter")                                                 \
  V(Library, "library")                                                        \
  V(Import, "import")                                                          \
  V(Source, "source")                                                          \
  V(Class, "Class")                                                            \
  V(Null, "Null")                                                              \
  V(Dynamic, "dynamic")                                                        \
  V(Void, "void")                                                              \
  V(UnresolvedClass, "UnresolvedClass")                                        \
  V(Type, "_Type")                                                             \
  V(TypeParameter, "_TypeParameter")                                           \
  V(TypeArguments, "TypeArguments")                                            \
  V(InstantiatedTypeArguments, "InstantiatedTypeArguments")                    \
  V(PatchClass, "PatchClass")                                                  \
  V(Function, "Function")                                                      \
  V(FunctionResult, "function result")                                         \
  V(FactoryResult, "factory result")                                           \
  V(ClosureData, "ClosureData")                                                \
  V(RedirectionData, "RedirectionData")                                        \
  V(Field, "Field")                                                            \
  V(LiteralToken, "LiteralToken")                                              \
  V(TokenStream, "TokenStream")                                                \
  V(Script, "Script")                                                          \
  V(LibraryClass, "Library")                                                   \
  V(LibraryPrefix, "LibraryPrefix")                                            \
  V(Namespace, "Namespace")                                                    \
  V(Code, "Code")                                                              \
  V(Instructions, "Instructions")                                              \
  V(PcDescriptors, "PcDescriptors")                                            \
  V(Stackmap, "Stackmap")                                                      \
  V(LocalVarDescriptors, "LocalVarDescriptors")                                \
  V(ExceptionHandlers, "ExceptionHandlers")                                    \
  V(DeoptInfo, "DeoptInfo")                                                    \
  V(Context, "Context")                                                        \
  V(ContextScope, "ContextScope")                                              \
  V(ICData, "ICData")                                                          \
  V(MegamorphicCache, "MegamorphicCache")                                      \
  V(SubtypeTestCache, "SubtypeTestCache")                                      \
  V(ApiError, "ApiError")                                                      \
  V(LanguageError, "LanguageError")                                            \
  V(UnhandledException, "UnhandledException")                                  \
  V(UnwindError, "UnwindError")                                                \
  V(IntegerImplementation, "_IntegerImplementation")                           \
  V(Number, "num")                                                             \
  V(Smi, "_Smi")                                                               \
  V(Mint, "_Mint")                                                             \
  V(Bigint, "_Bigint")                                                         \
  V(Double, "_Double")                                                         \
  V(Bool, "bool")                                                              \
  V(ObjectArray, "_ObjectArray")                                               \
  V(GrowableObjectArray, "_GrowableObjectArray")                               \
  V(ImmutableArray, "_ImmutableArray")                                         \
  V(OneByteString, "_OneByteString")                                           \
  V(TwoByteString, "_TwoByteString")                                           \
  V(ExternalOneByteString, "_ExternalOneByteString")                           \
  V(ExternalTwoByteString, "_ExternalTwoByteString")                           \
  V(Stacktrace, "Stacktrace")                                                  \
  V(JSSyntaxRegExp, "_JSSyntaxRegExp")                                         \
  V(Object, "Object")                                                          \
  V(_Int8Array, "_Int8Array")                                                  \
  V(_Uint8Array, "_Uint8Array")                                                \
  V(_Uint8ClampedArray, "_Uint8ClampedArray")                                  \
  V(_Int16Array, "_Int16Array")                                                \
  V(_Uint16Array, "_Uint16Array")                                              \
  V(_Int32Array, "_Int32Array")                                                \
  V(_Uint32Array, "_Uint32Array")                                              \
  V(_Int64Array, "_Int64Array")                                                \
  V(_Uint64Array, "_Uint64Array")                                              \
  V(_Float32Array, "_Float32Array")                                            \
  V(_Float64Array, "_Float64Array")                                            \
  V(_ExternalInt8Array, "_ExternalInt8Array")                                  \
  V(_ExternalUint8Array, "_ExternalUint8Array")                                \
  V(_ExternalUint8ClampedArray, "_ExternalUint8ClampedArray")                  \
  V(_ExternalInt16Array, "_ExternalInt16Array")                                \
  V(_ExternalUint16Array, "_ExternalUint16Array")                              \
  V(_ExternalInt32Array, "_ExternalInt32Array")                                \
  V(_ExternalUint32Array, "_ExternalUint32Array")                              \
  V(_ExternalInt64Array, "_ExternalInt64Array")                                \
  V(_ExternalUint64Array, "_ExternalUint64Array")                              \
  V(_ExternalFloat32Array, "_ExternalFloat32Array")                            \
  V(_ExternalFloat64Array, "_ExternalFloat64Array")                            \
  V(_WeakProperty, "_WeakProperty")                                            \
  V(InvocationMirror, "_InvocationMirror")                                     \
  V(AllocateInvocationMirror, "_allocateInvocationMirror")                     \

// Contains a list of frequently used strings in a canonicalized form. This
// list is kept in the vm_isolate in order to share the copy across isolates
// without having to maintain copies in each isolate.
class Symbols : public AllStatic {
 public:
  enum { kMaxOneCharCodeSymbol = 0xFF };

  // List of strings that are pre created in the vm isolate.
  enum SymbolId {
    kIllegal = 0,

#define DEFINE_SYMBOL_INDEX(symbol, literal)                                   \
    k##symbol,
PREDEFINED_SYMBOLS_LIST(DEFINE_SYMBOL_INDEX)
#undef DEFINE_SYMBOL_INDEX
    kMaxPredefinedId,
    kNullCharId = kMaxPredefinedId,
    kMaxId = kNullCharId + kMaxOneCharCodeSymbol + 1,
  };

  // Access methods for symbols stored in the vm isolate.
#define DEFINE_SYMBOL_ACCESSOR(symbol, literal)                                \
  static RawString* symbol() { return predefined_[k##symbol]; }
PREDEFINED_SYMBOLS_LIST(DEFINE_SYMBOL_ACCESSOR)
#undef DEFINE_SYMBOL_ACCESSOR

  // Initialize frequently used symbols in the vm isolate.
  static void InitOnce(Isolate* isolate);

  // Initialize and setup a symbol table for the isolate.
  static void SetupSymbolTable(Isolate* isolate);

  // Get number of symbols in an isolate's symbol table.
  static intptr_t Size(Isolate* isolate);

  // Creates a Symbol given a C string that is assumed to contain
  // UTF-8 encoded characters and '\0' is considered a termination character.
  // TODO(7123) - Rename this to FromCString(....).
  static RawString* New(const char* cstr);

  // Creates a new Symbol from an array of UTF-8 encoded characters.
  static RawString* FromUTF8(const uint8_t* utf8_array, intptr_t len);

  // Creates a new Symbol from an array of Latin-1 encoded characters.
  static RawString* FromLatin1(const uint8_t* latin1_array, intptr_t len);

  // Creates a new Symbol from an array of UTF-16 encoded characters.
  static RawString* FromUTF16(const uint16_t* utf16_array, intptr_t len);

  // Creates a new Symbol from an array of UTF-32 encoded characters.
  static RawString* FromUTF32(const int32_t* utf32_array, intptr_t len);

  static RawString* New(const String& str);
  static RawString* New(const String& str,
                        intptr_t begin_index,
                        intptr_t length);

  // Returns char* of predefined symbol.
  static const char* Name(SymbolId symbol);

  static RawString* FromCharCode(int32_t char_code);

  static RawString** PredefinedAddress() {
    return reinterpret_cast<RawString**>(&predefined_);
  }

 private:
  enum {
    kInitialVMIsolateSymtabSize = ((kMaxId + 15) & -16),
    kInitialSymtabSize = 256
  };

  // Helper functions to create a symbol given a string or set of characters.
  template<typename CharacterType, typename CallbackType>
  static RawString* NewSymbol(const CharacterType* characters,
                              intptr_t len,
                              CallbackType new_string);

  // Add the string into the VM isolate symbol table.
  static void Add(const Array& symbol_table, const String& str);

  // Insert symbol into symbol table, growing it if necessary.
  static void InsertIntoSymbolTable(const Array& symbol_table,
                                    const String& symbol,
                                    intptr_t index);

  // Grow the symbol table.
  static void GrowSymbolTable(const Array& symbol_table);

  // Return index in symbol table if the symbol already exists or
  // return the index into which the new symbol can be added.
  template<typename T>
  static intptr_t FindIndex(const Array& symbol_table,
                            const T* characters,
                            intptr_t len,
                            intptr_t hash);
  static intptr_t FindIndex(const Array& symbol_table,
                            const String& str,
                            intptr_t begin_index,
                            intptr_t len,
                            intptr_t hash);
  static intptr_t LookupVMSymbol(RawObject* obj);
  static RawObject* GetVMSymbol(intptr_t object_id);
  static bool IsVMSymbolId(intptr_t object_id) {
    return (object_id >= kMaxPredefinedObjectIds &&
            object_id < (kMaxPredefinedObjectIds + kMaxId));
  }

  // List of symbols that are stored in the vm isolate for easy access.
  static RawString* predefined_[kMaxId];

  friend class String;
  friend class SnapshotReader;
  friend class SnapshotWriter;
  friend class ApiMessageReader;

  DISALLOW_COPY_AND_ASSIGN(Symbols);
};

}  // namespace dart

#endif  // VM_SYMBOLS_H_
