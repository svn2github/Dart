// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_INTERMEDIATE_LANGUAGE_H_
#define VM_INTERMEDIATE_LANGUAGE_H_

#include "vm/allocation.h"
#include "vm/ast.h"
#include "vm/growable_array.h"
#include "vm/handles_impl.h"
#include "vm/locations.h"
#include "vm/object.h"
#include "vm/parser.h"

namespace dart {

DECLARE_FLAG(bool, throw_on_javascript_int_overflow);

class BitVector;
class BlockEntryInstr;
class BufferFormatter;
class CatchBlockEntryInstr;
class ComparisonInstr;
class Definition;
class Environment;
class FlowGraph;
class FlowGraphBuilder;
class FlowGraphCompiler;
class FlowGraphVisitor;
class Instruction;
class LocalVariable;
class ParsedFunction;
class Range;


// TODO(srdjan): Unify with INTRINSIC_LIST.
// (class-name, function-name, recognized enum, fingerprint).
// See intrinsifier for fingerprint computation.
#define RECOGNIZED_LIST(V)                                                     \
  V(::, identical, ObjectIdentical, 496869842)                                 \
  V(Object, ==, ObjectEquals, 1068471689)                                      \
  V(Object, Object., ObjectConstructor, 1066669787)                            \
  V(Object, get:_cid, ObjectCid, 1771959715)                                   \
  V(_TypedListBase, get:_cid, TypedListBaseCid, 1438451377)                    \
  V(_List, get:length, ObjectArrayLength, 1181352729)                          \
  V(_List, _List., ObjectArrayConstructor, 1595327584)                         \
  V(_ImmutableList, get:length, ImmutableArrayLength, 274917727)               \
  V(_TypedList, get:length, TypedDataLength, 522565357)                        \
  V(_TypedList, _getInt8, ByteArrayBaseGetInt8, 1541411498)                    \
  V(_TypedList, _getUint8, ByteArrayBaseGetUint8, 1032404349)                  \
  V(_TypedList, _getInt16, ByteArrayBaseGetInt16, 381073990)                   \
  V(_TypedList, _getUint16, ByteArrayBaseGetUint16, 1142676276)                \
  V(_TypedList, _getInt32, ByteArrayBaseGetInt32, 330269934)                   \
  V(_TypedList, _getUint32, ByteArrayBaseGetUint32, 59490554)                  \
  V(_TypedList, _getFloat32, ByteArrayBaseGetFloat32, 393003933)               \
  V(_TypedList, _getFloat64, ByteArrayBaseGetFloat64, 1792407200)              \
  V(_TypedList, _getFloat32x4, ByteArrayBaseGetFloat32x4, 1338379857)          \
  V(_TypedList, _getInt32x4, ByteArrayBaseGetInt32x4, 1469917805)              \
  V(_TypedList, _setInt8, ByteArrayBaseSetInt8, 433348464)                     \
  V(_TypedList, _setUint8, ByteArrayBaseSetUint8, 149406583)                   \
  V(_TypedList, _setInt16, ByteArrayBaseSetInt16, 805477162)                   \
  V(_TypedList, _setUint16, ByteArrayBaseSetUint16, 888580944)                 \
  V(_TypedList, _setInt32, ByteArrayBaseSetInt32, 1708248181)                  \
  V(_TypedList, _setUint32, ByteArrayBaseSetUint32, 1863152792)                \
  V(_TypedList, _setFloat32, ByteArrayBaseSetFloat32, 1148703855)              \
  V(_TypedList, _setFloat64, ByteArrayBaseSetFloat64, 972883980)               \
  V(_TypedList, _setFloat32x4, ByteArrayBaseSetFloat32x4, 950522310)           \
  V(_TypedList, _setInt32x4, ByteArrayBaseSetInt32x4, 1301138078)              \
  V(_GrowableList, get:length, GrowableArrayLength, 778505107)                 \
  V(_GrowableList, get:_capacity, GrowableArrayCapacity, 555140075)            \
  V(_GrowableList, _setData, GrowableArraySetData, 2126927509)                 \
  V(_GrowableList, _setLength, GrowableArraySetLength, 89389299)               \
  V(_StringBase, get:length, StringBaseLength, 784399628)                      \
  V(_StringBase, get:isEmpty, StringBaseIsEmpty, 49873871)                     \
  V(_StringBase, codeUnitAt, StringBaseCodeUnitAt, 397735324)                  \
  V(_StringBase, [], StringBaseCharAt, 1512210677)                             \
  V(_StringBase, _interpolate, StringBaseInterpolate, 172499441)               \
  V(_OneByteString, _setAt, OneByteStringSetAt, 468605749)                     \
  V(_IntegerImplementation, toDouble, IntegerToDouble, 1084977108)             \
  V(_IntegerImplementation, _leftShiftWithMask32, IntegerLeftShiftWithMask32,  \
      597111055)                                                               \
  V(_Double, toInt, DoubleToInteger, 1547535151)                               \
  V(_Double, truncateToDouble, DoubleTruncate, 2117801967)                     \
  V(_Double, roundToDouble, DoubleRound, 2124216110)                           \
  V(_Double, floorToDouble, DoubleFloor, 968600699)                            \
  V(_Double, ceilToDouble, DoubleCeil, 1779929274)                             \
  V(_Double, _modulo, DoubleMod, 1473971007)                                   \
  V(::, sqrt, MathSqrt, 101545548)                                             \
  V(::, sin, MathSin, 1741396147)                                              \
  V(::, cos, MathCos, 1951197905)                                              \
  V(::, min, MathMin, 1022567780)                                              \
  V(::, max, MathMax, 612058870)                                               \
  V(::, _doublePow, MathDoublePow, 823139975)                                  \
  V(Float32x4, Float32x4., Float32x4Constructor, 1755873079)                   \
  V(Float32x4, Float32x4.zero, Float32x4Zero, 1494069379)                      \
  V(Float32x4, Float32x4.splat, Float32x4Splat, 916211464)                     \
  V(Float32x4, Float32x4.fromInt32x4Bits, Float32x4FromInt32x4Bits,            \
      640076216)                                                               \
  V(Float32x4, Float32x4.fromFloat64x2, Float32x4FromFloat64x2, 1279591344)    \
  V(_Float32x4, shuffle, Float32x4Shuffle, 1636488139)                         \
  V(_Float32x4, shuffleMix, Float32x4ShuffleMix, 597555927)                    \
  V(_Float32x4, get:x, Float32x4ShuffleX, 384850558)                           \
  V(_Float32x4, get:y, Float32x4ShuffleY, 1398002778)                          \
  V(_Float32x4, get:z, Float32x4ShuffleZ, 1178056441)                          \
  V(_Float32x4, get:w, Float32x4ShuffleW, 480831839)                           \
  V(_Float32x4, get:signMask, Float32x4GetSignMask, 630761511)                 \
  V(_Float32x4, _cmpequal, Float32x4Equal, 571062952)                          \
  V(_Float32x4, _cmpgt, Float32x4GreaterThan, 1613543295)                      \
  V(_Float32x4, _cmpgte, Float32x4GreaterThanOrEqual, 589402909)               \
  V(_Float32x4, _cmplt, Float32x4LessThan, 1502332656)                         \
  V(_Float32x4, _cmplte, Float32x4LessThanOrEqual, 1069848031)                 \
  V(_Float32x4, _cmpnequal, Float32x4NotEqual, 1334574472)                     \
  V(_Float32x4, _min, Float32x4Min, 2036349551)                                \
  V(_Float32x4, _max, Float32x4Max, 571688115)                                 \
  V(_Float32x4, _scale, Float32x4Scale, 1311297761)                            \
  V(_Float32x4, _sqrt, Float32x4Sqrt, 1709659395)                              \
  V(_Float32x4, _reciprocalSqrt, Float32x4ReciprocalSqrt, 2043980962)          \
  V(_Float32x4, _reciprocal, Float32x4Reciprocal, 739405237)                   \
  V(_Float32x4, _negate, Float32x4Negate, 445839777)                           \
  V(_Float32x4, _abs, Float32x4Absolute, 1152777608)                           \
  V(_Float32x4, _clamp, Float32x4Clamp, 353415442)                             \
  V(_Float32x4, withX, Float32x4WithX, 1446546696)                             \
  V(_Float32x4, withY, Float32x4WithY, 309844761)                              \
  V(_Float32x4, withZ, Float32x4WithZ, 971921505)                              \
  V(_Float32x4, withW, Float32x4WithW, 1759699726)                             \
  V(Float64x2, Float64x2., Float64x2Constructor, 1399581872)                   \
  V(Float64x2, Float64x2.zero, Float64x2Zero, 1836770587)                      \
  V(Float64x2, Float64x2.splat, Float64x2Splat, 939291159)                     \
  V(Float64x2, Float64x2.fromFloat32x4, Float64x2FromFloat32x4, 1499726406)    \
  V(_Float64x2, get:x, Float64x2GetX, 261044094)                               \
  V(_Float64x2, get:y, Float64x2GetY, 1942257886)                              \
  V(_Float64x2, _negate, Float64x2Negate, 2133212774)                          \
  V(_Float64x2, abs, Float64x2Abs, 1224776282)                                 \
  V(_Float64x2, sqrt, Float64x2Sqrt, 1037569520)                               \
  V(_Float64x2, get:signMask, Float64x2GetSignMask, 252936800)                 \
  V(_Float64x2, scale, Float64x2Scale, 1199438744)                             \
  V(_Float64x2, withX, Float64x2WithX, 1042725932)                             \
  V(_Float64x2, withY, Float64x2WithY, 1496958947)                             \
  V(_Float64x2, min, Float64x2Min, 485240583)                                  \
  V(_Float64x2, max, Float64x2Max, 2146148204)                                 \
  V(Int32x4, Int32x4.bool, Int32x4BoolConstructor, 87082660)                   \
  V(Int32x4, Int32x4.fromFloat32x4Bits, Int32x4FromFloat32x4Bits,              \
      372517418)                                                               \
  V(_Int32x4, get:flagX, Int32x4GetFlagX, 1077555238)                          \
  V(_Int32x4, get:flagY, Int32x4GetFlagY, 779160284)                           \
  V(_Int32x4, get:flagZ, Int32x4GetFlagZ, 181912283)                           \
  V(_Int32x4, get:flagW, Int32x4GetFlagW, 977675534)                           \
  V(_Int32x4, get:signMask, Int32x4GetSignMask, 1929271914)                    \
  V(_Int32x4, shuffle, Int32x4Shuffle, 1870018702)                             \
  V(_Int32x4, shuffleMix, Int32x4ShuffleMix, 967644870)                        \
  V(_Int32x4, select, Int32x4Select, 1696037681)                               \
  V(_Int32x4, withFlagX, Int32x4WithFlagX, 467852789)                          \
  V(_Int32x4, withFlagY, Int32x4WithFlagY, 1903359978)                         \
  V(_Int32x4, withFlagZ, Int32x4WithFlagZ, 862460960)                          \
  V(_Int32x4, withFlagW, Int32x4WithFlagW, 1095242907)                         \
  V(_List, [], ObjectArrayGetIndexed, 795612476)                               \
  V(_List, []=, ObjectArraySetIndexed, 1288827575)                             \
  V(_ImmutableList, [], ImmutableArrayGetIndexed, 1990177341)                  \
  V(_GrowableList, [], GrowableArrayGetIndexed, 919108233)                     \
  V(_GrowableList, []=, GrowableArraySetIndexed, 1218649853)                   \
  V(_Float32Array, [], Float32ArrayGetIndexed, 856653338)                      \
  V(_Float32Array, []=, Float32ArraySetIndexed, 2086166464)                    \
  V(_Float64Array, [], Float64ArrayGetIndexed, 1779054297)                     \
  V(_Float64Array, []=, Float64ArraySetIndexed, 243929230)                     \
  V(_Int8Array, [], Int8ArrayGetIndexed, 321230586)                            \
  V(_Int8Array, []=, Int8ArraySetIndexed, 2050598685)                          \
  V(_Uint8Array, [], Uint8ArrayGetIndexed, 16125140)                           \
  V(_Uint8Array, []=, Uint8ArraySetIndexed, 2018064553)                        \
  V(_Uint8ClampedArray, [], Uint8ClampedArrayGetIndexed, 430672063)            \
  V(_Uint8ClampedArray, []=, Uint8ClampedArraySetIndexed, 821294340)           \
  V(_ExternalUint8Array, [], ExternalUint8ArrayGetIndexed, 1678777951)         \
  V(_ExternalUint8Array, []=, ExternalUint8ArraySetIndexed, 918478513)         \
  V(_ExternalUint8ClampedArray, [], ExternalUint8ClampedArrayGetIndexed,       \
    1346536303)                                                                \
  V(_ExternalUint8ClampedArray, []=, ExternalUint8ClampedArraySetIndexed,      \
    1794849214)                                                                \
  V(_Int16Array, [], Int16ArrayGetIndexed, 74127855)                           \
  V(_Int16Array, []=, Int16ArraySetIndexed, 1610252345)                        \
  V(_Uint16Array, [], Uint16ArrayGetIndexed, 470411953)                        \
  V(_Uint16Array, []=, Uint16ArraySetIndexed, 1648929040)                      \
  V(_Int32Array, [], Int32ArrayGetIndexed, 203101370)                          \
  V(_Int32Array, []=, Int32ArraySetIndexed, 338968571)                         \
  V(_Uint32Array, [], Uint32ArrayGetIndexed, 1640672852)                       \
  V(_Uint32Array, []=, Uint32ArraySetIndexed, 1472976717)                      \
  V(_Float32x4Array, [], Float32x4ArrayGetIndexed, 1466627059)                 \
  V(_Float32x4Array, []=, Float32x4ArraySetIndexed, 2141660076)                \
  V(_Int32x4Array, [], Int32x4ArrayGetIndexed, 818792056)                      \
  V(_Int32x4Array, []=, Int32x4ArraySetIndexed, 1021474038)                    \
  V(_Float64x2Array, [], Float64x2ArrayGetIndexed, 288114492)                  \
  V(_Float64x2Array, []=, Float64x2ArraySetIndexed, 941746736)                 \


// A list of core function that should always be inlined.
#define INLINE_WHITE_LIST(V)                                                   \
  V(_List, get:length, ObjectArrayLength, 1181352729)                          \
  V(_ImmutableList, get:length, ImmutableArrayLength, 274917727)               \
  V(_TypedList, get:length, TypedDataLength, 522565357)                        \
  V(_GrowableList, get:length, GrowableArrayLength, 778505107)                 \
  V(_StringBase, get:length, StringBaseLength, 784399628)                      \
  V(ListIterator, moveNext, ListIteratorMoveNext, 210829138)                   \
  V(_GrowableList, get:iterator, GrowableArrayIterator, 1812933946)            \
  V(_GrowableList, forEach, GrowableArrayForEach, 2085943947)                  \
  V(_List, [], ObjectArrayGetIndexed, 795612476)                               \
  V(_List, []=, ObjectArraySetIndexed, 1288827575)                             \
  V(_List, get:isEmpty, ObjectArrayIsEmpty, 2130247737)                        \
  V(_ImmutableList, [], ImmutableArrayGetIndexed, 1990177341)                  \
  V(_GrowableList, [], GrowableArrayGetIndexed, 919108233)                     \
  V(_GrowableList, []=, GrowableArraySetIndexed, 1218649853)                   \
  V(_Float32Array, [], Float32ArrayGetIndexed, 856653338)                      \
  V(_Float32Array, []=, Float32ArraySetIndexed, 2086166464)                    \
  V(_Float64Array, [], Float64ArrayGetIndexed, 1779054297)                     \
  V(_Float64Array, []=, Float64ArraySetIndexed, 243929230)                     \
  V(_Int8Array, [], Int8ArrayGetIndexed, 321230586)                            \
  V(_Int8Array, []=, Int8ArraySetIndexed, 2050598685)                          \
  V(_Uint8Array, [], Uint8ArrayGetIndexed, 16125140)                           \
  V(_Uint8Array, []=, Uint8ArraySetIndexed, 2018064553)                        \
  V(_Uint8ClampedArray, [], Uint8ClampedArrayGetIndexed, 430672063)            \
  V(_Uint8ClampedArray, []=, Uint8ClampedArraySetIndexed, 821294340)           \
  V(_Uint16Array, [], Uint16ArrayGetIndexed, 470411953)                        \
  V(_Uint16Array, []=, Uint16ArraySetIndexed, 1648929040)                      \
  V(_Int16Array, [], Int16ArrayGetIndexed, 74127855)                           \
  V(_Int16Array, []=, Int16ArraySetIndexed, 1610252345)                        \
  V(_Int32Array, [], Int32ArrayGetIndexed, 203101370)                          \
  V(_Int32Array, []=, Int32ArraySetIndexed, 338968571)                         \
  V(_Uint8ArrayView, [], Uint8ArrayViewGetIndexed, 1543480955)                 \
  V(_Uint8ArrayView, []=, Uint8ArrayViewSetIndexed, 936729641)                 \
  V(_Int8ArrayView, [], Int8ArrayViewGetIndexed, 1898018934)                   \
  V(_Int8ArrayView, []=, Int8ArrayViewSetIndexed, 111684506)                   \
  V(::, asin, MathASin, 1651042633)                                            \
  V(::, acos, MathACos, 1139647090)                                            \
  V(::, atan, MathATan, 1668754384)                                            \
  V(::, atan2, MathATan2, 1845649456)                                          \
  V(::, cos, MathCos, 1951197905)                                              \
  V(::, exp, MathExp, 1809210829)                                              \
  V(::, log, MathLog, 1620336448)                                              \
  V(::, max, MathMax, 612058870)                                               \
  V(::, min, MathMin, 1022567780)                                              \
  V(::, pow, MathPow, 930962530)                                               \
  V(::, sin, MathSin, 1741396147)                                              \
  V(::, sqrt, MathSqrt, 101545548)                                             \
  V(::, tan, MathTan, 982072809)                                               \


// A list of core functions that internally dispatch based on received id.
#define POLYMORPHIC_TARGET_LIST(V)                                             \
  V(_StringBase, [], StringBaseCharAt, 1512210677)                             \
  V(_StringBase, codeUnitAt, StringBaseCodeUnitAt, 397735324)                  \
  V(_TypedList, _getInt8, ByteArrayBaseGetInt8, 1541411498)                    \
  V(_TypedList, _getUint8, ByteArrayBaseGetUint8, 1032404349)                  \
  V(_TypedList, _getInt16, ByteArrayBaseGetInt16, 381073990)                   \
  V(_TypedList, _getUint16, ByteArrayBaseGetUint16, 1142676276)                \
  V(_TypedList, _getInt32, ByteArrayBaseGetInt32, 330269934)                   \
  V(_TypedList, _getUint32, ByteArrayBaseGetUint32, 59490554)                  \
  V(_TypedList, _getFloat32, ByteArrayBaseGetFloat32, 393003933)               \
  V(_TypedList, _getFloat64, ByteArrayBaseGetFloat64, 1792407200)              \
  V(_TypedList, _getFloat32x4, ByteArrayBaseGetFloat32x4, 1338379857)          \
  V(_TypedList, _getInt32x4, ByteArrayBaseGetInt32x4, 1469917805)              \
  V(_TypedList, _setInt8, ByteArrayBaseSetInt8, 433348464)                     \
  V(_TypedList, _setUint8, ByteArrayBaseSetInt8, 149406583)                    \
  V(_TypedList, _setInt16, ByteArrayBaseSetInt16, 805477162)                   \
  V(_TypedList, _setUint16, ByteArrayBaseSetInt16, 888580944)                  \
  V(_TypedList, _setInt32, ByteArrayBaseSetInt32, 1708248181)                  \
  V(_TypedList, _setUint32, ByteArrayBaseSetUint32, 1863152792)                \
  V(_TypedList, _setFloat32, ByteArrayBaseSetFloat32, 1148703855)              \
  V(_TypedList, _setFloat64, ByteArrayBaseSetFloat64, 972883980)               \
  V(_TypedList, _setFloat32x4, ByteArrayBaseSetFloat32x4, 950522310)           \
  V(_TypedList, _setInt32x4, ByteArrayBaseSetInt32x4, 1301138078)              \

// Class that recognizes the name and owner of a function and returns the
// corresponding enum. See RECOGNIZED_LIST above for list of recognizable
// functions.
class MethodRecognizer : public AllStatic {
 public:
  enum Kind {
    kUnknown,
#define DEFINE_ENUM_LIST(class_name, function_name, enum_name, fp) k##enum_name,
RECOGNIZED_LIST(DEFINE_ENUM_LIST)
#undef DEFINE_ENUM_LIST
  };

  static Kind RecognizeKind(const Function& function);
  static bool AlwaysInline(const Function& function);
  static bool PolymorphicTarget(const Function& function);
  static const char* KindToCString(Kind kind);
  static void InitializeState();
};


// CompileType describes type of the value produced by the definition.
//
// It captures the following properties:
//    - whether value can potentially be null or it is definitely not null;
//    - concrete class id of the value or kDynamicCid if unknown statically;
//    - abstract super type of the value, concrete type of the value in runtime
//      is guaranteed to be sub type of this type.
//
// Values of CompileType form a lattice with a None type as a bottom and a
// nullable Dynamic type as a top element. Method Union provides a join
// operation for the lattice.
class CompileType : public ValueObject {
 public:
  static const bool kNullable = true;
  static const bool kNonNullable = false;

  CompileType(bool is_nullable, intptr_t cid, const AbstractType* type)
      : is_nullable_(is_nullable), cid_(cid), type_(type) { }

  CompileType(const CompileType& other)
      : ValueObject(),
        is_nullable_(other.is_nullable_),
        cid_(other.cid_),
        type_(other.type_) { }

  CompileType& operator=(const CompileType& other) {
    is_nullable_ = other.is_nullable_;
    cid_ = other.cid_;
    type_ =  other.type_;
    return *this;
  }

  bool is_nullable() const { return is_nullable_; }

  // Return type such that concrete value's type in runtime is guaranteed to
  // be subtype of it.
  const AbstractType* ToAbstractType();

  // Return class id such that it is either kDynamicCid or in runtime
  // value is guaranteed to have an equal class id.
  intptr_t ToCid();

  // Return class id such that it is either kDynamicCid or in runtime
  // value is guaranteed to be either null or have an equal class id.
  intptr_t ToNullableCid();

  // Returns true if the value is guaranteed to be not-null or is known to be
  // always null.
  bool HasDecidableNullability();

  // Returns true if the value is known to be always null.
  bool IsNull();

  // Returns true if this type is more specific than given type.
  bool IsMoreSpecificThan(const AbstractType& other);

  // Returns true if value of this type is assignable to a location of the
  // given type.
  bool IsAssignableTo(const AbstractType& type) {
    bool is_instance;
    return CanComputeIsInstanceOf(type, kNullable, &is_instance) &&
           is_instance;
  }

  // Create a new CompileType representing given combination of class id and
  // abstract type. The pair is assumed to be coherent.
  static CompileType Create(intptr_t cid, const AbstractType& type);

  CompileType CopyNonNullable() const {
    return CompileType(kNonNullable, cid_, type_);
  }

  static CompileType CreateNullable(bool is_nullable, intptr_t cid) {
    return CompileType(is_nullable, cid, NULL);
  }

  // Create a new CompileType representing given abstract type. By default
  // values as assumed to be nullable.
  static CompileType FromAbstractType(const AbstractType& type,
                                      bool is_nullable = kNullable);

  // Create a new CompileType representing an value with the given class id.
  // Resulting CompileType is nullable only if cid is kDynamicCid or kNullCid.
  static CompileType FromCid(intptr_t cid);

  // Create None CompileType. It is the bottom of the lattice and is used to
  // represent type of the phi that was not yet inferred.
  static CompileType None() {
    return CompileType(kNullable, kIllegalCid, NULL);
  }

  // Create Dynamic CompileType. It is the top of the lattice and is used to
  // represent unknown type.
  static CompileType Dynamic();

  static CompileType Null();

  // Create non-nullable Bool type.
  static CompileType Bool();

  // Create non-nullable Int type.
  static CompileType Int();

  // Create non-nullable String type.
  static CompileType String();

  // Perform a join operation over the type lattice.
  void Union(CompileType* other);

  // Returns true if this and other types are the same.
  bool IsEqualTo(CompileType* other) {
    return (is_nullable_ == other->is_nullable_) &&
        (ToNullableCid() == other->ToNullableCid()) &&
        (ToAbstractType()->Equals(*other->ToAbstractType()));
  }

  bool IsNone() const {
    return (cid_ == kIllegalCid) && (type_ == NULL);
  }

  void PrintTo(BufferFormatter* f) const;
  const char* ToCString() const;

 private:
  bool CanComputeIsInstanceOf(const AbstractType& type,
                              bool is_nullable,
                              bool* is_instance);

  bool is_nullable_;
  intptr_t cid_;
  const AbstractType* type_;
};


// Zone allocated wrapper for the CompileType value.
class ZoneCompileType : public ZoneAllocated {
 public:
  static CompileType* Wrap(const CompileType& type) {
    ZoneCompileType* zone_type = new ZoneCompileType(type);
    return zone_type->ToCompileType();
  }

  CompileType* ToCompileType() {
    return &type_;
  }

 protected:
  explicit ZoneCompileType(const CompileType& type) : type_(type) { }

  CompileType type_;
};


// ConstrainedCompileType represents a compile type that is computed from
// another compile type.
class ConstrainedCompileType : public ZoneCompileType {
 public:
  virtual ~ConstrainedCompileType() { }

  // Recompute compile type.
  virtual void Update() = 0;

 protected:
  explicit ConstrainedCompileType(const CompileType& type)
      : ZoneCompileType(type) { }
};


// NotNullConstrainedCompileType represents not-null constraint applied to
// the source compile type. Result is non-nullable version of the incoming
// compile type. It is used to represent compile type propagated downwards
// from strict comparison with the null constant.
class NotNullConstrainedCompileType : public ConstrainedCompileType {
 public:
  explicit NotNullConstrainedCompileType(CompileType* source)
      : ConstrainedCompileType(source->CopyNonNullable()), source_(source) { }

  virtual void Update() {
    type_ = source_->CopyNonNullable();
  }

 private:
  CompileType* source_;
};


class EffectSet : public ValueObject {
 public:
  enum Effects {
    kNoEffects = 0,
    kExternalization = 1,
    kLastEffect = kExternalization
  };

  EffectSet(const EffectSet& other)
      : ValueObject(), effects_(other.effects_) {
  }

  bool IsNone() const { return effects_ == kNoEffects; }

  static EffectSet None() { return EffectSet(kNoEffects); }
  static EffectSet All() {
    ASSERT(EffectSet::kLastEffect == 1);
    return EffectSet(kExternalization);
  }

  static EffectSet Externalization() {
    return EffectSet(kExternalization);
  }

  bool ToInt() { return effects_; }

 private:
  explicit EffectSet(intptr_t effects) : effects_(effects) { }

  intptr_t effects_;
};


class Value : public ZoneAllocated {
 public:
  // A forward iterator that allows removing the current value from the
  // underlying use list during iteration.
  class Iterator {
   public:
    explicit Iterator(Value* head) : next_(head) { Advance(); }
    Value* Current() const { return current_; }
    bool Done() const { return current_ == NULL; }
    void Advance() {
      // Pre-fetch next on advance and cache it.
      current_ = next_;
      if (next_ != NULL) next_ = next_->next_use();
    }
   private:
    Value* current_;
    Value* next_;
  };

  explicit Value(Definition* definition)
      : definition_(definition),
        previous_use_(NULL),
        next_use_(NULL),
        instruction_(NULL),
        use_index_(-1),
        reaching_type_(NULL) { }

  Definition* definition() const { return definition_; }
  void set_definition(Definition* definition) { definition_ = definition; }

  Value* previous_use() const { return previous_use_; }
  void set_previous_use(Value* previous) { previous_use_ = previous; }

  Value* next_use() const { return next_use_; }
  void set_next_use(Value* next) { next_use_ = next; }

  bool IsSingleUse() const {
    return (next_use_ == NULL) && (previous_use_ == NULL);
  }

  Instruction* instruction() const { return instruction_; }
  void set_instruction(Instruction* instruction) { instruction_ = instruction; }

  intptr_t use_index() const { return use_index_; }
  void set_use_index(intptr_t index) { use_index_ = index; }

  static void AddToList(Value* value, Value** list);
  void RemoveFromUseList();

  // Change the definition after use lists have been computed.
  inline void BindTo(Definition* definition);
  inline void BindToEnvironment(Definition* definition);

  Value* Copy(Isolate* isolate) { return new(isolate) Value(definition_); }

  // This function must only be used when the new Value is dominated by
  // the original Value.
  Value* CopyWithType() {
    Value* copy = new Value(definition_);
    copy->reaching_type_ = reaching_type_;
    return copy;
  }

  CompileType* Type();

  void SetReachingType(CompileType* type) {
    reaching_type_ = type;
  }

  void PrintTo(BufferFormatter* f) const;

  const char* DebugName() const { return "Value"; }

  bool IsSmiValue() { return Type()->ToCid() == kSmiCid; }

  // Return true if the value represents a constant.
  bool BindsToConstant() const;

  // Return true if the value represents the constant null.
  bool BindsToConstantNull() const;

  // Assert if BindsToConstant() is false, otherwise returns the constant value.
  const Object& BoundConstant() const;

  // Compile time constants, Bool, Smi and Nulls do not need to update
  // the store buffer.
  bool NeedsStoreBuffer();

  bool Equals(Value* other) const;

 private:
  friend class FlowGraphPrinter;

  Definition* definition_;
  Value* previous_use_;
  Value* next_use_;
  Instruction* instruction_;
  intptr_t use_index_;

  CompileType* reaching_type_;

  DISALLOW_COPY_AND_ASSIGN(Value);
};


// An embedded container with N elements of type T.  Used (with partial
// specialization for N=0) because embedded arrays cannot have size 0.
template<typename T, intptr_t N>
class EmbeddedArray {
 public:
  EmbeddedArray() : elements_() { }

  intptr_t length() const { return N; }

  const T& operator[](intptr_t i) const {
    ASSERT(i < length());
    return elements_[i];
  }

  T& operator[](intptr_t i) {
    ASSERT(i < length());
    return elements_[i];
  }

  const T& At(intptr_t i) const {
    return (*this)[i];
  }

  void SetAt(intptr_t i, const T& val) {
    (*this)[i] = val;
  }

 private:
  T elements_[N];
};


template<typename T>
class EmbeddedArray<T, 0> {
 public:
  intptr_t length() const { return 0; }
  const T& operator[](intptr_t i) const {
    UNREACHABLE();
    static T sentinel = 0;
    return sentinel;
  }
  T& operator[](intptr_t i) {
    UNREACHABLE();
    static T sentinel = 0;
    return sentinel;
  }
};


// Instructions.

// M is a single argument macro.  It is applied to each concrete instruction
// type name.  The concrete instruction classes are the name with Instr
// concatenated.
#define FOR_EACH_INSTRUCTION(M)                                                \
  M(GraphEntry)                                                                \
  M(JoinEntry)                                                                 \
  M(TargetEntry)                                                               \
  M(CatchBlockEntry)                                                           \
  M(Phi)                                                                       \
  M(Redefinition)                                                              \
  M(Parameter)                                                                 \
  M(ParallelMove)                                                              \
  M(PushArgument)                                                              \
  M(Return)                                                                    \
  M(Throw)                                                                     \
  M(ReThrow)                                                                   \
  M(Goto)                                                                      \
  M(Branch)                                                                    \
  M(AssertAssignable)                                                          \
  M(AssertBoolean)                                                             \
  M(CurrentContext)                                                            \
  M(StoreContext)                                                              \
  M(ClosureCall)                                                               \
  M(InstanceCall)                                                              \
  M(PolymorphicInstanceCall)                                                   \
  M(StaticCall)                                                                \
  M(LoadLocal)                                                                 \
  M(PushTemp)                                                                  \
  M(DropTemps)                                                                 \
  M(StoreLocal)                                                                \
  M(StrictCompare)                                                             \
  M(EqualityCompare)                                                           \
  M(RelationalOp)                                                              \
  M(NativeCall)                                                                \
  M(DebugStepCheck)                                                            \
  M(LoadIndexed)                                                               \
  M(StoreIndexed)                                                              \
  M(StoreInstanceField)                                                        \
  M(LoadStaticField)                                                           \
  M(StoreStaticField)                                                          \
  M(BooleanNegate)                                                             \
  M(InstanceOf)                                                                \
  M(CreateArray)                                                               \
  M(AllocateObject)                                                            \
  M(LoadField)                                                                 \
  M(LoadUntagged)                                                              \
  M(LoadClassId)                                                               \
  M(InstantiateType)                                                           \
  M(InstantiateTypeArguments)                                                  \
  M(AllocateContext)                                                           \
  M(CloneContext)                                                              \
  M(BinarySmiOp)                                                               \
  M(UnarySmiOp)                                                                \
  M(UnaryDoubleOp)                                                             \
  M(CheckStackOverflow)                                                        \
  M(SmiToDouble)                                                               \
  M(DoubleToInteger)                                                           \
  M(DoubleToSmi)                                                               \
  M(DoubleToDouble)                                                            \
  M(DoubleToFloat)                                                             \
  M(FloatToDouble)                                                             \
  M(CheckClass)                                                                \
  M(CheckSmi)                                                                  \
  M(Constant)                                                                  \
  M(UnboxedConstant)                                                           \
  M(CheckEitherNonSmi)                                                         \
  M(BinaryDoubleOp)                                                            \
  M(MathUnary)                                                                 \
  M(MathMinMax)                                                                \
  M(UnboxDouble)                                                               \
  M(BoxDouble)                                                                 \
  M(BoxFloat32x4)                                                              \
  M(UnboxFloat32x4)                                                            \
  M(BoxInt32x4)                                                                \
  M(UnboxInt32x4)                                                              \
  M(UnboxInteger)                                                              \
  M(BoxInteger)                                                                \
  M(BinaryMintOp)                                                              \
  M(ShiftMintOp)                                                               \
  M(UnaryMintOp)                                                               \
  M(CheckArrayBound)                                                           \
  M(Constraint)                                                                \
  M(StringToCharCode)                                                          \
  M(StringFromCharCode)                                                        \
  M(StringInterpolate)                                                         \
  M(InvokeMathCFunction)                                                       \
  M(MergedMath)                                                                \
  M(GuardFieldClass)                                                           \
  M(GuardFieldLength)                                                          \
  M(IfThenElse)                                                                \
  M(BinaryFloat32x4Op)                                                         \
  M(Simd32x4Shuffle)                                                           \
  M(Simd32x4ShuffleMix)                                                        \
  M(Simd32x4GetSignMask)                                                       \
  M(Float32x4Constructor)                                                      \
  M(Float32x4Zero)                                                             \
  M(Float32x4Splat)                                                            \
  M(Float32x4Comparison)                                                       \
  M(Float32x4MinMax)                                                           \
  M(Float32x4Scale)                                                            \
  M(Float32x4Sqrt)                                                             \
  M(Float32x4ZeroArg)                                                          \
  M(Float32x4Clamp)                                                            \
  M(Float32x4With)                                                             \
  M(Float32x4ToInt32x4)                                                        \
  M(MaterializeObject)                                                         \
  M(Int32x4BoolConstructor)                                                    \
  M(Int32x4GetFlag)                                                            \
  M(Int32x4Select)                                                             \
  M(Int32x4SetFlag)                                                            \
  M(Int32x4ToFloat32x4)                                                        \
  M(BinaryInt32x4Op)                                                           \
  M(TestSmi)                                                                   \
  M(TestCids)                                                                  \
  M(BoxFloat64x2)                                                              \
  M(UnboxFloat64x2)                                                            \
  M(BinaryFloat64x2Op)                                                         \
  M(Float64x2Zero)                                                             \
  M(Float64x2Constructor)                                                      \
  M(Float64x2Splat)                                                            \
  M(Float32x4ToFloat64x2)                                                      \
  M(Float64x2ToFloat32x4)                                                      \
  M(Simd64x2Shuffle)                                                           \
  M(Float64x2ZeroArg)                                                          \
  M(Float64x2OneArg)                                                           \
  M(ExtractNthOutput)                                                          \


#define FORWARD_DECLARATION(type) class type##Instr;
FOR_EACH_INSTRUCTION(FORWARD_DECLARATION)
#undef FORWARD_DECLARATION


// Functions required in all concrete instruction classes.
#define DECLARE_INSTRUCTION(type)                                              \
  virtual Tag tag() const { return k##type; }                                  \
  virtual void Accept(FlowGraphVisitor* visitor);                              \
  virtual type##Instr* As##type() { return this; }                             \
  virtual const char* DebugName() const { return #type; }                      \
  virtual LocationSummary* MakeLocationSummary(Isolate* isolate,               \
                                               bool optimizing) const;         \
  virtual void EmitNativeCode(FlowGraphCompiler* compiler);                    \


class Instruction : public ZoneAllocated {
 public:
#define DECLARE_TAG(type) k##type,
  enum Tag {
    FOR_EACH_INSTRUCTION(DECLARE_TAG)
  };
#undef DECLARE_TAG

  Instruction()
      : deopt_id_(Isolate::Current()->GetNextDeoptId()),
        lifetime_position_(-1),
        previous_(NULL),
        next_(NULL),
        env_(NULL),
        locs_(NULL),
        place_id_(kNoPlaceId) { }

  virtual ~Instruction() { }

  virtual Tag tag() const = 0;

  intptr_t deopt_id() const {
    ASSERT(CanDeoptimize() || CanBecomeDeoptimizationTarget());
    return deopt_id_;
  }

  const ICData* GetICData(
      const ZoneGrowableArray<const ICData*>& ic_data_array) const;

  bool IsBlockEntry() { return (AsBlockEntry() != NULL); }
  virtual BlockEntryInstr* AsBlockEntry() { return NULL; }

  bool IsDefinition() { return (AsDefinition() != NULL); }
  virtual Definition* AsDefinition() { return NULL; }

  virtual intptr_t token_pos() const { return Scanner::kNoSourcePos; }

  virtual intptr_t InputCount() const = 0;
  virtual Value* InputAt(intptr_t i) const = 0;
  void SetInputAt(intptr_t i, Value* value) {
    ASSERT(value != NULL);
    value->set_instruction(this);
    value->set_use_index(i);
    RawSetInputAt(i, value);
  }

  // Remove all inputs (including in the environment) from their
  // definition's use lists.
  void UnuseAllInputs();

  // Call instructions override this function and return the number of
  // pushed arguments.
  virtual intptr_t ArgumentCount() const = 0;
  virtual PushArgumentInstr* PushArgumentAt(intptr_t index) const {
    UNREACHABLE();
    return NULL;
  }
  inline Definition* ArgumentAt(intptr_t index) const;

  // Returns true, if this instruction can deoptimize.
  virtual bool CanDeoptimize() const = 0;

  // Visiting support.
  virtual void Accept(FlowGraphVisitor* visitor) = 0;

  Instruction* previous() const { return previous_; }
  void set_previous(Instruction* instr) {
    ASSERT(!IsBlockEntry());
    previous_ = instr;
  }

  Instruction* next() const { return next_; }
  void set_next(Instruction* instr) {
    ASSERT(!IsGraphEntry());
    ASSERT(!IsReturn());
    ASSERT(!IsBranch() || (instr == NULL));
    ASSERT(!IsPhi());
    ASSERT(instr == NULL || !instr->IsBlockEntry());
    // TODO(fschneider): Also add Throw and ReThrow to the list of instructions
    // that do not have a successor. Currently, the graph builder will continue
    // to append instruction in case of a Throw inside an expression. This
    // condition should be handled in the graph builder
    next_ = instr;
  }

  // Link together two instruction.
  void LinkTo(Instruction* next) {
    ASSERT(this != next);
    this->set_next(next);
    next->set_previous(this);
  }

  // Removed this instruction from the graph, after use lists have been
  // computed.  If the instruction is a definition with uses, those uses are
  // unaffected (so the instruction can be reinserted, e.g., hoisting).
  Instruction* RemoveFromGraph(bool return_previous = true);

  // Normal instructions can have 0 (inside a block) or 1 (last instruction in
  // a block) successors. Branch instruction with >1 successors override this
  // function.
  virtual intptr_t SuccessorCount() const;
  virtual BlockEntryInstr* SuccessorAt(intptr_t index) const;

  void Goto(JoinEntryInstr* entry);

  virtual const char* DebugName() const = 0;

  // Printing support.
  const char* ToCString() const;
  virtual void PrintTo(BufferFormatter* f) const;
  virtual void PrintOperandsTo(BufferFormatter* f) const;

#define INSTRUCTION_TYPE_CHECK(type)                                           \
  bool Is##type() { return (As##type() != NULL); }                             \
  virtual type##Instr* As##type() { return NULL; }
FOR_EACH_INSTRUCTION(INSTRUCTION_TYPE_CHECK)
#undef INSTRUCTION_TYPE_CHECK

  // Returns structure describing location constraints required
  // to emit native code for this instruction.
  virtual LocationSummary* locs() {
    ASSERT(locs_ != NULL);
    return locs_;
  }

  virtual LocationSummary* MakeLocationSummary(Isolate* isolate,
                                               bool is_optimizing) const = 0;

  void InitializeLocationSummary(Isolate* isolate, bool optimizing) {
    ASSERT(locs_ == NULL);
    locs_ = MakeLocationSummary(isolate, optimizing);
  }

  static LocationSummary* MakeCallSummary();

  virtual void EmitNativeCode(FlowGraphCompiler* compiler) {
    UNIMPLEMENTED();
  }

  Environment* env() const { return env_; }
  void SetEnvironment(Environment* deopt_env);
  void RemoveEnvironment();

  intptr_t lifetime_position() const { return lifetime_position_; }
  void set_lifetime_position(intptr_t pos) {
    lifetime_position_ = pos;
  }

  // Returns representation expected for the input operand at the given index.
  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    return kTagged;
  }

  // Representation of the value produced by this computation.
  virtual Representation representation() const {
    return kTagged;
  }

  bool WasEliminated() const {
    return next() == NULL;
  }

  // Returns deoptimization id that corresponds to the deoptimization target
  // that input operands conversions inserted for this instruction can jump
  // to.
  virtual intptr_t DeoptimizationTarget() const {
    UNREACHABLE();
    return Isolate::kNoDeoptId;
  }

  // Returns a replacement for the instruction or NULL if the instruction can
  // be eliminated.  By default returns the this instruction which means no
  // change.
  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  // Insert this instruction before 'next' after use lists are computed.
  // Instructions cannot be inserted before a block entry or any other
  // instruction without a previous instruction.
  void InsertBefore(Instruction* next) { InsertAfter(next->previous()); }

  // Insert this instruction after 'prev' after use lists are computed.
  void InsertAfter(Instruction* prev);

  // Append an instruction to the current one and return the tail.
  // This function updated def-use chains of the newly appended
  // instruction.
  Instruction* AppendInstruction(Instruction* tail);

  // Returns true if CSE and LICM are allowed for this instruction.
  virtual bool AllowsCSE() const {
    return false;
  }

  // Returns set of effects created by this instruction.
  virtual EffectSet Effects() const = 0;

  // Returns set of effects that affect this instruction.
  virtual EffectSet Dependencies() const {
    UNREACHABLE();
    return EffectSet::All();
  }

  // Get the block entry for this instruction.
  virtual BlockEntryInstr* GetBlock() const;

  // Place identifiers used by the load optimization pass.
  intptr_t place_id() const { return place_id_; }
  void set_place_id(intptr_t place_id) { place_id_ = place_id; }
  bool HasPlaceId() const { return place_id_ != kNoPlaceId; }

  // Returns a hash code for use with hash maps.
  virtual intptr_t Hashcode() const;

  // Compares two instructions.  Returns true, iff:
  // 1. They have the same tag.
  // 2. All input operands are Equals.
  // 3. They satisfy AttributesEqual.
  bool Equals(Instruction* other) const;

  // Compare attributes of a instructions (except input operands and tag).
  // All instructions that participate in CSE have to override this function.
  // This function can assume that the argument has the same type as this.
  virtual bool AttributesEqual(Instruction* other) const {
    UNREACHABLE();
    return false;
  }

  virtual void InheritDeoptTarget(Isolate* isolate, Instruction* other);

  bool NeedsEnvironment() const {
    return CanDeoptimize() || CanBecomeDeoptimizationTarget();
  }

  virtual bool CanBecomeDeoptimizationTarget() const {
    return false;
  }

  void InheritDeoptTargetAfter(Isolate* isolate, Instruction* other);

  virtual bool MayThrow() const = 0;

 protected:
  // Fetch deopt id without checking if this computation can deoptimize.
  intptr_t GetDeoptId() const {
    return deopt_id_;
  }

 private:
  friend class FlowGraphPrinter;
  friend class Definition;  // Needed for InsertBefore, InsertAfter.
  friend class CallSiteInliner;

  // Classes that set or read deopt_id_.
  friend class UnboxIntegerInstr;
  friend class UnboxDoubleInstr;
  friend class UnboxFloat32x4Instr;
  friend class UnboxFloat64x2Instr;
  friend class UnboxInt32x4Instr;
  friend class BinaryDoubleOpInstr;
  friend class BinaryFloat32x4OpInstr;
  friend class Float32x4ZeroInstr;
  friend class Float32x4SplatInstr;
  friend class Simd32x4ShuffleInstr;
  friend class Simd32x4ShuffleMixInstr;
  friend class Simd32x4GetSignMaskInstr;
  friend class Float32x4ConstructorInstr;
  friend class Float32x4ComparisonInstr;
  friend class Float32x4MinMaxInstr;
  friend class Float32x4ScaleInstr;
  friend class Float32x4SqrtInstr;
  friend class Float32x4ZeroArgInstr;
  friend class Float32x4ClampInstr;
  friend class Float32x4WithInstr;
  friend class Float32x4ToInt32x4Instr;
  friend class Simd64x2ShuffleInstr;
  friend class Float64x2ZeroArgInstr;
  friend class Float64x2OneArgInstr;
  friend class Float32x4ToFloat64x2Instr;
  friend class Float64x2ToFloat32x4Instr;
  friend class Float64x2ZeroInstr;
  friend class Float64x2SplatInstr;
  friend class Float64x2ConstructorInstr;
  friend class Int32x4BoolConstructorInstr;
  friend class Int32x4GetFlagInstr;
  friend class Int32x4SetFlagInstr;
  friend class Int32x4SelectInstr;
  friend class Int32x4ToFloat32x4Instr;
  friend class BinaryInt32x4OpInstr;
  friend class BinaryFloat64x2OpInstr;
  friend class BinaryMintOpInstr;
  friend class BinarySmiOpInstr;
  friend class UnarySmiOpInstr;
  friend class UnaryDoubleOpInstr;
  friend class ShiftMintOpInstr;
  friend class UnaryMintOpInstr;
  friend class MathUnaryInstr;
  friend class MathMinMaxInstr;
  friend class CheckClassInstr;
  friend class GuardFieldInstr;
  friend class CheckSmiInstr;
  friend class CheckArrayBoundInstr;
  friend class CheckEitherNonSmiInstr;
  friend class LICM;
  friend class DoubleToSmiInstr;
  friend class DoubleToDoubleInstr;
  friend class DoubleToFloatInstr;
  friend class FloatToDoubleInstr;
  friend class InvokeMathCFunctionInstr;
  friend class MergedMathInstr;
  friend class FlowGraphOptimizer;
  friend class LoadIndexedInstr;
  friend class StoreIndexedInstr;
  friend class StoreInstanceFieldInstr;
  friend class ComparisonInstr;
  friend class TargetEntryInstr;
  friend class JoinEntryInstr;
  friend class InstanceOfInstr;
  friend class PolymorphicInstanceCallInstr;
  friend class SmiToDoubleInstr;
  friend class DoubleToIntegerInstr;
  friend class BranchSimplifier;
  friend class BlockEntryInstr;
  friend class RelationalOpInstr;
  friend class EqualityCompareInstr;
  friend class TestCidsInstr;

  virtual void RawSetInputAt(intptr_t i, Value* value) = 0;

  enum {
    kNoPlaceId = -1
  };

  intptr_t deopt_id_;
  intptr_t lifetime_position_;  // Position used by register allocator.
  Instruction* previous_;
  Instruction* next_;
  Environment* env_;
  LocationSummary* locs_;
  intptr_t place_id_;

  DISALLOW_COPY_AND_ASSIGN(Instruction);
};


template<intptr_t N>
class TemplateInstruction: public Instruction {
 public:
  TemplateInstruction<N>() : inputs_() { }

  virtual intptr_t InputCount() const { return N; }
  virtual Value* InputAt(intptr_t i) const { return inputs_[i]; }

 protected:
  EmbeddedArray<Value*, N> inputs_;

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    inputs_[i] = value;
  }
};


class MoveOperands : public ZoneAllocated {
 public:
  MoveOperands(Location dest, Location src) : dest_(dest), src_(src) { }

  Location src() const { return src_; }
  Location dest() const { return dest_; }

  Location* src_slot() { return &src_; }
  Location* dest_slot() { return &dest_; }

  void set_src(const Location& value) { src_ = value; }
  void set_dest(const Location& value) { dest_ = value; }

  // The parallel move resolver marks moves as "in-progress" by clearing the
  // destination (but not the source).
  Location MarkPending() {
    ASSERT(!IsPending());
    Location dest = dest_;
    dest_ = Location::NoLocation();
    return dest;
  }

  void ClearPending(Location dest) {
    ASSERT(IsPending());
    dest_ = dest;
  }

  bool IsPending() const {
    ASSERT(!src_.IsInvalid() || dest_.IsInvalid());
    return dest_.IsInvalid() && !src_.IsInvalid();
  }

  // True if this move a move from the given location.
  bool Blocks(Location loc) const {
    return !IsEliminated() && src_.Equals(loc);
  }

  // A move is redundant if it's been eliminated, if its source and
  // destination are the same, or if its destination is unneeded.
  bool IsRedundant() const {
    return IsEliminated() || dest_.IsInvalid() || src_.Equals(dest_);
  }

  // We clear both operands to indicate move that's been eliminated.
  void Eliminate() { src_ = dest_ = Location::NoLocation(); }
  bool IsEliminated() const {
    ASSERT(!src_.IsInvalid() || dest_.IsInvalid());
    return src_.IsInvalid();
  }

 private:
  Location dest_;
  Location src_;

  DISALLOW_COPY_AND_ASSIGN(MoveOperands);
};


class ParallelMoveInstr : public TemplateInstruction<0> {
 public:
  ParallelMoveInstr() : moves_(4) { }

  DECLARE_INSTRUCTION(ParallelMove)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const {
    UNREACHABLE();  // This instruction never visited by optimization passes.
    return EffectSet::None();
  }

  virtual EffectSet Dependencies() const {
    UNREACHABLE();  // This instruction never visited by optimization passes.
    return EffectSet::None();
  }

  MoveOperands* AddMove(Location dest, Location src) {
    MoveOperands* move = new MoveOperands(dest, src);
    moves_.Add(move);
    return move;
  }

  MoveOperands* MoveOperandsAt(intptr_t index) const { return moves_[index]; }

  intptr_t NumMoves() const { return moves_.length(); }

  virtual void PrintTo(BufferFormatter* f) const;

  virtual bool MayThrow() const { return false; }

 private:
  GrowableArray<MoveOperands*> moves_;   // Elements cannot be null.

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveInstr);
};


// Basic block entries are administrative nodes.  There is a distinguished
// graph entry with no predecessor.  Joins are the only nodes with multiple
// predecessors.  Targets are all other basic block entries.  The types
// enforce edge-split form---joins are forbidden as the successors of
// branches.
class BlockEntryInstr : public Instruction {
 public:
  virtual BlockEntryInstr* AsBlockEntry() { return this; }

  virtual intptr_t PredecessorCount() const = 0;
  virtual BlockEntryInstr* PredecessorAt(intptr_t index) const = 0;

  intptr_t preorder_number() const { return preorder_number_; }
  void set_preorder_number(intptr_t number) { preorder_number_ = number; }

  intptr_t postorder_number() const { return postorder_number_; }
  void set_postorder_number(intptr_t number) { postorder_number_ = number; }

  intptr_t block_id() const { return block_id_; }

  void set_start_pos(intptr_t pos) { start_pos_ = pos; }
  intptr_t start_pos() const { return start_pos_; }
  void  set_end_pos(intptr_t pos) { end_pos_ = pos; }
  intptr_t end_pos() const { return end_pos_; }

  BlockEntryInstr* dominator() const { return dominator_; }

  const GrowableArray<BlockEntryInstr*>& dominated_blocks() {
    return dominated_blocks_;
  }

  void AddDominatedBlock(BlockEntryInstr* block) {
    block->set_dominator(this);
    dominated_blocks_.Add(block);
  }
  void ClearDominatedBlocks() { dominated_blocks_.Clear(); }

  bool Dominates(BlockEntryInstr* other) const;

  Instruction* last_instruction() const { return last_instruction_; }
  void set_last_instruction(Instruction* instr) { last_instruction_ = instr; }

  ParallelMoveInstr* parallel_move() const {
    return parallel_move_;
  }

  bool HasParallelMove() const {
    return parallel_move_ != NULL;
  }

  ParallelMoveInstr* GetParallelMove() {
    if (parallel_move_ == NULL) {
      parallel_move_ = new ParallelMoveInstr();
    }
    return parallel_move_;
  }

  // Discover basic-block structure by performing a recursive depth first
  // traversal of the instruction graph reachable from this instruction.  As
  // a side effect, the block entry instructions in the graph are assigned
  // numbers in both preorder and postorder.  The array 'preorder' maps
  // preorder block numbers to the block entry instruction with that number
  // and analogously for the array 'postorder'.  The depth first spanning
  // tree is recorded in the array 'parent', which maps preorder block
  // numbers to the preorder number of the block's spanning-tree parent.
  // The array 'assigned_vars' maps preorder block numbers to the set of
  // assigned frame-allocated local variables in the block.  As a side
  // effect of this function, the set of basic block predecessors (e.g.,
  // block entry instructions of predecessor blocks) and also the last
  // instruction in the block is recorded in each entry instruction.
  void DiscoverBlocks(
      BlockEntryInstr* predecessor,
      GrowableArray<BlockEntryInstr*>* preorder,
      GrowableArray<BlockEntryInstr*>* postorder,
      GrowableArray<intptr_t>* parent,
      intptr_t variable_count,
      intptr_t fixed_parameter_count);

  // Perform a depth first search to prune code not reachable from an OSR
  // entry point.
  bool PruneUnreachable(FlowGraphBuilder* builder,
                        GraphEntryInstr* graph_entry,
                        Instruction* parent,
                        intptr_t osr_id,
                        BitVector* block_marks);

  virtual intptr_t InputCount() const { return 0; }
  virtual Value* InputAt(intptr_t i) const {
    UNREACHABLE();
    return NULL;
  }

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // BlockEntry environment is copied to Goto and Branch instructions
    // when we insert new blocks targeting this block.
    return true;
  }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  intptr_t try_index() const { return try_index_; }

  // True for blocks inside a try { } region.
  bool InsideTryBlock() const {
    return try_index_ != CatchClauseNode::kInvalidTryIndex;
  }

  BitVector* loop_info() const { return loop_info_; }
  void set_loop_info(BitVector* loop_info) {
    loop_info_ = loop_info;
  }

  virtual BlockEntryInstr* GetBlock() const {
    return const_cast<BlockEntryInstr*>(this);
  }

  // Helper to mutate the graph during inlining. This block should be
  // replaced with new_block as a predecessor of all of this block's
  // successors.
  void ReplaceAsPredecessorWith(BlockEntryInstr* new_block);

  void set_block_id(intptr_t block_id) { block_id_ = block_id; }

  // For all instruction in this block: Remove all inputs (including in the
  // environment) from their definition's use lists for all instructions.
  void ClearAllInstructions();

 protected:
  BlockEntryInstr(intptr_t block_id, intptr_t try_index)
      : block_id_(block_id),
        try_index_(try_index),
        preorder_number_(-1),
        postorder_number_(-1),
        dominator_(NULL),
        dominated_blocks_(1),
        last_instruction_(NULL),
        parallel_move_(NULL),
        loop_info_(NULL) { }

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) { UNREACHABLE(); }

  virtual void ClearPredecessors() = 0;
  virtual void AddPredecessor(BlockEntryInstr* predecessor) = 0;

  void set_dominator(BlockEntryInstr* instr) { dominator_ = instr; }

  intptr_t block_id_;
  const intptr_t try_index_;
  intptr_t preorder_number_;
  intptr_t postorder_number_;
  // Starting and ending lifetime positions for this block.  Used by
  // the linear scan register allocator.
  intptr_t start_pos_;
  intptr_t end_pos_;
  BlockEntryInstr* dominator_;  // Immediate dominator, NULL for graph entry.
  // TODO(fschneider): Optimize the case of one child to save space.
  GrowableArray<BlockEntryInstr*> dominated_blocks_;
  Instruction* last_instruction_;

  // Parallel move that will be used by linear scan register allocator to
  // connect live ranges at the start of the block.
  ParallelMoveInstr* parallel_move_;

  // Bit vector containg loop blocks for a loop header indexed by block
  // preorder number.
  BitVector* loop_info_;

  DISALLOW_COPY_AND_ASSIGN(BlockEntryInstr);
};


class ForwardInstructionIterator : public ValueObject {
 public:
  explicit ForwardInstructionIterator(BlockEntryInstr* block_entry)
      : current_(block_entry) {
    Advance();
  }

  void Advance() {
    ASSERT(!Done());
    current_ = current_->next();
  }

  bool Done() const { return current_ == NULL; }

  // Removes 'current_' from graph and sets 'current_' to previous instruction.
  void RemoveCurrentFromGraph();

  Instruction* Current() const { return current_; }

 private:
  Instruction* current_;
};


class BackwardInstructionIterator : public ValueObject {
 public:
  explicit BackwardInstructionIterator(BlockEntryInstr* block_entry)
      : block_entry_(block_entry), current_(block_entry->last_instruction()) {
    ASSERT(block_entry_->previous() == NULL);
  }

  void Advance() {
    ASSERT(!Done());
    current_ = current_->previous();
  }

  bool Done() const { return current_ == block_entry_; }

  void RemoveCurrentFromGraph();

  Instruction* Current() const { return current_; }

 private:
  BlockEntryInstr* block_entry_;
  Instruction* current_;
};


class GraphEntryInstr : public BlockEntryInstr {
 public:
  GraphEntryInstr(const ParsedFunction* parsed_function,
                  TargetEntryInstr* normal_entry,
                  intptr_t osr_id);

  DECLARE_INSTRUCTION(GraphEntry)

  virtual intptr_t PredecessorCount() const { return 0; }
  virtual BlockEntryInstr* PredecessorAt(intptr_t index) const {
    UNREACHABLE();
    return NULL;
  }
  virtual intptr_t SuccessorCount() const;
  virtual BlockEntryInstr* SuccessorAt(intptr_t index) const;

  void AddCatchEntry(CatchBlockEntryInstr* entry) { catch_entries_.Add(entry); }

  CatchBlockEntryInstr* GetCatchEntry(intptr_t index);

  GrowableArray<Definition*>* initial_definitions() {
    return &initial_definitions_;
  }
  ConstantInstr* constant_null();

  bool IsCompiledForOsr() const { return osr_id_ != Isolate::kNoDeoptId; }

  intptr_t entry_count() const { return entry_count_; }
  void set_entry_count(intptr_t count) { entry_count_ = count; }

  intptr_t spill_slot_count() const { return spill_slot_count_; }
  void set_spill_slot_count(intptr_t count) {
    ASSERT(count >= 0);
    spill_slot_count_ = count;
  }

  // Number of stack slots reserved for compiling try-catch. For functions
  // without try-catch, this is 0. Otherwise, it is the number of local
  // variables.
  intptr_t fixed_slot_count() const { return fixed_slot_count_; }
  void set_fixed_slot_count(intptr_t count) {
    ASSERT(count >= 0);
    fixed_slot_count_ = count;
  }
  TargetEntryInstr* normal_entry() const { return normal_entry_; }

  const ParsedFunction& parsed_function() const {
    return *parsed_function_;
  }

  const GrowableArray<CatchBlockEntryInstr*>& catch_entries() const {
    return catch_entries_;
  }

  virtual void PrintTo(BufferFormatter* f) const;

 private:
  virtual void ClearPredecessors() {}
  virtual void AddPredecessor(BlockEntryInstr* predecessor) { UNREACHABLE(); }

  const ParsedFunction* parsed_function_;
  TargetEntryInstr* normal_entry_;
  GrowableArray<CatchBlockEntryInstr*> catch_entries_;
  GrowableArray<Definition*> initial_definitions_;
  const intptr_t osr_id_;
  intptr_t entry_count_;
  intptr_t spill_slot_count_;
  intptr_t fixed_slot_count_;  // For try-catch in optimized code.

  DISALLOW_COPY_AND_ASSIGN(GraphEntryInstr);
};


class JoinEntryInstr : public BlockEntryInstr {
 public:
  JoinEntryInstr(intptr_t block_id, intptr_t try_index)
      : BlockEntryInstr(block_id, try_index),
        predecessors_(2),  // Two is the assumed to be the common case.
        phis_(NULL) { }

  DECLARE_INSTRUCTION(JoinEntry)

  virtual intptr_t PredecessorCount() const { return predecessors_.length(); }
  virtual BlockEntryInstr* PredecessorAt(intptr_t index) const {
    return predecessors_[index];
  }

  // Returns -1 if pred is not in the list.
  intptr_t IndexOfPredecessor(BlockEntryInstr* pred) const;

  ZoneGrowableArray<PhiInstr*>* phis() const { return phis_; }

  void InsertPhi(intptr_t var_index, intptr_t var_count);
  void RemoveDeadPhis(Definition* replacement);

  void InsertPhi(PhiInstr* phi);
  void RemovePhi(PhiInstr* phi);

  virtual void PrintTo(BufferFormatter* f) const;

  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }

 private:
  // Classes that have access to predecessors_ when inlining.
  friend class BlockEntryInstr;
  friend class InlineExitCollector;
  friend class PolymorphicInliner;

  // Direct access to phis_ in order to resize it due to phi elimination.
  friend class ConstantPropagator;
  friend class DeadCodeElimination;

  virtual void ClearPredecessors() { predecessors_.Clear(); }
  virtual void AddPredecessor(BlockEntryInstr* predecessor);

  GrowableArray<BlockEntryInstr*> predecessors_;
  ZoneGrowableArray<PhiInstr*>* phis_;

  DISALLOW_COPY_AND_ASSIGN(JoinEntryInstr);
};


class PhiIterator : public ValueObject {
 public:
  explicit PhiIterator(JoinEntryInstr* join)
      : phis_(join->phis()), index_(0) { }

  void Advance() {
    ASSERT(!Done());
    index_++;
  }

  bool Done() const {
    return (phis_ == NULL) || (index_ >= phis_->length());
  }

  PhiInstr* Current() const {
    return (*phis_)[index_];
  }

 private:
  ZoneGrowableArray<PhiInstr*>* phis_;
  intptr_t index_;
};


class TargetEntryInstr : public BlockEntryInstr {
 public:
  TargetEntryInstr(intptr_t block_id, intptr_t try_index)
      : BlockEntryInstr(block_id, try_index),
        predecessor_(NULL),
        edge_weight_(0.0) {
  }

  DECLARE_INSTRUCTION(TargetEntry)

  double edge_weight() const { return edge_weight_; }
  void set_edge_weight(double weight) { edge_weight_ = weight; }
  void adjust_edge_weight(double scale_factor) { edge_weight_ *= scale_factor; }

  virtual intptr_t PredecessorCount() const {
    return (predecessor_ == NULL) ? 0 : 1;
  }
  virtual BlockEntryInstr* PredecessorAt(intptr_t index) const {
    ASSERT((index == 0) && (predecessor_ != NULL));
    return predecessor_;
  }

  virtual void PrintTo(BufferFormatter* f) const;

 private:
  friend class BlockEntryInstr;  // Access to predecessor_ when inlining.

  virtual void ClearPredecessors() { predecessor_ = NULL; }
  virtual void AddPredecessor(BlockEntryInstr* predecessor) {
    ASSERT(predecessor_ == NULL);
    predecessor_ = predecessor;
  }

  BlockEntryInstr* predecessor_;
  double edge_weight_;

  DISALLOW_COPY_AND_ASSIGN(TargetEntryInstr);
};


class CatchBlockEntryInstr : public BlockEntryInstr {
 public:
  CatchBlockEntryInstr(intptr_t block_id,
                       intptr_t try_index,
                       const Array& handler_types,
                       intptr_t catch_try_index,
                       const LocalVariable& exception_var,
                       const LocalVariable& stacktrace_var,
                       bool needs_stacktrace)
      : BlockEntryInstr(block_id, try_index),
        predecessor_(NULL),
        catch_handler_types_(Array::ZoneHandle(handler_types.raw())),
        catch_try_index_(catch_try_index),
        exception_var_(exception_var),
        stacktrace_var_(stacktrace_var),
        needs_stacktrace_(needs_stacktrace) { }

  DECLARE_INSTRUCTION(CatchBlockEntry)

  virtual intptr_t PredecessorCount() const {
    return (predecessor_ == NULL) ? 0 : 1;
  }
  virtual BlockEntryInstr* PredecessorAt(intptr_t index) const {
    ASSERT((index == 0) && (predecessor_ != NULL));
    return predecessor_;
  }

  const LocalVariable& exception_var() const { return exception_var_; }
  const LocalVariable& stacktrace_var() const { return stacktrace_var_; }

  bool needs_stacktrace() const { return needs_stacktrace_; }

  // Returns try index for the try block to which this catch handler
  // corresponds.
  intptr_t catch_try_index() const {
    return catch_try_index_;
  }
  GrowableArray<Definition*>* initial_definitions() {
    return &initial_definitions_;
  }

  virtual void PrintTo(BufferFormatter* f) const;

 private:
  friend class BlockEntryInstr;  // Access to predecessor_ when inlining.

  virtual void ClearPredecessors() { predecessor_ = NULL; }
  virtual void AddPredecessor(BlockEntryInstr* predecessor) {
    ASSERT(predecessor_ == NULL);
    predecessor_ = predecessor;
  }

  BlockEntryInstr* predecessor_;
  const Array& catch_handler_types_;
  const intptr_t catch_try_index_;
  GrowableArray<Definition*> initial_definitions_;
  const LocalVariable& exception_var_;
  const LocalVariable& stacktrace_var_;
  const bool needs_stacktrace_;

  DISALLOW_COPY_AND_ASSIGN(CatchBlockEntryInstr);
};


// If the result of the allocation is not stored into any field, passed
// as an argument or used in a phi then it can't alias with any other
// SSA value.
enum AliasIdentity {
  kIdentityUnknown,
  kIdentityAliased,
  kIdentityNotAliased
};


// Abstract super-class of all instructions that define a value (Bind, Phi).
class Definition : public Instruction {
 public:
  Definition();

  virtual Definition* AsDefinition() { return this; }

  bool IsComparison() { return (AsComparison() != NULL); }
  virtual ComparisonInstr* AsComparison() { return NULL; }

  // Overridden by definitions that have pushed arguments.
  virtual intptr_t ArgumentCount() const { return 0; }

  // Overridden by definitions that have call counts.
  virtual intptr_t CallCount() const {
    UNREACHABLE();
    return -1;
  }

  intptr_t temp_index() const { return temp_index_; }
  void set_temp_index(intptr_t index) { temp_index_ = index; }
  void ClearTempIndex() { temp_index_ = -1; }
  bool HasTemp() const { return temp_index_ >= 0; }

  intptr_t ssa_temp_index() const { return ssa_temp_index_; }
  void set_ssa_temp_index(intptr_t index) {
    ASSERT(index >= 0);
    ssa_temp_index_ = index;
  }
  bool HasSSATemp() const { return ssa_temp_index_ >= 0; }
  void ClearSSATempIndex() { ssa_temp_index_ = -1; }
  bool HasPairRepresentation() const {
    return (representation() == kPairOfTagged) ||
           (representation() == kPairOfUnboxedDouble) ||
           (representation() == kUnboxedMint);
  }

  // Compile time type of the definition, which may be requested before type
  // propagation during graph building.
  CompileType* Type() {
    if (type_ == NULL) {
      type_ = ComputeInitialType();
    }
    return type_;
  }

  virtual CompileType* ComputeInitialType() const {
    return ZoneCompileType::Wrap(ComputeType());
  }

  // Does this define a mint?
  bool IsMintDefinition() {
    return (Type()->ToCid() == kMintCid) ||
            IsBinaryMintOp() ||
            IsUnaryMintOp() ||
            IsShiftMintOp() ||
            IsUnboxInteger();
  }

  // Compute compile type for this definition. It is safe to use this
  // approximation even before type propagator was run (e.g. during graph
  // building).
  virtual CompileType ComputeType() const {
    return CompileType::Dynamic();
  }

  // Update CompileType of the definition. Returns true if the type has changed.
  virtual bool RecomputeType() {
    return false;
  }

  bool UpdateType(CompileType new_type) {
    if (type_ == NULL) {
      type_ = ZoneCompileType::Wrap(new_type);
      return true;
    }

    if (type_->IsNone() || !type_->IsEqualTo(&new_type)) {
      *type_ = new_type;
      return true;
    }

    return false;
  }

  bool HasUses() const {
    return (input_use_list_ != NULL) || (env_use_list_ != NULL);
  }
  bool HasOnlyUse(Value* use) const;

  Value* input_use_list() const { return input_use_list_; }
  void set_input_use_list(Value* head) { input_use_list_ = head; }
  intptr_t InputUseListLength() const {
    intptr_t length = 0;
    Value* use = input_use_list_;
    while (use != NULL) {
      length++;
      use = use->next_use();
    }
    return length;
  }

  Value* env_use_list() const { return env_use_list_; }
  void set_env_use_list(Value* head) { env_use_list_ = head; }

  void AddInputUse(Value* value) { Value::AddToList(value, &input_use_list_); }
  void AddEnvUse(Value* value) { Value::AddToList(value, &env_use_list_); }

  // Replace uses of this definition with uses of other definition or value.
  // Precondition: use lists must be properly calculated.
  // Postcondition: use lists and use values are still valid.
  void ReplaceUsesWith(Definition* other);

  // Replace this definition and all uses with another definition.  If
  // replacing during iteration, pass the iterator so that the instruction
  // can be replaced without affecting iteration order, otherwise pass a
  // NULL iterator.
  void ReplaceWith(Definition* other, ForwardInstructionIterator* iterator);

  // Printing support. These functions are sometimes overridden for custom
  // formatting. Otherwise, it prints in the format "opcode(op1, op2, op3)".
  virtual void PrintTo(BufferFormatter* f) const;
  virtual void PrintOperandsTo(BufferFormatter* f) const;

  // A value in the constant propagation lattice.
  //    - non-constant sentinel
  //    - a constant (any non-sentinel value)
  //    - unknown sentinel
  Object& constant_value() const { return constant_value_; }

  virtual void InferRange();

  Range* range() const { return range_; }

  // Definitions can be canonicalized only into definitions to ensure
  // this check statically we override base Canonicalize with a Canonicalize
  // returning Definition (return type is covariant).
  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  static const intptr_t kReplacementMarker = -2;

  Definition* Replacement() {
    if (ssa_temp_index_ == kReplacementMarker) {
      return reinterpret_cast<Definition*>(temp_index_);
    }
    return this;
  }

  void SetReplacement(Definition* other) {
    ASSERT(ssa_temp_index_ >= 0);
    ASSERT(WasEliminated());
    ssa_temp_index_ = kReplacementMarker;
    temp_index_ = reinterpret_cast<intptr_t>(other);
  }

  virtual AliasIdentity Identity() const {
    // Only implemented for allocation instructions.
    UNREACHABLE();
    return kIdentityUnknown;
  }

  virtual void SetIdentity(AliasIdentity identity) {
    UNREACHABLE();
  }

  Definition* OriginalDefinition();

 protected:
  friend class RangeAnalysis;
  friend class Value;

  Range* range_;
  CompileType* type_;

 private:
  intptr_t temp_index_;
  intptr_t ssa_temp_index_;
  Value* input_use_list_;
  Value* env_use_list_;

  Object& constant_value_;

  DISALLOW_COPY_AND_ASSIGN(Definition);
};


// Change a value's definition after use lists have been computed.
inline void Value::BindTo(Definition* def) {
  RemoveFromUseList();
  set_definition(def);
  def->AddInputUse(this);
}


inline void Value::BindToEnvironment(Definition* def) {
  RemoveFromUseList();
  set_definition(def);
  def->AddEnvUse(this);
}


template<intptr_t N>
class TemplateDefinition : public Definition {
 public:
  TemplateDefinition<N>() : inputs_() { }

  virtual intptr_t InputCount() const { return N; }
  virtual Value* InputAt(intptr_t i) const { return inputs_[i]; }

 protected:
  EmbeddedArray<Value*, N> inputs_;

 private:
  friend class BranchInstr;
  friend class IfThenElseInstr;

  virtual void RawSetInputAt(intptr_t i, Value* value) {
    inputs_[i] = value;
  }
};


struct BranchLabels {
  Label* true_label;
  Label* false_label;
  Label* fall_through;
};


class PhiInstr : public Definition {
 public:
  PhiInstr(JoinEntryInstr* block, intptr_t num_inputs)
    : block_(block),
      inputs_(num_inputs),
      is_alive_(false),
      representation_(kTagged),
      reaching_defs_(NULL) {
    for (intptr_t i = 0; i < num_inputs; ++i) {
      inputs_.Add(NULL);
    }
  }

  // Get the block entry for that instruction.
  virtual BlockEntryInstr* GetBlock() const { return block(); }
  JoinEntryInstr* block() const { return block_; }

  virtual CompileType ComputeType() const;
  virtual bool RecomputeType();

  virtual intptr_t ArgumentCount() const { return 0; }

  intptr_t InputCount() const { return inputs_.length(); }

  Value* InputAt(intptr_t i) const { return inputs_[i]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  // Phi is alive if it reaches a non-environment use.
  bool is_alive() const { return is_alive_; }
  void mark_alive() { is_alive_ = true; }
  void mark_dead() { is_alive_ = false; }

  virtual Representation RequiredInputRepresentation(intptr_t i) const {
    return representation_;
  }

  virtual Representation representation() const {
    return representation_;
  }

  virtual void set_representation(Representation r) {
    representation_ = r;
  }

  virtual intptr_t Hashcode() const {
    UNREACHABLE();
    return 0;
  }

  DECLARE_INSTRUCTION(Phi)

  virtual void PrintTo(BufferFormatter* f) const;

  virtual void InferRange();

  BitVector* reaching_defs() const {
    return reaching_defs_;
  }

  void set_reaching_defs(BitVector* reaching_defs) {
    reaching_defs_ = reaching_defs;
  }

  virtual bool MayThrow() const { return false; }

  // A phi is redundant if all input operands are the same.
  bool IsRedundant() const;

 private:
  // Direct access to inputs_ in order to resize it due to unreachable
  // predecessors.
  friend class ConstantPropagator;

  void RawSetInputAt(intptr_t i, Value* value) { inputs_[i] = value; }

  JoinEntryInstr* block_;
  GrowableArray<Value*> inputs_;
  bool is_alive_;
  Representation representation_;

  BitVector* reaching_defs_;

  DISALLOW_COPY_AND_ASSIGN(PhiInstr);
};


class ParameterInstr : public Definition {
 public:
  ParameterInstr(intptr_t index, BlockEntryInstr* block)
      : index_(index), block_(block) { }

  DECLARE_INSTRUCTION(Parameter)

  intptr_t index() const { return index_; }

  // Get the block entry for that instruction.
  virtual BlockEntryInstr* GetBlock() const { return block_; }

  virtual intptr_t ArgumentCount() const { return 0; }

  intptr_t InputCount() const { return 0; }
  Value* InputAt(intptr_t i) const {
    UNREACHABLE();
    return NULL;
  }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }

  virtual intptr_t Hashcode() const {
    UNREACHABLE();
    return 0;
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual CompileType ComputeType() const;

  virtual bool MayThrow() const { return false; }

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) { UNREACHABLE(); }

  const intptr_t index_;
  BlockEntryInstr* block_;

  DISALLOW_COPY_AND_ASSIGN(ParameterInstr);
};


class PushArgumentInstr : public TemplateDefinition<1> {
 public:
  explicit PushArgumentInstr(Value* value) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(PushArgument)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual CompileType ComputeType() const;

  Value* value() const { return InputAt(0); }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(PushArgumentInstr);
};


inline Definition* Instruction::ArgumentAt(intptr_t index) const {
  return PushArgumentAt(index)->value()->definition();
}


class ReturnInstr : public TemplateInstruction<1> {
 public:
  ReturnInstr(intptr_t token_pos, Value* value)
      : token_pos_(token_pos) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(Return)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual intptr_t token_pos() const { return token_pos_; }
  Value* value() const { return inputs_[0]; }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // Return instruction might turn into a Goto instruction after inlining.
    // Every Goto must have an environment.
    return true;
  }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(ReturnInstr);
};


class ThrowInstr : public TemplateInstruction<0> {
 public:
  explicit ThrowInstr(intptr_t token_pos) : token_pos_(token_pos) { }

  DECLARE_INSTRUCTION(Throw)

  virtual intptr_t ArgumentCount() const { return 1; }

  virtual intptr_t token_pos() const { return token_pos_; }

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return true; }

 private:
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(ThrowInstr);
};


class ReThrowInstr : public TemplateInstruction<0> {
 public:
  // 'catch_try_index' can be CatchClauseNode::kInvalidTryIndex if the
  // rethrow has been artifically generated by the parser.
  ReThrowInstr(intptr_t token_pos, intptr_t catch_try_index)
      : token_pos_(token_pos), catch_try_index_(catch_try_index) {}

  DECLARE_INSTRUCTION(ReThrow)

  virtual intptr_t ArgumentCount() const { return 2; }

  virtual intptr_t token_pos() const { return token_pos_; }
  intptr_t catch_try_index() const { return catch_try_index_; }

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return true; }

 private:
  const intptr_t token_pos_;
  const intptr_t catch_try_index_;

  DISALLOW_COPY_AND_ASSIGN(ReThrowInstr);
};


class GotoInstr : public TemplateInstruction<0> {
 public:
  explicit GotoInstr(JoinEntryInstr* entry)
    : successor_(entry),
      edge_weight_(0.0),
      parallel_move_(NULL) {
  }

  DECLARE_INSTRUCTION(Goto)

  virtual intptr_t ArgumentCount() const { return 0; }

  JoinEntryInstr* successor() const { return successor_; }
  void set_successor(JoinEntryInstr* successor) { successor_ = successor; }
  virtual intptr_t SuccessorCount() const;
  virtual BlockEntryInstr* SuccessorAt(intptr_t index) const;

  double edge_weight() const { return edge_weight_; }
  void set_edge_weight(double weight) { edge_weight_ = weight; }
  void adjust_edge_weight(double scale_factor) { edge_weight_ *= scale_factor; }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // Goto instruction can be used as a deoptimization target when LICM
    // hoists instructions out of the loop.
    return true;
  }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  ParallelMoveInstr* parallel_move() const {
    return parallel_move_;
  }

  bool HasParallelMove() const {
    return parallel_move_ != NULL;
  }

  ParallelMoveInstr* GetParallelMove() {
    if (parallel_move_ == NULL) {
      parallel_move_ = new ParallelMoveInstr();
    }
    return parallel_move_;
  }

  virtual void PrintTo(BufferFormatter* f) const;

  virtual bool MayThrow() const { return false; }

 private:
  JoinEntryInstr* successor_;
  double edge_weight_;

  // Parallel move that will be used by linear scan register allocator to
  // connect live ranges at the end of the block and resolve phis.
  ParallelMoveInstr* parallel_move_;
};


class ComparisonInstr : public TemplateDefinition<2> {
 public:
  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  virtual ComparisonInstr* AsComparison() { return this; }

  virtual intptr_t token_pos() const { return token_pos_; }
  Token::Kind kind() const { return kind_; }

  virtual ComparisonInstr* CopyWithNewOperands(Value* left, Value* right) = 0;

  virtual void EmitBranchCode(FlowGraphCompiler* compiler,
                              BranchInstr* branch) = 0;

  virtual Condition EmitComparisonCode(FlowGraphCompiler* compiler,
                                       BranchLabels labels) = 0;

  void SetDeoptId(intptr_t deopt_id) {
    deopt_id_ = deopt_id;
  }

  // Operation class id is computed from collected ICData.
  void set_operation_cid(intptr_t value) { operation_cid_ = value; }
  intptr_t operation_cid() const { return operation_cid_; }

  void NegateComparison() {
    kind_ = Token::NegateComparison(kind_);
  }

 protected:
  ComparisonInstr(intptr_t token_pos,
                  Token::Kind kind,
                  Value* left,
                  Value* right)
      : token_pos_(token_pos), kind_(kind), operation_cid_(kIllegalCid) {
    SetInputAt(0, left);
    if (right != NULL) {
      SetInputAt(1, right);
    }
  }

 private:
  const intptr_t token_pos_;
  Token::Kind kind_;
  intptr_t operation_cid_;  // Set by optimizer.

  DISALLOW_COPY_AND_ASSIGN(ComparisonInstr);
};


class BranchInstr : public Instruction {
 public:
  explicit BranchInstr(ComparisonInstr* comparison)
      : comparison_(comparison),
        is_checked_(false),
        constrained_type_(NULL),
        constant_target_(NULL) {
    ASSERT(comparison->env() == NULL);
    for (intptr_t i = comparison->InputCount() - 1; i >= 0; --i) {
      comparison->InputAt(i)->set_instruction(this);
    }
  }

  DECLARE_INSTRUCTION(Branch)

  virtual intptr_t ArgumentCount() const {
    return comparison()->ArgumentCount();
  }

  intptr_t InputCount() const { return comparison()->InputCount(); }

  Value* InputAt(intptr_t i) const { return comparison()->InputAt(i); }

  virtual intptr_t token_pos() const { return comparison_->token_pos(); }

  virtual bool CanDeoptimize() const {
    // Branches need a deoptimization info in checked mode if they
    // can throw a type check error.
    return comparison()->CanDeoptimize() || is_checked();
  }

  virtual bool CanBecomeDeoptimizationTarget() const {
    return comparison()->CanBecomeDeoptimizationTarget();
  }

  virtual EffectSet Effects() const {
    return comparison()->Effects();
  }

  ComparisonInstr* comparison() const { return comparison_; }
  void SetComparison(ComparisonInstr* comp);

  void set_is_checked(bool value) { is_checked_ = value; }
  bool is_checked() const { return is_checked_; }

  virtual intptr_t DeoptimizationTarget() const {
    return comparison()->DeoptimizationTarget();
  }

  virtual Representation RequiredInputRepresentation(intptr_t i) const {
    return comparison()->RequiredInputRepresentation(i);
  }

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  virtual void PrintTo(BufferFormatter* f) const;

  // Set compile type constrained by the comparison of this branch.
  // FlowGraphPropagator propagates it downwards into either true or false
  // successor.
  void set_constrained_type(ConstrainedCompileType* type) {
    constrained_type_ = type;
  }

  // Return compile type constrained by the comparison of this branch.
  ConstrainedCompileType* constrained_type() const {
    return constrained_type_;
  }

  void set_constant_target(TargetEntryInstr* target) {
    ASSERT(target == true_successor() || target == false_successor());
    constant_target_ = target;
  }
  TargetEntryInstr* constant_target() const {
    return constant_target_;
  }

  virtual void InheritDeoptTarget(Isolate* isolate, Instruction* other);

  virtual bool MayThrow() const {
    return comparison()->MayThrow();
  }

  TargetEntryInstr* true_successor() const { return true_successor_; }
  TargetEntryInstr* false_successor() const { return false_successor_; }

  TargetEntryInstr** true_successor_address() { return &true_successor_; }
  TargetEntryInstr** false_successor_address() { return &false_successor_; }

  virtual intptr_t SuccessorCount() const;
  virtual BlockEntryInstr* SuccessorAt(intptr_t index) const;

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    comparison()->RawSetInputAt(i, value);
  }

  TargetEntryInstr* true_successor_;
  TargetEntryInstr* false_successor_;
  ComparisonInstr* comparison_;
  bool is_checked_;
  ConstrainedCompileType* constrained_type_;
  TargetEntryInstr* constant_target_;

  DISALLOW_COPY_AND_ASSIGN(BranchInstr);
};


class StoreContextInstr : public TemplateInstruction<1> {
 public:
  explicit StoreContextInstr(Value* value) {
    SetInputAt(kValuePos, value);
  }

  enum {
    kValuePos = 0
  };

  DECLARE_INSTRUCTION(StoreContext)

  virtual intptr_t ArgumentCount() const { return 0; }

  Value* value() const { return inputs_[kValuePos]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(StoreContextInstr);
};


class RedefinitionInstr : public TemplateDefinition<1> {
 public:
  explicit RedefinitionInstr(Value* value) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(Redefinition)

  Value* value() const { return inputs_[0]; }

  virtual CompileType ComputeType() const;
  virtual bool RecomputeType();

  virtual bool CanDeoptimize() const { return false; }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(RedefinitionInstr);
};


class RangeBoundary : public ValueObject {
 public:
  enum Kind {
    kUnknown,
    kNegativeInfinity,
    kPositiveInfinity,
    kSymbol,
    kConstant,
  };

  enum RangeSize {
    kRangeBoundarySmi,
    kRangeBoundaryInt64,
  };

  RangeBoundary() : kind_(kUnknown), value_(0), offset_(0) { }

  RangeBoundary(const RangeBoundary& other)
      : ValueObject(),
        kind_(other.kind_),
        value_(other.value_),
        offset_(other.offset_) { }

  explicit RangeBoundary(int64_t val)
      : kind_(kConstant), value_(val), offset_(0) { }

  RangeBoundary& operator=(const RangeBoundary& other) {
    kind_ = other.kind_;
    value_ = other.value_;
    offset_ = other.offset_;
    return *this;
  }

  static const int64_t kMin = kMinInt64;
  static const int64_t kMax = kMaxInt64;

  // Construct a RangeBoundary for a constant value.
  static RangeBoundary FromConstant(int64_t val) {
    return RangeBoundary(val);
  }

  // Construct a RangeBoundary for -inf.
  static RangeBoundary NegativeInfinity() {
    return RangeBoundary(kNegativeInfinity, 0, 0);
  }

  // Construct a RangeBoundary for +inf.
  static RangeBoundary PositiveInfinity() {
    return RangeBoundary(kPositiveInfinity, 0, 0);
  }

  // Construct a RangeBoundary from a definition and offset.
  static RangeBoundary FromDefinition(Definition* defn, int64_t offs = 0);

  // Construct a RangeBoundary for the constant MinSmi value.
  static RangeBoundary MinSmi() {
    return FromConstant(Smi::kMinValue);
  }

  // Construct a RangeBoundary for the constant MaxSmi value.
  static RangeBoundary MaxSmi() {
    return FromConstant(Smi::kMaxValue);
  }

    // Construct a RangeBoundary for the constant kMin value.
  static RangeBoundary MinConstant() {
    return FromConstant(kMin);
  }

  // Construct a RangeBoundary for the constant kMax value.
  static RangeBoundary MaxConstant() {
    return FromConstant(kMax);
  }

  // Calculate the minimum of a and b within the given range.
  static RangeBoundary Min(RangeBoundary a, RangeBoundary b, RangeSize size);
  static RangeBoundary Max(RangeBoundary a, RangeBoundary b, RangeSize size);

  // Returns true when this is a constant that is outside of Smi range.
  bool OverflowedSmi() const {
    return (IsConstant() && !Smi::IsValid(ConstantValue())) || IsInfinity();
  }

  // Returns true if this outside mint range.
  bool OverflowedMint() const {
    return IsInfinity();
  }

  // -/+ infinity are clamped to MinConstant/MaxConstant of the given type.
  RangeBoundary Clamp(RangeSize size) const {
    if (IsNegativeInfinity()) {
      return (size == kRangeBoundaryInt64) ? MinConstant() : MinSmi();
    }
    if (IsPositiveInfinity()) {
      return (size == kRangeBoundaryInt64) ? MaxConstant() : MaxSmi();
    }
    if ((size == kRangeBoundarySmi) && IsConstant()) {
      if (ConstantValue() <= Smi::kMinValue) {
        return MinSmi();
      }
      if (ConstantValue() >= Smi::kMaxValue) {
        return MaxSmi();
      }
    }
    // If this range is a symbolic range, we do not clamp it.
    // This could lead to some imprecision later on.
    return *this;
  }


  bool IsSmiMinimumOrBelow() const {
    return IsNegativeInfinity() ||
           (IsConstant() && (ConstantValue() <= Smi::kMinValue));
  }

  bool IsSmiMaximumOrAbove() const {
    return IsPositiveInfinity() ||
           (IsConstant() && (ConstantValue() >= Smi::kMaxValue));
  }

  bool IsMinimumOrBelow() const {
    return IsNegativeInfinity() || (IsConstant() && (ConstantValue() == kMin));
  }

  bool IsMaximumOrAbove() const {
    return IsPositiveInfinity() || (IsConstant() && (ConstantValue() == kMax));
  }

  intptr_t kind() const {
    return kind_;
  }

  // Kind tests.
  bool IsUnknown() const { return kind_ == kUnknown; }
  bool IsConstant() const { return kind_ == kConstant; }
  bool IsSymbol() const { return kind_ == kSymbol; }
  bool IsNegativeInfinity() const { return kind_ == kNegativeInfinity; }
  bool IsPositiveInfinity() const { return kind_ == kPositiveInfinity; }
  bool IsInfinity() const {
    return IsNegativeInfinity() || IsPositiveInfinity();
  }
  bool IsConstantOrInfinity() const {
    return IsConstant() || IsInfinity();
  }

  // Returns the value of a kConstant RangeBoundary.
  int64_t ConstantValue() const;

  // Returns the Definition associated with a kSymbol RangeBoundary.
  Definition* symbol() const {
    ASSERT(IsSymbol());
    return reinterpret_cast<Definition*>(value_);
  }

  // Offset from symbol.
  int64_t offset() const {
    return offset_;
  }

  // Computes the LowerBound of this. Three cases:
  // IsInfinity() -> NegativeInfinity().
  // IsConstant() -> value().
  // IsSymbol() -> lower bound computed from definition + offset.
  RangeBoundary LowerBound() const;

  // Computes the UpperBound of this. Three cases:
  // IsInfinity() -> PositiveInfinity().
  // IsConstant() -> value().
  // IsSymbol() -> upper bound computed from definition + offset.
  RangeBoundary UpperBound() const;

  void PrintTo(BufferFormatter* f) const;
  const char* ToCString() const;

  static RangeBoundary Add(const RangeBoundary& a,
                           const RangeBoundary& b,
                           const RangeBoundary& overflow);

  static RangeBoundary Sub(const RangeBoundary& a,
                           const RangeBoundary& b,
                           const RangeBoundary& overflow);

  static RangeBoundary Shl(const RangeBoundary& value_boundary,
                           int64_t shift_count,
                           const RangeBoundary& overflow);

  // Attempts to calculate a + b when:
  // a is a symbol and b is a constant OR
  // a is a constant and b is a symbol
  // returns true if it succeeds, output is in result.
  static bool SymbolicAdd(const RangeBoundary& a,
                          const RangeBoundary& b,
                          RangeBoundary* result);

  // Attempts to calculate a - b when:
  // a is a symbol and b is a constant
  // returns true if it succeeds, output is in result.
  static bool SymbolicSub(const RangeBoundary& a,
                          const RangeBoundary& b,
                          RangeBoundary* result);

  bool Equals(const RangeBoundary& other) const;

 private:
  RangeBoundary(Kind kind, int64_t value, int64_t offset)
      : kind_(kind), value_(value), offset_(offset) { }

  Kind kind_;
  int64_t value_;
  int64_t offset_;
};


class Range : public ZoneAllocated {
 public:
  Range(RangeBoundary min, RangeBoundary max) : min_(min), max_(max) { }

  static Range* Unknown() {
    return new Range(RangeBoundary::MinConstant(),
                     RangeBoundary::MaxConstant());
  }

  static Range* UnknownSmi() {
    return new Range(RangeBoundary::MinSmi(),
                     RangeBoundary::MaxSmi());
  }

  void PrintTo(BufferFormatter* f) const;
  static const char* ToCString(const Range* range);

  const RangeBoundary& min() const { return min_; }
  const RangeBoundary& max() const { return max_; }

  static RangeBoundary ConstantMinSmi(const Range* range) {
    if (range == NULL) {
      return RangeBoundary::MinSmi();
    }
    return range->min().LowerBound().Clamp(RangeBoundary::kRangeBoundarySmi);
  }

  static RangeBoundary ConstantMaxSmi(const Range* range) {
    if (range == NULL) {
      return RangeBoundary::MaxSmi();
    }
    return range->max().UpperBound().Clamp(RangeBoundary::kRangeBoundarySmi);
  }

  static RangeBoundary ConstantMin(const Range* range) {
    if (range == NULL) {
      return RangeBoundary::MinConstant();
    }
    return range->min().LowerBound().Clamp(RangeBoundary::kRangeBoundaryInt64);
  }

  static RangeBoundary ConstantMax(const Range* range) {
    if (range == NULL) {
      return RangeBoundary::MaxConstant();
    }
    return range->max().UpperBound().Clamp(RangeBoundary::kRangeBoundaryInt64);
  }

  // [0, +inf]
  bool IsPositive() const;

  // [-inf, val].
  bool OnlyLessThanOrEqualTo(int64_t val) const;

  // [val, +inf].
  bool OnlyGreaterThanOrEqualTo(int64_t val) const;

  // Inclusive.
  bool IsWithin(int64_t min_int, int64_t max_int) const;

  // Inclusive.
  bool Overlaps(int64_t min_int, int64_t max_int) const;

  bool IsUnsatisfiable() const;

  bool IsFinite() const {
    return !min_.IsInfinity() && !max_.IsInfinity();
  }

  // Clamp this to be within size.
  void Clamp(RangeBoundary::RangeSize size);

  static void Add(const Range* left_range,
                  const Range* right_range,
                  RangeBoundary* min,
                  RangeBoundary* max,
                  Definition* left_defn);

  static void Sub(const Range* left_range,
                  const Range* right_range,
                  RangeBoundary* min,
                  RangeBoundary* max,
                  Definition* left_defn);

  static bool Mul(const Range* left_range,
                  const Range* right_range,
                  RangeBoundary* min,
                  RangeBoundary* max);

  static void Shl(const Range* left_range,
                  const Range* right_range,
                  RangeBoundary* min,
                  RangeBoundary* max);

  static bool And(const Range* left_range,
                  const Range* right_range,
                  RangeBoundary* min,
                  RangeBoundary* max);


  // Both the a and b ranges are >= 0.
  static bool OnlyPositiveOrZero(const Range& a, const Range& b);

  // Both the a and b ranges are <= 0.
  static bool OnlyNegativeOrZero(const Range& a, const Range& b);

  // Return the maximum absolute value included in range.
  static int64_t ConstantAbsMax(const Range* range);

  static Range* BinaryOp(const Token::Kind op,
                         const Range* left_range,
                         const Range* right_range,
                         Definition* left_defn);

 private:
  RangeBoundary min_;
  RangeBoundary max_;
};


class ConstraintInstr : public TemplateDefinition<2> {
 public:
  ConstraintInstr(Value* value, Range* constraint)
      : constraint_(constraint),
        target_(NULL) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(Constraint)

  virtual intptr_t InputCount() const {
    return (inputs_[1] == NULL) ? 1 : 2;
  }

  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool AttributesEqual(Instruction* other) const {
    UNREACHABLE();
    return false;
  }

  virtual bool MayThrow() const { return false; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  Value* value() const { return inputs_[0]; }
  Range* constraint() const { return constraint_; }

  virtual void InferRange();

  void AddDependency(Definition* defn) {
    Value* val = new Value(defn);
    defn->AddInputUse(val);
    SetInputAt(1, val);
  }

  // Constraints for branches have their target block stored in order
  // to find the the comparsion that generated the constraint:
  // target->predecessor->last_instruction->comparison.
  void set_target(TargetEntryInstr* target) {
    target_ = target;
  }
  TargetEntryInstr* target() const {
    return target_;
  }

 private:
  Value* dependency() {
    return inputs_[1];
  }

  Range* constraint_;
  TargetEntryInstr* target_;

  DISALLOW_COPY_AND_ASSIGN(ConstraintInstr);
};


class ConstantInstr : public TemplateDefinition<0> {
 public:
  explicit ConstantInstr(const Object& value);

  DECLARE_INSTRUCTION(Constant)
  virtual CompileType ComputeType() const;

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  const Object& value() const { return value_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual void InferRange();

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

 private:
  const Object& value_;

  DISALLOW_COPY_AND_ASSIGN(ConstantInstr);
};


// Merged ConstantInstr -> UnboxedXXX into UnboxedConstantInstr.
// TODO(srdjan): Implemented currently for doubles only, should implement
// for other unboxing instructions.
class UnboxedConstantInstr : public ConstantInstr {
 public:
  explicit UnboxedConstantInstr(const Object& value);

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  // Either NULL or the address of the unboxed constant.
  uword constant_address() const { return constant_address_; }

  DECLARE_INSTRUCTION(UnboxedConstant)

 private:
  uword constant_address_;  // Either NULL or points to the untagged constant.

  DISALLOW_COPY_AND_ASSIGN(UnboxedConstantInstr);
};


class AssertAssignableInstr : public TemplateDefinition<3> {
 public:
  AssertAssignableInstr(intptr_t token_pos,
                        Value* value,
                        Value* instantiator,
                        Value* instantiator_type_arguments,
                        const AbstractType& dst_type,
                        const String& dst_name)
      : token_pos_(token_pos),
        dst_type_(AbstractType::ZoneHandle(dst_type.raw())),
        dst_name_(dst_name) {
    ASSERT(!dst_type.IsNull());
    ASSERT(!dst_name.IsNull());
    SetInputAt(0, value);
    SetInputAt(1, instantiator);
    SetInputAt(2, instantiator_type_arguments);
  }

  DECLARE_INSTRUCTION(AssertAssignable)
  virtual CompileType* ComputeInitialType() const;
  virtual bool RecomputeType();

  Value* value() const { return inputs_[0]; }
  Value* instantiator() const { return inputs_[1]; }
  Value* instantiator_type_arguments() const { return inputs_[2]; }

  virtual intptr_t token_pos() const { return token_pos_; }
  const AbstractType& dst_type() const { return dst_type_; }
  void set_dst_type(const AbstractType& dst_type) {
    dst_type_ = dst_type.raw();
  }
  const String& dst_name() const { return dst_name_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return true; }

 private:
  const intptr_t token_pos_;
  AbstractType& dst_type_;
  const String& dst_name_;

  DISALLOW_COPY_AND_ASSIGN(AssertAssignableInstr);
};


class AssertBooleanInstr : public TemplateDefinition<1> {
 public:
  AssertBooleanInstr(intptr_t token_pos, Value* value)
      : token_pos_(token_pos) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(AssertBoolean)
  virtual CompileType ComputeType() const;

  virtual intptr_t token_pos() const { return token_pos_; }
  Value* value() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return true; }

 private:
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(AssertBooleanInstr);
};


// Denotes the current context, normally held in a register.  This is
// a computation, not a value, because it's mutable.
class CurrentContextInstr : public TemplateDefinition<0> {
 public:
  CurrentContextInstr() { }

  DECLARE_INSTRUCTION(CurrentContext)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(CurrentContextInstr);
};


class ClosureCallInstr : public TemplateDefinition<1> {
 public:
  ClosureCallInstr(Value* function,
                   ClosureCallNode* node,
                   ZoneGrowableArray<PushArgumentInstr*>* arguments)
      : ast_node_(*node),
        arguments_(arguments) {
    SetInputAt(0, function);
  }

  DECLARE_INSTRUCTION(ClosureCall)

  const Array& argument_names() const { return ast_node_.arguments()->names(); }
  virtual intptr_t token_pos() const { return ast_node_.token_pos(); }

  virtual intptr_t ArgumentCount() const { return arguments_->length(); }
  virtual PushArgumentInstr* PushArgumentAt(intptr_t index) const {
    return (*arguments_)[index];
  }

  // TODO(kmillikin): implement exact call counts for closure calls.
  virtual intptr_t CallCount() const { return 1; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::All(); }

  virtual bool MayThrow() const { return true; }

 private:
  const ClosureCallNode& ast_node_;
  ZoneGrowableArray<PushArgumentInstr*>* arguments_;

  DISALLOW_COPY_AND_ASSIGN(ClosureCallInstr);
};


class InstanceCallInstr : public TemplateDefinition<0> {
 public:
  InstanceCallInstr(intptr_t token_pos,
                    const String& function_name,
                    Token::Kind token_kind,
                    ZoneGrowableArray<PushArgumentInstr*>* arguments,
                    const Array& argument_names,
                    intptr_t checked_argument_count,
                    const ZoneGrowableArray<const ICData*>& ic_data_array)
      : ic_data_(GetICData(ic_data_array)),
        token_pos_(token_pos),
        function_name_(function_name),
        token_kind_(token_kind),
        arguments_(arguments),
        argument_names_(argument_names),
        checked_argument_count_(checked_argument_count) {
    ASSERT(function_name.IsNotTemporaryScopedHandle());
    ASSERT(!arguments->is_empty());
    ASSERT(argument_names.IsZoneHandle() || argument_names.InVMHeap());
    ASSERT(Token::IsBinaryOperator(token_kind) ||
           Token::IsEqualityOperator(token_kind) ||
           Token::IsRelationalOperator(token_kind) ||
           Token::IsPrefixOperator(token_kind) ||
           Token::IsIndexOperator(token_kind) ||
           Token::IsTypeTestOperator(token_kind) ||
           Token::IsTypeCastOperator(token_kind) ||
           token_kind == Token::kGET ||
           token_kind == Token::kSET ||
           token_kind == Token::kILLEGAL);
  }

  DECLARE_INSTRUCTION(InstanceCall)

  const ICData* ic_data() const { return ic_data_; }
  bool HasICData() const {
    return (ic_data() != NULL) && !ic_data()->IsNull();
  }

  // ICData can be replaced by optimizer.
  void set_ic_data(const ICData* value) { ic_data_ = value; }

  virtual intptr_t token_pos() const { return token_pos_; }
  const String& function_name() const { return function_name_; }
  Token::Kind token_kind() const { return token_kind_; }
  virtual intptr_t ArgumentCount() const { return arguments_->length(); }
  virtual PushArgumentInstr* PushArgumentAt(intptr_t index) const {
    return (*arguments_)[index];
  }
  const Array& argument_names() const { return argument_names_; }
  intptr_t checked_argument_count() const { return checked_argument_count_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::All(); }

  virtual bool MayThrow() const { return true; }

 protected:
  friend class FlowGraphOptimizer;
  void set_ic_data(ICData* value) { ic_data_ = value; }

 private:
  const ICData* ic_data_;
  const intptr_t token_pos_;
  const String& function_name_;
  const Token::Kind token_kind_;  // Binary op, unary op, kGET or kILLEGAL.
  ZoneGrowableArray<PushArgumentInstr*>* const arguments_;
  const Array& argument_names_;
  const intptr_t checked_argument_count_;

  DISALLOW_COPY_AND_ASSIGN(InstanceCallInstr);
};


class PolymorphicInstanceCallInstr : public TemplateDefinition<0> {
 public:
  PolymorphicInstanceCallInstr(InstanceCallInstr* instance_call,
                               const ICData& ic_data,
                               bool with_checks)
      : instance_call_(instance_call),
        ic_data_(ic_data),
        with_checks_(with_checks) {
    ASSERT(instance_call_ != NULL);
    deopt_id_ = instance_call->deopt_id();
  }

  InstanceCallInstr* instance_call() const { return instance_call_; }
  bool with_checks() const { return with_checks_; }
  virtual intptr_t token_pos() const { return instance_call_->token_pos(); }

  virtual intptr_t ArgumentCount() const {
    return instance_call()->ArgumentCount();
  }
  virtual PushArgumentInstr* PushArgumentAt(intptr_t index) const {
    return instance_call()->PushArgumentAt(index);
  }

  bool HasSingleRecognizedTarget() const;

  bool HasOnlyDispatcherTargets() const;

  virtual intptr_t CallCount() const { return ic_data().AggregateCount(); }

  DECLARE_INSTRUCTION(PolymorphicInstanceCall)

  const ICData& ic_data() const { return ic_data_; }

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::All(); }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool MayThrow() const { return true; }

 private:
  InstanceCallInstr* instance_call_;
  const ICData& ic_data_;
  const bool with_checks_;

  DISALLOW_COPY_AND_ASSIGN(PolymorphicInstanceCallInstr);
};


class StrictCompareInstr : public ComparisonInstr {
 public:
  StrictCompareInstr(intptr_t token_pos,
                     Token::Kind kind,
                     Value* left,
                     Value* right,
                     bool needs_number_check);

  DECLARE_INSTRUCTION(StrictCompare)

  virtual ComparisonInstr* CopyWithNewOperands(Value* left, Value* right);

  virtual CompileType ComputeType() const;

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanBecomeDeoptimizationTarget() const {
    // StrictCompare can be merged into Branch and thus needs an environment.
    return true;
  }

  virtual bool CanDeoptimize() const { return false; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  virtual void EmitBranchCode(FlowGraphCompiler* compiler,
                              BranchInstr* branch);

  virtual Condition EmitComparisonCode(FlowGraphCompiler* compiler,
                                       BranchLabels labels);

  bool needs_number_check() const { return needs_number_check_; }
  void set_needs_number_check(bool value) { needs_number_check_ = value; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

 private:
  // True if the comparison must check for double, Mint or Bigint and
  // use value comparison instead.
  bool needs_number_check_;

  DISALLOW_COPY_AND_ASSIGN(StrictCompareInstr);
};


// Comparison instruction that is equivalent to the (left & right) == 0
// comparison pattern.
class TestSmiInstr : public ComparisonInstr {
 public:
  TestSmiInstr(intptr_t token_pos, Token::Kind kind, Value* left, Value* right)
      : ComparisonInstr(token_pos, kind, left, right) {
    ASSERT(kind == Token::kEQ || kind == Token::kNE);
  }

  DECLARE_INSTRUCTION(TestSmi);

  virtual ComparisonInstr* CopyWithNewOperands(Value* left, Value* right);

  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // TestSmi can be merged into Branch and thus needs an environment.
    return true;
  }

  virtual intptr_t DeoptimizationTarget() const {
    return GetDeoptId();
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    return kTagged;
  }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  virtual void EmitBranchCode(FlowGraphCompiler* compiler,
                              BranchInstr* branch);

  virtual Condition EmitComparisonCode(FlowGraphCompiler* compiler,
                                       BranchLabels labels);

 private:
  DISALLOW_COPY_AND_ASSIGN(TestSmiInstr);
};


// Checks the input value cid against cids stored in a table and returns either
// a result or deoptimizes.
// TODO(srdjan): Modify ComparisonInstr to allow 1 or 2 arguments, since
// TestCidInstr needs only one argument
class TestCidsInstr : public ComparisonInstr {
 public:
  TestCidsInstr(intptr_t token_pos,
                Token::Kind kind,
                Value* value,
                const ZoneGrowableArray<intptr_t>& cid_results,
                intptr_t deopt_id)
      : ComparisonInstr(token_pos, kind, value, NULL),
        cid_results_(cid_results) {
    ASSERT((kind == Token::kIS) || (kind == Token::kISNOT));
    set_operation_cid(kObjectCid);
    deopt_id_ = deopt_id;
  }

  virtual intptr_t InputCount() const { return 1; }

  const ZoneGrowableArray<intptr_t>& cid_results() const {
    return cid_results_;
  }

  DECLARE_INSTRUCTION(TestCids);

  virtual ComparisonInstr* CopyWithNewOperands(Value* left, Value* right);

  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const {
    return deopt_id_ != Isolate::kNoDeoptId;
  }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // TestCid can be merged into Branch and thus needs an environment.
    return true;
  }

  virtual intptr_t DeoptimizationTarget() const {
    return GetDeoptId();
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    return kTagged;
  }

  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  virtual bool AttributesEqual(Instruction* other) const;

  virtual void EmitBranchCode(FlowGraphCompiler* compiler,
                              BranchInstr* branch);

  virtual Condition EmitComparisonCode(FlowGraphCompiler* compiler,
                                       BranchLabels labels);

  virtual void PrintOperandsTo(BufferFormatter* f) const;

 private:
  const ZoneGrowableArray<intptr_t>& cid_results_;
  DISALLOW_COPY_AND_ASSIGN(TestCidsInstr);
};


class EqualityCompareInstr : public ComparisonInstr {
 public:
  EqualityCompareInstr(intptr_t token_pos,
                       Token::Kind kind,
                       Value* left,
                       Value* right,
                       intptr_t cid,
                       intptr_t deopt_id)
      : ComparisonInstr(token_pos, kind, left, right) {
    ASSERT(Token::IsEqualityOperator(kind));
    set_operation_cid(cid);
    deopt_id_ = deopt_id;  // Override generated deopt-id.
  }

  DECLARE_INSTRUCTION(EqualityCompare)

  virtual ComparisonInstr* CopyWithNewOperands(Value* left, Value* right);

  virtual CompileType ComputeType() const;

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // EqualityCompare can be merged into Branch and thus needs an environment.
    return true;
  }

  virtual void EmitBranchCode(FlowGraphCompiler* compiler,
                              BranchInstr* branch);

  virtual Condition EmitComparisonCode(FlowGraphCompiler* compiler,
                                       BranchLabels labels);

  virtual intptr_t DeoptimizationTarget() const {
    return GetDeoptId();
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    if (operation_cid() == kDoubleCid) return kUnboxedDouble;
    if (operation_cid() == kMintCid) return kUnboxedMint;
    return kTagged;
  }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(EqualityCompareInstr);
};


class RelationalOpInstr : public ComparisonInstr {
 public:
  RelationalOpInstr(intptr_t token_pos,
                    Token::Kind kind,
                    Value* left,
                    Value* right,
                    intptr_t cid,
                    intptr_t deopt_id)
      : ComparisonInstr(token_pos, kind, left, right) {
    ASSERT(Token::IsRelationalOperator(kind));
    set_operation_cid(cid);
    deopt_id_ = deopt_id;  // Override generated deopt-id.
  }

  DECLARE_INSTRUCTION(RelationalOp)

  virtual ComparisonInstr* CopyWithNewOperands(Value* left, Value* right);

  virtual CompileType ComputeType() const;

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual bool CanBecomeDeoptimizationTarget() const {
    // RelationalOp can be merged into Branch and thus needs an environment.
    return true;
  }

  virtual void EmitBranchCode(FlowGraphCompiler* compiler,
                              BranchInstr* branch);

  virtual Condition EmitComparisonCode(FlowGraphCompiler* compiler,
                                       BranchLabels labels);

  virtual intptr_t DeoptimizationTarget() const {
    return GetDeoptId();
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    if (operation_cid() == kDoubleCid) return kUnboxedDouble;
    if (operation_cid() == kMintCid) return kUnboxedMint;
    return kTagged;
  }

  virtual EffectSet Effects() const {
    return EffectSet::None();
  }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(RelationalOpInstr);
};


// TODO(vegorov): ComparisonInstr should be switched to use IfTheElseInstr for
// materialization of true and false constants.
class IfThenElseInstr : public Definition {
 public:
  IfThenElseInstr(ComparisonInstr* comparison,
                  Value* if_true,
                  Value* if_false)
      : comparison_(comparison),
        if_true_(Smi::Cast(if_true->BoundConstant()).Value()),
        if_false_(Smi::Cast(if_false->BoundConstant()).Value()) {
    // Adjust uses at the comparison.
    ASSERT(comparison->env() == NULL);
    for (intptr_t i = comparison->InputCount() - 1; i >= 0; --i) {
      comparison->InputAt(i)->set_instruction(this);
    }
  }

  // Returns true if this combination of comparison and values flowing on
  // the true and false paths is supported on the current platform.
  static bool Supports(ComparisonInstr* comparison, Value* v1, Value* v2);

  DECLARE_INSTRUCTION(IfThenElse)

  intptr_t InputCount() const { return comparison()->InputCount(); }

  Value* InputAt(intptr_t i) const { return comparison()->InputAt(i); }

  virtual bool CanDeoptimize() const {
    return comparison()->CanDeoptimize();
  }

  virtual bool CanBecomeDeoptimizationTarget() const {
    return comparison()->CanBecomeDeoptimizationTarget();
  }

  virtual intptr_t DeoptimizationTarget() const {
    return comparison()->DeoptimizationTarget();
  }

  virtual Representation RequiredInputRepresentation(intptr_t i) const {
    return comparison()->RequiredInputRepresentation(i);
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual CompileType ComputeType() const;

  virtual void InferRange();

  ComparisonInstr* comparison() const { return comparison_; }
  intptr_t if_true() const { return if_true_; }
  intptr_t if_false() const { return if_false_; }

  virtual bool AllowsCSE() const { return comparison()->AllowsCSE(); }
  virtual EffectSet Effects() const { return comparison()->Effects(); }
  virtual EffectSet Dependencies() const {
    return comparison()->Dependencies();
  }
  virtual bool AttributesEqual(Instruction* other) const {
    IfThenElseInstr* other_if_then_else = other->AsIfThenElse();
    return comparison()->AttributesEqual(other_if_then_else->comparison()) &&
           (if_true_ == other_if_then_else->if_true_) &&
           (if_false_ == other_if_then_else->if_false_);
  }

  virtual bool MayThrow() const { return comparison()->MayThrow(); }

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    comparison()->RawSetInputAt(i, value);
  }

  ComparisonInstr* comparison_;
  const intptr_t if_true_;
  const intptr_t if_false_;

  DISALLOW_COPY_AND_ASSIGN(IfThenElseInstr);
};


class StaticCallInstr : public TemplateDefinition<0> {
 public:
  StaticCallInstr(intptr_t token_pos,
                  const Function& function,
                  const Array& argument_names,
                  ZoneGrowableArray<PushArgumentInstr*>* arguments,
                  const ZoneGrowableArray<const ICData*>& ic_data_array)
      : ic_data_(GetICData(ic_data_array)),
        token_pos_(token_pos),
        function_(function),
        argument_names_(argument_names),
        arguments_(arguments),
        result_cid_(kDynamicCid),
        is_known_list_constructor_(false),
        is_native_list_factory_(false),
        identity_(kIdentityUnknown) {
    ASSERT(function.IsZoneHandle());
    ASSERT(argument_names.IsZoneHandle() ||  argument_names.InVMHeap());
  }

  // ICData for static calls carries call count.
  const ICData* ic_data() const { return ic_data_; }
  bool HasICData() const {
    return (ic_data() != NULL) && !ic_data()->IsNull();
  }

  DECLARE_INSTRUCTION(StaticCall)
  virtual CompileType ComputeType() const;

  // Accessors forwarded to the AST node.
  const Function& function() const { return function_; }
  const Array& argument_names() const { return argument_names_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  virtual intptr_t ArgumentCount() const { return arguments_->length(); }
  virtual PushArgumentInstr* PushArgumentAt(intptr_t index) const {
    return (*arguments_)[index];
  }

  virtual intptr_t CallCount() const { return ic_data()->AggregateCount(); }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::All(); }

  void set_result_cid(intptr_t value) { result_cid_ = value; }

  bool is_known_list_constructor() const { return is_known_list_constructor_; }
  void set_is_known_list_constructor(bool value) {
    is_known_list_constructor_ = value;
  }

  bool is_native_list_factory() const { return is_native_list_factory_; }
  void set_is_native_list_factory(bool value) {
    is_native_list_factory_ = value;
  }

  bool IsRecognizedFactory() const {
    return is_known_list_constructor() || is_native_list_factory();
  }

  virtual bool MayThrow() const { return true; }

  virtual AliasIdentity Identity() const { return identity_; }
  virtual void SetIdentity(AliasIdentity identity) { identity_ = identity; }

 private:
  const ICData* ic_data_;
  const intptr_t token_pos_;
  const Function& function_;
  const Array& argument_names_;
  ZoneGrowableArray<PushArgumentInstr*>* arguments_;
  intptr_t result_cid_;  // For some library functions we know the result.

  // 'True' for recognized list constructors.
  bool is_known_list_constructor_;
  bool is_native_list_factory_;

  AliasIdentity identity_;

  DISALLOW_COPY_AND_ASSIGN(StaticCallInstr);
};


class LoadLocalInstr : public TemplateDefinition<0> {
 public:
  explicit LoadLocalInstr(const LocalVariable& local)
      : local_(local), is_last_(false) { }

  DECLARE_INSTRUCTION(LoadLocal)
  virtual CompileType ComputeType() const;

  const LocalVariable& local() const { return local_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const {
    UNREACHABLE();  // Eliminated by SSA construction.
    return EffectSet::None();
  }

  void mark_last() { is_last_ = true; }
  bool is_last() const { return is_last_; }

  virtual bool MayThrow() const {
    UNREACHABLE();
    return false;
  }

 private:
  const LocalVariable& local_;
  bool is_last_;

  DISALLOW_COPY_AND_ASSIGN(LoadLocalInstr);
};


class PushTempInstr : public TemplateDefinition<1> {
 public:
  explicit PushTempInstr(Value* value) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(PushTemp)

  Value* value() const { return inputs_[0]; }

  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const {
    UNREACHABLE();  // Eliminated by SSA construction.
    return EffectSet::None();
  }

  virtual bool MayThrow() const {
    UNREACHABLE();
    return false;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PushTempInstr);
};


class DropTempsInstr : public Definition {
 public:
  explicit DropTempsInstr(intptr_t num_temps, Value* value = NULL)
      : num_temps_(num_temps), value_(NULL) {
    if (value != NULL) {
      SetInputAt(0, value);
    }
  }

  DECLARE_INSTRUCTION(DropTemps)

  virtual intptr_t InputCount() const { return value_ != NULL ? 1 : 0; }
  virtual Value* InputAt(intptr_t i) const {
    ASSERT((value_ != NULL) && (i == 0));
    return value_;
  }

  Value* value() const { return value_; }

  intptr_t num_temps() const { return num_temps_; }

  virtual CompileType* ComputeInitialType() const;

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const {
    UNREACHABLE();  // Eliminated by SSA construction.
    return EffectSet::None();
  }

  virtual bool MayThrow() const {
    UNREACHABLE();
    return false;
  }

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    value_ = value;
  }

  const intptr_t num_temps_;
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(DropTempsInstr);
};


class StoreLocalInstr : public TemplateDefinition<1> {
 public:
  StoreLocalInstr(const LocalVariable& local, Value* value)
      : local_(local), is_dead_(false), is_last_(false) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(StoreLocal)
  virtual CompileType* ComputeInitialType() const;

  const LocalVariable& local() const { return local_; }
  Value* value() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  void mark_dead() { is_dead_ = true; }
  bool is_dead() const { return is_dead_; }

  void mark_last() { is_last_ = true; }
  bool is_last() const { return is_last_; }

  virtual EffectSet Effects() const {
    UNREACHABLE();  // Eliminated by SSA construction.
    return EffectSet::None();
  }

  virtual bool MayThrow() const {
    UNREACHABLE();
    return false;
  }

 private:
  const LocalVariable& local_;
  bool is_dead_;
  bool is_last_;

  DISALLOW_COPY_AND_ASSIGN(StoreLocalInstr);
};


class NativeCallInstr : public TemplateDefinition<0> {
 public:
  explicit NativeCallInstr(NativeBodyNode* node)
      : ast_node_(*node) {}

  DECLARE_INSTRUCTION(NativeCall)

  virtual intptr_t token_pos() const { return ast_node_.token_pos(); }

  const Function& function() const { return ast_node_.function(); }

  const String& native_name() const {
    return ast_node_.native_c_function_name();
  }

  NativeFunction native_c_function() const {
    return ast_node_.native_c_function();
  }

  bool is_bootstrap_native() const {
    return ast_node_.is_bootstrap_native();
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::All(); }

  virtual bool MayThrow() const {
    UNREACHABLE();
    return true;
  }

 private:
  const NativeBodyNode& ast_node_;

  DISALLOW_COPY_AND_ASSIGN(NativeCallInstr);
};


class DebugStepCheckInstr : public TemplateInstruction<0> {
 public:
  DebugStepCheckInstr(intptr_t token_pos,
                      PcDescriptors::Kind stub_kind)
      : token_pos_(token_pos),
        stub_kind_(stub_kind) {
  }

  DECLARE_INSTRUCTION(DebugStepCheck)

  virtual intptr_t token_pos() const { return token_pos_; }
  virtual bool MayThrow() const { return false; }
  virtual bool CanDeoptimize() const { return false; }
  virtual EffectSet Effects() const { return EffectSet::All(); }
  virtual intptr_t ArgumentCount() const { return 0; }
  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

 private:
  const intptr_t token_pos_;
  const PcDescriptors::Kind stub_kind_;

  DISALLOW_COPY_AND_ASSIGN(DebugStepCheckInstr);
};


enum StoreBarrierType {
  kNoStoreBarrier,
  kEmitStoreBarrier
};


class StoreInstanceFieldInstr : public TemplateDefinition<2> {
 public:
  StoreInstanceFieldInstr(const Field& field,
                          Value* instance,
                          Value* value,
                          StoreBarrierType emit_store_barrier,
                          intptr_t token_pos)
      : field_(field),
        offset_in_bytes_(field.Offset()),
        emit_store_barrier_(emit_store_barrier),
        token_pos_(token_pos),
        is_initialization_(false) {
    SetInputAt(kInstancePos, instance);
    SetInputAt(kValuePos, value);
  }

  StoreInstanceFieldInstr(intptr_t offset_in_bytes,
                          Value* instance,
                          Value* value,
                          StoreBarrierType emit_store_barrier,
                          intptr_t token_pos)
      : field_(Field::Handle()),
        offset_in_bytes_(offset_in_bytes),
        emit_store_barrier_(emit_store_barrier),
        token_pos_(token_pos),
        is_initialization_(false) {
    SetInputAt(kInstancePos, instance);
    SetInputAt(kValuePos, value);
  }

  DECLARE_INSTRUCTION(StoreInstanceField)

  void set_is_initialization(bool value) { is_initialization_ = value; }

  enum {
    kInstancePos = 0,
    kValuePos = 1
  };

  Value* instance() const { return inputs_[kInstancePos]; }
  Value* value() const { return inputs_[kValuePos]; }
  bool is_initialization() const { return is_initialization_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  virtual CompileType* ComputeInitialType() const;

  const Field& field() const { return field_; }
  intptr_t offset_in_bytes() const { return offset_in_bytes_; }

  bool ShouldEmitStoreBarrier() const {
    return value()->NeedsStoreBuffer()
        && (emit_store_barrier_ == kEmitStoreBarrier);
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  // May require a deoptimization target for input conversions.
  virtual intptr_t DeoptimizationTarget() const {
    return GetDeoptId();
  }

  // Currently CSE/LICM don't operate on any instructions that can be affected
  // by stores/loads. LoadOptimizer handles loads separately. Hence stores
  // are marked as having no side-effects.
  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  bool IsUnboxedStore() const;

  bool IsPotentialUnboxedStore() const;

  virtual Representation RequiredInputRepresentation(intptr_t index) const;

 private:
  friend class FlowGraphOptimizer;  // For ASSERT(initialization_).

  bool CanValueBeSmi() const {
    const intptr_t cid = value()->Type()->ToNullableCid();
    // Write barrier is skipped for nullable and non-nullable smis.
    ASSERT(cid != kSmiCid);
    return (cid == kDynamicCid);
  }

  const Field& field_;
  intptr_t offset_in_bytes_;
  const StoreBarrierType emit_store_barrier_;
  const intptr_t token_pos_;
  bool is_initialization_;  // Marks stores in the constructor.

  DISALLOW_COPY_AND_ASSIGN(StoreInstanceFieldInstr);
};


class GuardFieldInstr : public TemplateInstruction<1> {
 public:
  GuardFieldInstr(Value* value,
                  const Field& field,
                  intptr_t deopt_id)
    : field_(field) {
    deopt_id_ = deopt_id;
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  const Field& field() const { return field_; }

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return true; }
  virtual bool CanBecomeDeoptimizationTarget() const {
    // Ensure that we record kDeopt PC descriptor in unoptimized code.
    return true;
  }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

 private:
  const Field& field_;

  DISALLOW_COPY_AND_ASSIGN(GuardFieldInstr);
};


class GuardFieldClassInstr : public GuardFieldInstr {
 public:
  GuardFieldClassInstr(Value* value,
                       const Field& field,
                       intptr_t deopt_id)
    : GuardFieldInstr(value, field, deopt_id) { }

  DECLARE_INSTRUCTION(GuardFieldClass)

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  virtual bool AttributesEqual(Instruction* other) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(GuardFieldClassInstr);
};


class GuardFieldLengthInstr : public GuardFieldInstr {
 public:
  GuardFieldLengthInstr(Value* value,
                       const Field& field,
                       intptr_t deopt_id)
    : GuardFieldInstr(value, field, deopt_id) { }

  DECLARE_INSTRUCTION(GuardFieldLength)

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  virtual bool AttributesEqual(Instruction* other) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(GuardFieldLengthInstr);
};


class LoadStaticFieldInstr : public TemplateDefinition<1> {
 public:
  explicit LoadStaticFieldInstr(Value* field_value) {
    ASSERT(field_value->BindsToConstant());
    SetInputAt(0, field_value);
  }

  DECLARE_INSTRUCTION(LoadStaticField)
  virtual CompileType ComputeType() const;

  const Field& StaticField() const;

  Value* field_value() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual bool AllowsCSE() const { return StaticField().is_final(); }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const;
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(LoadStaticFieldInstr);
};


class StoreStaticFieldInstr : public TemplateDefinition<1> {
 public:
  StoreStaticFieldInstr(const Field& field, Value* value)
      : field_(field) {
    ASSERT(field.IsZoneHandle());
    SetInputAt(kValuePos, value);
  }

  enum {
    kValuePos = 0
  };

  DECLARE_INSTRUCTION(StoreStaticField)
  virtual CompileType* ComputeInitialType() const;

  const Field& field() const { return field_; }
  Value* value() const { return inputs_[kValuePos]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  // Currently CSE/LICM don't operate on any instructions that can be affected
  // by stores/loads. LoadOptimizer handles loads separately. Hence stores
  // are marked as having no side-effects.
  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  bool CanValueBeSmi() const {
    const intptr_t cid = value()->Type()->ToNullableCid();
    // Write barrier is skipped for nullable and non-nullable smis.
    ASSERT(cid != kSmiCid);
    return (cid == kDynamicCid);
  }

  const Field& field_;

  DISALLOW_COPY_AND_ASSIGN(StoreStaticFieldInstr);
};


class LoadIndexedInstr : public TemplateDefinition<2> {
 public:
  LoadIndexedInstr(Value* array,
                   Value* index,
                   intptr_t index_scale,
                   intptr_t class_id,
                   intptr_t deopt_id,
                   intptr_t token_pos)
      : index_scale_(index_scale),
        class_id_(class_id),
        token_pos_(token_pos) {
    SetInputAt(0, array);
    SetInputAt(1, index);
    deopt_id_ = deopt_id;
  }

  intptr_t token_pos() const { return token_pos_; }

  DECLARE_INSTRUCTION(LoadIndexed)
  virtual CompileType ComputeType() const;

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0 || idx == 1);
    // The array may be tagged or untagged (for external arrays).
    if (idx == 0) return kNoRepresentation;
    return kTagged;
  }

  bool IsExternal() const {
    return array()->definition()->representation() == kUntagged;
  }

  Value* array() const { return inputs_[0]; }
  Value* index() const { return inputs_[1]; }
  intptr_t index_scale() const { return index_scale_; }
  intptr_t class_id() const { return class_id_; }

  virtual bool CanDeoptimize() const {
    return deopt_id_ != Isolate::kNoDeoptId;
  }


  virtual Representation representation() const;
  virtual void InferRange();

  virtual bool AllowsCSE() const { return false; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const;
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t index_scale_;
  const intptr_t class_id_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(LoadIndexedInstr);
};


class StringFromCharCodeInstr : public TemplateDefinition<1> {
 public:
  StringFromCharCodeInstr(Value* char_code, intptr_t cid) : cid_(cid) {
    ASSERT(char_code != NULL);
    ASSERT(char_code->definition()->IsLoadIndexed());
    ASSERT(char_code->definition()->AsLoadIndexed()->class_id() ==
           kOneByteStringCid);
    SetInputAt(0, char_code);
  }

  DECLARE_INSTRUCTION(StringFromCharCode)
  virtual CompileType ComputeType() const;

  Value* char_code() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return other->AsStringFromCharCode()->cid_ == cid_;
  }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t cid_;

  DISALLOW_COPY_AND_ASSIGN(StringFromCharCodeInstr);
};


class StringToCharCodeInstr : public TemplateDefinition<1> {
 public:
  StringToCharCodeInstr(Value* str, intptr_t cid) : cid_(cid) {
    ASSERT(str != NULL);
    SetInputAt(0, str);
  }

  DECLARE_INSTRUCTION(StringToCharCode)
  virtual CompileType ComputeType() const;

  Value* str() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return other->AsStringToCharCode()->cid_ == cid_;
  }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t cid_;

  DISALLOW_COPY_AND_ASSIGN(StringToCharCodeInstr);
};


class StringInterpolateInstr : public TemplateDefinition<1> {
 public:
  StringInterpolateInstr(Value* value, intptr_t token_pos)
      : token_pos_(token_pos), function_(Function::Handle()) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }
  virtual intptr_t token_pos() const { return token_pos_; }

  virtual CompileType ComputeType() const;
  // Issues a static call to Dart code which calls toString on objects.
  virtual EffectSet Effects() const { return EffectSet::All(); }
  virtual bool CanDeoptimize() const { return true; }
  virtual bool MayThrow() const { return true; }

  const Function& CallFunction() const;

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  DECLARE_INSTRUCTION(StringInterpolate)

 private:
  const intptr_t token_pos_;
  Function& function_;

  DISALLOW_COPY_AND_ASSIGN(StringInterpolateInstr);
};


class StoreIndexedInstr : public TemplateDefinition<3> {
 public:
  StoreIndexedInstr(Value* array,
                    Value* index,
                    Value* value,
                    StoreBarrierType emit_store_barrier,
                    intptr_t index_scale,
                    intptr_t class_id,
                    intptr_t deopt_id,
                    intptr_t token_pos)
      : emit_store_barrier_(emit_store_barrier),
        index_scale_(index_scale),
        class_id_(class_id),
        token_pos_(token_pos) {
    SetInputAt(kArrayPos, array);
    SetInputAt(kIndexPos, index);
    SetInputAt(kValuePos, value);
    deopt_id_ = deopt_id;
  }

  DECLARE_INSTRUCTION(StoreIndexed)

  enum {
    kArrayPos = 0,
    kIndexPos = 1,
    kValuePos = 2
  };

  Value* array() const { return inputs_[kArrayPos]; }
  Value* index() const { return inputs_[kIndexPos]; }
  Value* value() const { return inputs_[kValuePos]; }

  intptr_t index_scale() const { return index_scale_; }
  intptr_t class_id() const { return class_id_; }

  bool ShouldEmitStoreBarrier() const {
    return value()->NeedsStoreBuffer()
        && (emit_store_barrier_ == kEmitStoreBarrier);
  }

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const;

  bool IsExternal() const {
    return array()->definition()->representation() == kUntagged;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  const StoreBarrierType emit_store_barrier_;
  const intptr_t index_scale_;
  const intptr_t class_id_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(StoreIndexedInstr);
};


// Note overrideable, built-in: value ? false : true.
class BooleanNegateInstr : public TemplateDefinition<1> {
 public:
  explicit BooleanNegateInstr(Value* value) {
    SetInputAt(0, value);
  }

  DECLARE_INSTRUCTION(BooleanNegate)
  virtual CompileType ComputeType() const;

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(BooleanNegateInstr);
};


class InstanceOfInstr : public TemplateDefinition<3> {
 public:
  InstanceOfInstr(intptr_t token_pos,
                  Value* value,
                  Value* instantiator,
                  Value* instantiator_type_arguments,
                  const AbstractType& type,
                  bool negate_result,
                  intptr_t deopt_id)
      : token_pos_(token_pos),
        type_(type),
        negate_result_(negate_result) {
    ASSERT(!type.IsNull());
    SetInputAt(0, value);
    SetInputAt(1, instantiator);
    SetInputAt(2, instantiator_type_arguments);
    deopt_id_ = deopt_id;
  }

  DECLARE_INSTRUCTION(InstanceOf)
  virtual CompileType ComputeType() const;

  Value* value() const { return inputs_[0]; }
  Value* instantiator() const { return inputs_[1]; }
  Value* instantiator_type_arguments() const { return inputs_[2]; }

  bool negate_result() const { return negate_result_; }
  const AbstractType& type() const { return type_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return true; }

 private:
  const intptr_t token_pos_;
  Value* value_;
  Value* instantiator_;
  Value* type_arguments_;
  const AbstractType& type_;
  const bool negate_result_;

  DISALLOW_COPY_AND_ASSIGN(InstanceOfInstr);
};


class AllocateObjectInstr : public TemplateDefinition<0> {
 public:
  AllocateObjectInstr(intptr_t token_pos,
                      const Class& cls,
                      ZoneGrowableArray<PushArgumentInstr*>* arguments)
      : token_pos_(token_pos),
        cls_(cls),
        arguments_(arguments),
        identity_(kIdentityUnknown),
        closure_function_(Function::ZoneHandle()) {
    // Either no arguments or one type-argument and one instantiator.
    ASSERT(arguments->is_empty() || (arguments->length() == 1));
  }

  DECLARE_INSTRUCTION(AllocateObject)
  virtual CompileType ComputeType() const;

  virtual intptr_t ArgumentCount() const { return arguments_->length(); }
  virtual PushArgumentInstr* PushArgumentAt(intptr_t index) const {
    return (*arguments_)[index];
  }

  const Class& cls() const { return cls_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  const Function& closure_function() const { return closure_function_; }
  void set_closure_function(const Function& function) {
    closure_function_ ^= function.raw();
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  virtual AliasIdentity Identity() const { return identity_; }
  virtual void SetIdentity(AliasIdentity identity) { identity_ = identity; }

 private:
  const intptr_t token_pos_;
  const Class& cls_;
  ZoneGrowableArray<PushArgumentInstr*>* const arguments_;
  AliasIdentity identity_;
  Function& closure_function_;

  DISALLOW_COPY_AND_ASSIGN(AllocateObjectInstr);
};


// This instruction captures the state of the object which had its allocation
// removed during the AllocationSinking pass.
// It does not produce any real code only deoptimization information.
class MaterializeObjectInstr : public Definition {
 public:
  MaterializeObjectInstr(const Class& cls,
                         const ZoneGrowableArray<const Object*>& slots,
                         ZoneGrowableArray<Value*>* values)
      : cls_(cls), slots_(slots), values_(values), locations_(NULL) {
    ASSERT(slots_.length() == values_->length());
    for (intptr_t i = 0; i < InputCount(); i++) {
      InputAt(i)->set_instruction(this);
      InputAt(i)->set_use_index(i);
    }
  }

  const Class& cls() const { return cls_; }
  intptr_t FieldOffsetAt(intptr_t i) const {
    return slots_[i]->IsField()
        ? Field::Cast(*slots_[i]).Offset()
        : Smi::Cast(*slots_[i]).Value();
  }
  const Location& LocationAt(intptr_t i) {
    return locations_[i];
  }

  DECLARE_INSTRUCTION(MaterializeObject)
  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual intptr_t InputCount() const {
    return values_->length();
  }

  virtual Value* InputAt(intptr_t i) const {
    return (*values_)[i];
  }

  // SelectRepresentations pass is run once more while MaterializeObject
  // instructions are still in the graph. To avoid any redundant boxing
  // operations inserted by that pass we should indicate that this
  // instruction can cope with any representation as it is essentially
  // an environment use.
  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(0 <= idx && idx < InputCount());
    return kNoRepresentation;
  }

  virtual bool CanDeoptimize() const { return false; }
  virtual EffectSet Effects() const { return EffectSet::None(); }

  Location* locations() { return locations_; }
  void set_locations(Location* locations) { locations_ = locations; }

  virtual bool MayThrow() const { return false; }

  void RemapRegisters(intptr_t* fpu_reg_slots,
                      intptr_t* cpu_reg_slots);

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    (*values_)[i] = value;
  }

  const Class& cls_;
  const ZoneGrowableArray<const Object*>& slots_;
  ZoneGrowableArray<Value*>* values_;
  Location* locations_;

  DISALLOW_COPY_AND_ASSIGN(MaterializeObjectInstr);
};


class CreateArrayInstr : public TemplateDefinition<2> {
 public:
  CreateArrayInstr(intptr_t token_pos,
                   Value* element_type,
                   Value* num_elements)
      : token_pos_(token_pos), identity_(kIdentityUnknown)  {
    SetInputAt(kElementTypePos, element_type);
    SetInputAt(kLengthPos, num_elements);
  }

  enum {
    kElementTypePos = 0,
    kLengthPos = 1
  };

  DECLARE_INSTRUCTION(CreateArray)
  virtual CompileType ComputeType() const;

  virtual intptr_t token_pos() const { return token_pos_; }
  Value* element_type() const { return inputs_[kElementTypePos]; }
  Value* num_elements() const { return inputs_[kLengthPos]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  // Throw needs environment, which is created only if instruction can
  // deoptimize.
  virtual bool CanDeoptimize() const { return MayThrow(); }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  // OutOfMemoryError can be called.
  virtual bool MayThrow() const { return true; }

  virtual AliasIdentity Identity() const { return identity_; }
  virtual void SetIdentity(AliasIdentity identity) { identity_ = identity; }

 private:
  const intptr_t token_pos_;
  AliasIdentity identity_;

  DISALLOW_COPY_AND_ASSIGN(CreateArrayInstr);
};


class LoadUntaggedInstr : public TemplateDefinition<1> {
 public:
  LoadUntaggedInstr(Value* object, intptr_t offset) : offset_(offset) {
    SetInputAt(0, object);
  }

  virtual Representation representation() const {
    return kUntagged;
  }
  DECLARE_INSTRUCTION(LoadUntagged)
  virtual CompileType ComputeType() const;

  Value* object() const { return inputs_[0]; }
  intptr_t offset() const { return offset_; }

  virtual bool CanDeoptimize() const { return false; }

  // This instruction must not be moved without the indexed access that
  // depends on it (e.g. out of loops). GC may cause collect
  // the array while the external data-array is still accessed.
  virtual bool AllowsCSE() const { return false; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  intptr_t offset_;

  DISALLOW_COPY_AND_ASSIGN(LoadUntaggedInstr);
};


class LoadClassIdInstr : public TemplateDefinition<1> {
 public:
  explicit LoadClassIdInstr(Value* object) {
    SetInputAt(0, object);
  }

  virtual Representation representation() const {
    return kTagged;
  }
  DECLARE_INSTRUCTION(LoadClassId)
  virtual CompileType ComputeType() const;

  Value* object() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const {
    return EffectSet::Externalization();
  }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(LoadClassIdInstr);
};




class LoadFieldInstr : public TemplateDefinition<1> {
 public:
  LoadFieldInstr(Value* instance,
                 intptr_t offset_in_bytes,
                 const AbstractType& type,
                 intptr_t token_pos)
      : offset_in_bytes_(offset_in_bytes),
        type_(type),
        result_cid_(kDynamicCid),
        immutable_(false),
        recognized_kind_(MethodRecognizer::kUnknown),
        field_(NULL),
        token_pos_(token_pos) {
    ASSERT(offset_in_bytes >= 0);
    ASSERT(type.IsZoneHandle());  // May be null if field is not an instance.
    SetInputAt(0, instance);
  }

  LoadFieldInstr(Value* instance,
                 const Field* field,
                 const AbstractType& type,
                 intptr_t token_pos)
      : offset_in_bytes_(field->Offset()),
        type_(type),
        result_cid_(kDynamicCid),
        immutable_(false),
        recognized_kind_(MethodRecognizer::kUnknown),
        field_(field),
        token_pos_(token_pos) {
    ASSERT(field->IsZoneHandle());
    ASSERT(type.IsZoneHandle());  // May be null if field is not an instance.
    SetInputAt(0, instance);
  }

  void set_is_immutable(bool value) { immutable_ = value; }

  Value* instance() const { return inputs_[0]; }
  intptr_t offset_in_bytes() const { return offset_in_bytes_; }
  const AbstractType& type() const { return type_; }
  void set_result_cid(intptr_t value) { result_cid_ = value; }
  intptr_t result_cid() const { return result_cid_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  const Field* field() const { return field_; }

  virtual Representation representation() const;

  bool IsUnboxedLoad() const;

  bool IsPotentialUnboxedLoad() const;

  void set_recognized_kind(MethodRecognizer::Kind kind) {
    recognized_kind_ = kind;
  }

  MethodRecognizer::Kind recognized_kind() const {
    return recognized_kind_;
  }

  DECLARE_INSTRUCTION(LoadField)
  virtual CompileType ComputeType() const;

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual void InferRange();

  bool IsImmutableLengthLoad() const;

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  static MethodRecognizer::Kind RecognizedKindFromArrayCid(intptr_t cid);

  static bool IsFixedLengthArrayCid(intptr_t cid);

  virtual bool AllowsCSE() const { return immutable_; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const;
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t offset_in_bytes_;
  const AbstractType& type_;
  intptr_t result_cid_;
  bool immutable_;

  MethodRecognizer::Kind recognized_kind_;
  const Field* field_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(LoadFieldInstr);
};


class InstantiateTypeInstr : public TemplateDefinition<1> {
 public:
  InstantiateTypeInstr(intptr_t token_pos,
                       const AbstractType& type,
                       const Class& instantiator_class,
                       Value* instantiator)
      : token_pos_(token_pos),
        type_(type),
        instantiator_class_(instantiator_class) {
    ASSERT(type.IsZoneHandle());
    SetInputAt(0, instantiator);
  }

  DECLARE_INSTRUCTION(InstantiateType)

  Value* instantiator() const { return inputs_[0]; }
  const AbstractType& type() const { return type_;
  }
  const Class& instantiator_class() const { return instantiator_class_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return true; }

 private:
  const intptr_t token_pos_;
  const AbstractType& type_;
  const Class& instantiator_class_;

  DISALLOW_COPY_AND_ASSIGN(InstantiateTypeInstr);
};


class InstantiateTypeArgumentsInstr : public TemplateDefinition<1> {
 public:
  InstantiateTypeArgumentsInstr(intptr_t token_pos,
                                const TypeArguments& type_arguments,
                                const Class& instantiator_class,
                                Value* instantiator)
      : token_pos_(token_pos),
        type_arguments_(type_arguments),
        instantiator_class_(instantiator_class) {
    ASSERT(type_arguments.IsZoneHandle());
    SetInputAt(0, instantiator);
  }

  DECLARE_INSTRUCTION(InstantiateTypeArguments)

  Value* instantiator() const { return inputs_[0]; }
  const TypeArguments& type_arguments() const {
    return type_arguments_;
  }
  const Class& instantiator_class() const { return instantiator_class_; }
  virtual intptr_t token_pos() const { return token_pos_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return true; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  const intptr_t token_pos_;
  const TypeArguments& type_arguments_;
  const Class& instantiator_class_;

  DISALLOW_COPY_AND_ASSIGN(InstantiateTypeArgumentsInstr);
};


class AllocateContextInstr : public TemplateDefinition<0> {
 public:
  AllocateContextInstr(intptr_t token_pos,
                       intptr_t num_context_variables)
      : token_pos_(token_pos),
        num_context_variables_(num_context_variables) {}

  DECLARE_INSTRUCTION(AllocateContext)
  virtual CompileType ComputeType() const;

  virtual intptr_t token_pos() const { return token_pos_; }
  intptr_t num_context_variables() const { return num_context_variables_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t token_pos_;
  const intptr_t num_context_variables_;

  DISALLOW_COPY_AND_ASSIGN(AllocateContextInstr);
};


class CloneContextInstr : public TemplateDefinition<1> {
 public:
  CloneContextInstr(intptr_t token_pos, Value* context_value)
      : token_pos_(token_pos) {
    SetInputAt(0, context_value);
  }

  virtual intptr_t token_pos() const { return token_pos_; }
  Value* context_value() const { return inputs_[0]; }

  DECLARE_INSTRUCTION(CloneContext)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(CloneContextInstr);
};


class CheckEitherNonSmiInstr : public TemplateInstruction<2> {
 public:
  CheckEitherNonSmiInstr(Value* left,
                         Value* right,
                         intptr_t deopt_id) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  DECLARE_INSTRUCTION(CheckEitherNonSmi)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return true; }

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(CheckEitherNonSmiInstr);
};


class BoxDoubleInstr : public TemplateDefinition<1> {
 public:
  explicit BoxDoubleInstr(Value* value) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  DECLARE_INSTRUCTION(BoxDouble)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual intptr_t DeoptimizationTarget() const {
    return Isolate::kNoDeoptId;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(BoxDoubleInstr);
};


class BoxFloat32x4Instr : public TemplateDefinition<1> {
 public:
  explicit BoxFloat32x4Instr(Value* value) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual intptr_t DeoptimizationTarget() const {
    return Isolate::kNoDeoptId;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat32x4;
  }

  DECLARE_INSTRUCTION(BoxFloat32x4)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(BoxFloat32x4Instr);
};


class BoxFloat64x2Instr : public TemplateDefinition<1> {
 public:
  explicit BoxFloat64x2Instr(Value* value) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual intptr_t DeoptimizationTarget() const {
    return Isolate::kNoDeoptId;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat64x2;
  }

  DECLARE_INSTRUCTION(BoxFloat64x2)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(BoxFloat64x2Instr);
};



class BoxInt32x4Instr : public TemplateDefinition<1> {
 public:
  explicit BoxInt32x4Instr(Value* value) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual intptr_t DeoptimizationTarget() const {
    return Isolate::kNoDeoptId;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedInt32x4;
  }

  DECLARE_INSTRUCTION(BoxInt32x4)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(BoxInt32x4Instr);
};


class BoxIntegerInstr : public TemplateDefinition<1> {
 public:
  explicit BoxIntegerInstr(Value* value) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const { return false; }

  virtual intptr_t DeoptimizationTarget() const {
    return Isolate::kNoDeoptId;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedMint;
  }

  DECLARE_INSTRUCTION(BoxInteger)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(BoxIntegerInstr);
};


class UnboxDoubleInstr : public TemplateDefinition<1> {
 public:
  UnboxDoubleInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const {
    return (value()->Type()->ToCid() != kDoubleCid)
        && (value()->Type()->ToCid() != kSmiCid);
  }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  DECLARE_INSTRUCTION(UnboxDouble)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(UnboxDoubleInstr);
};


class UnboxFloat32x4Instr : public TemplateDefinition<1> {
 public:
  UnboxFloat32x4Instr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const {
    return (value()->Type()->ToCid() != kFloat32x4Cid);
  }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  DECLARE_INSTRUCTION(UnboxFloat32x4)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(UnboxFloat32x4Instr);
};


class UnboxFloat64x2Instr : public TemplateDefinition<1> {
 public:
  UnboxFloat64x2Instr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const {
    return (value()->Type()->ToCid() != kFloat64x2Cid);
  }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  DECLARE_INSTRUCTION(UnboxFloat64x2)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(UnboxFloat64x2Instr);
};


class UnboxInt32x4Instr : public TemplateDefinition<1> {
 public:
  UnboxInt32x4Instr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const {
    return (value()->Type()->ToCid() != kInt32x4Cid);
  }

  virtual Representation representation() const {
    return kUnboxedInt32x4;
  }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  DECLARE_INSTRUCTION(UnboxInt32x4)
  virtual CompileType ComputeType() const;

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(UnboxInt32x4Instr);
};


class UnboxIntegerInstr : public TemplateDefinition<1> {
 public:
  UnboxIntegerInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual bool CanDeoptimize() const {
    return (value()->Type()->ToCid() != kSmiCid)
        && (value()->Type()->ToCid() != kMintCid);
  }

  virtual Representation representation() const {
    return kUnboxedMint;
  }


  virtual void InferRange();

  DECLARE_INSTRUCTION(UnboxInteger)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(UnboxIntegerInstr);
};


class MathUnaryInstr : public TemplateDefinition<1> {
 public:
  enum MathUnaryKind {
    kIllegal,
    kSin,
    kCos,
    kSqrt,
    kDoubleSquare,
  };
  MathUnaryInstr(MathUnaryKind kind, Value* value, intptr_t deopt_id)
      : kind_(kind) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }
  MathUnaryKind kind() const { return kind_; }
  const RuntimeEntry& TargetFunction() const;

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(MathUnary)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return kind() == other->AsMathUnary()->kind();
  }

  virtual bool MayThrow() const { return false; }

  Definition* Canonicalize(FlowGraph* flow_graph);

  static const char* KindToCString(MathUnaryKind kind);

 private:
  const MathUnaryKind kind_;

  DISALLOW_COPY_AND_ASSIGN(MathUnaryInstr);
};


// Represents Math's static min and max functions.
class MathMinMaxInstr : public TemplateDefinition<2> {
 public:
  MathMinMaxInstr(MethodRecognizer::Kind op_kind,
                  Value* left_value,
                  Value* right_value,
                  intptr_t deopt_id,
                  intptr_t result_cid)
      : op_kind_(op_kind), result_cid_(result_cid) {
    ASSERT((result_cid == kSmiCid) || (result_cid == kDoubleCid));
    SetInputAt(0, left_value);
    SetInputAt(1, right_value);
    deopt_id_ = deopt_id;
  }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  intptr_t result_cid() const { return result_cid_; }

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    if (result_cid() == kSmiCid) {
      return kTagged;
    }
    ASSERT(result_cid() == kDoubleCid);
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    if (result_cid() == kSmiCid) {
      return kTagged;
    }
    ASSERT(result_cid() == kDoubleCid);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(MathMinMax)
  virtual CompileType ComputeType() const;
  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;
  const intptr_t result_cid_;

  DISALLOW_COPY_AND_ASSIGN(MathMinMaxInstr);
};


class BinaryDoubleOpInstr : public TemplateDefinition<2> {
 public:
  BinaryDoubleOpInstr(Token::Kind op_kind,
                      Value* left,
                      Value* right,
                      intptr_t deopt_id,
                      intptr_t token_pos)
      : op_kind_(op_kind), token_pos_(token_pos) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    // Overriden generated deopt_id.
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual intptr_t token_pos() const { return token_pos_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(BinaryDoubleOp)
  virtual CompileType ComputeType() const;

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsBinaryDoubleOp()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(BinaryDoubleOpInstr);
};


class BinaryFloat32x4OpInstr : public TemplateDefinition<2> {
 public:
  BinaryFloat32x4OpInstr(Token::Kind op_kind,
                         Value* left,
                         Value* right,
                         intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(BinaryFloat32x4Op)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsBinaryFloat32x4Op()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(BinaryFloat32x4OpInstr);
};


class Simd32x4ShuffleInstr : public TemplateDefinition<1> {
 public:
  Simd32x4ShuffleInstr(MethodRecognizer::Kind op_kind, Value* value,
                       intptr_t mask,
                       intptr_t deopt_id)
      : op_kind_(op_kind), mask_(mask) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  intptr_t mask() const { return mask_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    if ((op_kind_ == MethodRecognizer::kFloat32x4ShuffleX) ||
        (op_kind_ == MethodRecognizer::kFloat32x4ShuffleY) ||
        (op_kind_ == MethodRecognizer::kFloat32x4ShuffleZ) ||
        (op_kind_ == MethodRecognizer::kFloat32x4ShuffleW)) {
      return kUnboxedDouble;
    }
    if ((op_kind_ == MethodRecognizer::kInt32x4Shuffle)) {
      return kUnboxedInt32x4;
    }
    ASSERT((op_kind_ == MethodRecognizer::kFloat32x4Shuffle));
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    if ((op_kind_ == MethodRecognizer::kFloat32x4ShuffleX) ||
        (op_kind_ == MethodRecognizer::kFloat32x4ShuffleY) ||
        (op_kind_ == MethodRecognizer::kFloat32x4ShuffleZ) ||
        (op_kind_ == MethodRecognizer::kFloat32x4ShuffleW) ||
        (op_kind_ == MethodRecognizer::kFloat32x4Shuffle)) {
      return kUnboxedFloat32x4;
    }
    ASSERT((op_kind_ == MethodRecognizer::kInt32x4Shuffle));
    return kUnboxedInt32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Simd32x4Shuffle)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return (op_kind() == other->AsSimd32x4Shuffle()->op_kind()) &&
           (mask() == other->AsSimd32x4Shuffle()->mask());
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;
  const intptr_t mask_;

  DISALLOW_COPY_AND_ASSIGN(Simd32x4ShuffleInstr);
};


class Simd32x4ShuffleMixInstr : public TemplateDefinition<2> {
 public:
  Simd32x4ShuffleMixInstr(MethodRecognizer::Kind op_kind, Value* xy,
                           Value* zw, intptr_t mask, intptr_t deopt_id)
      : op_kind_(op_kind), mask_(mask) {
    SetInputAt(0, xy);
    SetInputAt(1, zw);
    deopt_id_ = deopt_id;
  }

  Value* xy() const { return inputs_[0]; }
  Value* zw() const { return inputs_[1]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  intptr_t mask() const { return mask_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    if (op_kind() == MethodRecognizer::kInt32x4ShuffleMix) {
      return kUnboxedInt32x4;
    }
    ASSERT(op_kind() == MethodRecognizer::kFloat32x4ShuffleMix);
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    if (op_kind() == MethodRecognizer::kInt32x4ShuffleMix) {
      return kUnboxedInt32x4;
    }
    ASSERT(op_kind() == MethodRecognizer::kFloat32x4ShuffleMix);
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Simd32x4ShuffleMix)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return (op_kind() == other->AsSimd32x4ShuffleMix()->op_kind()) &&
           (mask() == other->AsSimd32x4ShuffleMix()->mask());
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;
  const intptr_t mask_;

  DISALLOW_COPY_AND_ASSIGN(Simd32x4ShuffleMixInstr);
};


class Float32x4ConstructorInstr : public TemplateDefinition<4> {
 public:
  Float32x4ConstructorInstr(Value* value0, Value* value1, Value* value2,
                            Value* value3, intptr_t deopt_id) {
    SetInputAt(0, value0);
    SetInputAt(1, value1);
    SetInputAt(2, value2);
    SetInputAt(3, value3);
    deopt_id_ = deopt_id;
  }

  Value* value0() const { return inputs_[0]; }
  Value* value1() const { return inputs_[1]; }
  Value* value2() const { return inputs_[2]; }
  Value* value3() const { return inputs_[3]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx >= 0 && idx < 4);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Constructor)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float32x4ConstructorInstr);
};


class Float32x4SplatInstr : public TemplateDefinition<1> {
 public:
  Float32x4SplatInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Splat)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float32x4SplatInstr);
};


class Float32x4ZeroInstr : public TemplateDefinition<0> {
 public:
  explicit Float32x4ZeroInstr(intptr_t deopt_id) {
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    UNIMPLEMENTED();
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Zero)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float32x4ZeroInstr);
};


class Float32x4ComparisonInstr : public TemplateDefinition<2> {
 public:
  Float32x4ComparisonInstr(MethodRecognizer::Kind op_kind, Value* left,
                           Value* right, intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedInt32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Comparison)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat32x4Comparison()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float32x4ComparisonInstr);
};


class Float32x4MinMaxInstr : public TemplateDefinition<2> {
 public:
  Float32x4MinMaxInstr(MethodRecognizer::Kind op_kind, Value* left,
                       Value* right, intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4MinMax)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat32x4MinMax()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float32x4MinMaxInstr);
};


class Float32x4ScaleInstr : public TemplateDefinition<2> {
 public:
  Float32x4ScaleInstr(MethodRecognizer::Kind op_kind, Value* left,
                      Value* right, intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    if (idx == 0) {
      return kUnboxedDouble;
    }
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Scale)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat32x4Scale()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float32x4ScaleInstr);
};


class Float32x4SqrtInstr : public TemplateDefinition<1> {
 public:
  Float32x4SqrtInstr(MethodRecognizer::Kind op_kind, Value* left,
                     intptr_t deopt_id) : op_kind_(op_kind) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Sqrt)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat32x4Sqrt()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float32x4SqrtInstr);
};


class Float32x4ZeroArgInstr : public TemplateDefinition<1> {
 public:
  Float32x4ZeroArgInstr(MethodRecognizer::Kind op_kind, Value* left,
                        intptr_t deopt_id) : op_kind_(op_kind) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4ZeroArg)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat32x4ZeroArg()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float32x4ZeroArgInstr);
};


class Float32x4ClampInstr : public TemplateDefinition<3> {
 public:
  Float32x4ClampInstr(Value* left, Value* lower, Value* upper,
                      intptr_t deopt_id) {
    SetInputAt(0, left);
    SetInputAt(1, lower);
    SetInputAt(2, upper);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* lower() const { return inputs_[1]; }
  Value* upper() const { return inputs_[2]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1) || (idx == 2));
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4Clamp)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float32x4ClampInstr);
};


class Float32x4WithInstr : public TemplateDefinition<2> {
 public:
  Float32x4WithInstr(MethodRecognizer::Kind op_kind, Value* left,
                     Value* replacement, intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, replacement);
    SetInputAt(1, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[1]; }
  Value* replacement() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    if (idx == 0) {
      return kUnboxedDouble;
    }
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4With)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat32x4With()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float32x4WithInstr);
};


class Simd64x2ShuffleInstr : public TemplateDefinition<1> {
 public:
  Simd64x2ShuffleInstr(MethodRecognizer::Kind op_kind, Value* value,
                       intptr_t mask,
                       intptr_t deopt_id)
      : op_kind_(op_kind), mask_(mask) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  intptr_t mask() const { return mask_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    if ((op_kind_ == MethodRecognizer::kFloat64x2GetX) ||
        (op_kind_ == MethodRecognizer::kFloat64x2GetY)) {
      return kUnboxedDouble;
    }
    UNIMPLEMENTED();
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    if ((op_kind_ == MethodRecognizer::kFloat64x2GetX) ||
        (op_kind_ == MethodRecognizer::kFloat64x2GetY)) {
      return kUnboxedFloat64x2;
    }
    UNIMPLEMENTED();
    return kUnboxedFloat64x2;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Simd64x2Shuffle)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return (op_kind() == other->AsSimd64x2Shuffle()->op_kind()) &&
           (mask() == other->AsSimd64x2Shuffle()->mask());
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;
  const intptr_t mask_;

  DISALLOW_COPY_AND_ASSIGN(Simd64x2ShuffleInstr);
};


class Float32x4ToInt32x4Instr : public TemplateDefinition<1> {
 public:
  Float32x4ToInt32x4Instr(Value* left, intptr_t deopt_id) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedInt32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4ToInt32x4)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float32x4ToInt32x4Instr);
};


class Float32x4ToFloat64x2Instr : public TemplateDefinition<1> {
 public:
  Float32x4ToFloat64x2Instr(Value* left, intptr_t deopt_id) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float32x4ToFloat64x2)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float32x4ToFloat64x2Instr);
};


class Float64x2ToFloat32x4Instr : public TemplateDefinition<1> {
 public:
  Float64x2ToFloat32x4Instr(Value* left, intptr_t deopt_id) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat64x2;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float64x2ToFloat32x4)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float64x2ToFloat32x4Instr);
};


class Float64x2ConstructorInstr : public TemplateDefinition<2> {
 public:
  Float64x2ConstructorInstr(Value* value0, Value* value1, intptr_t deopt_id) {
    SetInputAt(0, value0);
    SetInputAt(1, value1);
    deopt_id_ = deopt_id;
  }

  Value* value0() const { return inputs_[0]; }
  Value* value1() const { return inputs_[1]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx >= 0 && idx < 2);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float64x2Constructor)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float64x2ConstructorInstr);
};


class Float64x2SplatInstr : public TemplateDefinition<1> {
 public:
  Float64x2SplatInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float64x2Splat)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float64x2SplatInstr);
};


class Float64x2ZeroInstr : public TemplateDefinition<0> {
 public:
  explicit Float64x2ZeroInstr(intptr_t deopt_id) {
    deopt_id_ = deopt_id;
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    UNIMPLEMENTED();
    return kUnboxedFloat64x2;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float64x2Zero)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Float64x2ZeroInstr);
};


class Float64x2ZeroArgInstr : public TemplateDefinition<1> {
 public:
  Float64x2ZeroArgInstr(MethodRecognizer::Kind op_kind, Value* left,
                        intptr_t deopt_id) : op_kind_(op_kind) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    if (op_kind() == MethodRecognizer::kFloat64x2GetSignMask) {
      // Smi.
      return kTagged;
    }
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedFloat64x2;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float64x2ZeroArg)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat64x2ZeroArg()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float64x2ZeroArgInstr);
};


class Float64x2OneArgInstr : public TemplateDefinition<2> {
 public:
  Float64x2OneArgInstr(MethodRecognizer::Kind op_kind, Value* left,
                       Value* right, intptr_t deopt_id) : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    if (idx == 0) {
      return kUnboxedFloat64x2;
    }
    ASSERT(idx == 1);
    if ((op_kind() == MethodRecognizer::kFloat64x2WithX) ||
        (op_kind() == MethodRecognizer::kFloat64x2WithY) ||
        (op_kind() == MethodRecognizer::kFloat64x2Scale)) {
      return kUnboxedDouble;
    }
    return kUnboxedFloat64x2;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Float64x2OneArg)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsFloat64x2OneArg()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Float64x2OneArgInstr);
};


class Int32x4BoolConstructorInstr : public TemplateDefinition<4> {
 public:
  Int32x4BoolConstructorInstr(Value* value0, Value* value1, Value* value2,
                               Value* value3, intptr_t deopt_id) {
    SetInputAt(0, value0);
    SetInputAt(1, value1);
    SetInputAt(2, value2);
    SetInputAt(3, value3);
    deopt_id_ = deopt_id;
  }

  Value* value0() const { return inputs_[0]; }
  Value* value1() const { return inputs_[1]; }
  Value* value2() const { return inputs_[2]; }
  Value* value3() const { return inputs_[3]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedInt32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx >= 0 && idx < 4);
    return kTagged;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Int32x4BoolConstructor)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Int32x4BoolConstructorInstr);
};


class Int32x4GetFlagInstr : public TemplateDefinition<1> {
 public:
  Int32x4GetFlagInstr(MethodRecognizer::Kind op_kind, Value* value,
                       intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
      return kTagged;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedInt32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Int32x4GetFlag)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsInt32x4GetFlag()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Int32x4GetFlagInstr);
};


class Simd32x4GetSignMaskInstr : public TemplateDefinition<1> {
 public:
  Simd32x4GetSignMaskInstr(MethodRecognizer::Kind op_kind, Value* value,
                           intptr_t deopt_id) : op_kind_(op_kind) {
    SetInputAt(0, value);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kTagged;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    if (op_kind_ == MethodRecognizer::kFloat32x4GetSignMask) {
      return kUnboxedFloat32x4;
    }
    ASSERT(op_kind_ == MethodRecognizer::kInt32x4GetSignMask);
    return kUnboxedInt32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Simd32x4GetSignMask)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return other->AsSimd32x4GetSignMask()->op_kind() == op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Simd32x4GetSignMaskInstr);
};


class Int32x4SelectInstr : public TemplateDefinition<3> {
 public:
  Int32x4SelectInstr(Value* mask, Value* trueValue, Value* falseValue,
                      intptr_t deopt_id) {
    SetInputAt(0, mask);
    SetInputAt(1, trueValue);
    SetInputAt(2, falseValue);
    deopt_id_ = deopt_id;
  }

  Value* mask() const { return inputs_[0]; }
  Value* trueValue() const { return inputs_[1]; }
  Value* falseValue() const { return inputs_[2]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
      return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1) || (idx == 2));
    if (idx == 0) {
      return kUnboxedInt32x4;
    }
    return kUnboxedFloat32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Int32x4Select)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Int32x4SelectInstr);
};


class Int32x4SetFlagInstr : public TemplateDefinition<2> {
 public:
  Int32x4SetFlagInstr(MethodRecognizer::Kind op_kind, Value* value,
                       Value* flagValue, intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, value);
    SetInputAt(1, flagValue);
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }
  Value* flagValue() const { return inputs_[1]; }

  MethodRecognizer::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
      return kUnboxedInt32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    if (idx == 1) {
      return kTagged;
    }
    return kUnboxedInt32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Int32x4SetFlag)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsInt32x4SetFlag()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(Int32x4SetFlagInstr);
};


class Int32x4ToFloat32x4Instr : public TemplateDefinition<1> {
 public:
  Int32x4ToFloat32x4Instr(Value* left, intptr_t deopt_id) {
    SetInputAt(0, left);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedInt32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(Int32x4ToFloat32x4)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Int32x4ToFloat32x4Instr);
};


class BinaryInt32x4OpInstr : public TemplateDefinition<2> {
 public:
  BinaryInt32x4OpInstr(Token::Kind op_kind,
                        Value* left,
                        Value* right,
                        intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedInt32x4;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedInt32x4;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(BinaryInt32x4Op)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsBinaryInt32x4Op()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(BinaryInt32x4OpInstr);
};


class BinaryFloat64x2OpInstr : public TemplateDefinition<2> {
 public:
  BinaryFloat64x2OpInstr(Token::Kind op_kind,
                         Value* left,
                         Value* right,
                         intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedFloat64x2;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedFloat64x2;
  }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(BinaryFloat64x2Op)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    ASSERT(other->IsBinaryFloat64x2Op());
    return op_kind() == other->AsBinaryFloat64x2Op()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(BinaryFloat64x2OpInstr);
};


class BinaryMintOpInstr : public TemplateDefinition<2> {
 public:
  BinaryMintOpInstr(Token::Kind op_kind,
                           Value* left,
                           Value* right,
                           intptr_t deopt_id)
      : op_kind_(op_kind) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const {
    return FLAG_throw_on_javascript_int_overflow ||
        (op_kind() == Token::kADD) || (op_kind() == Token::kSUB);
  }

  virtual Representation representation() const {
    return kUnboxedMint;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return kUnboxedMint;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  virtual void InferRange();

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  DECLARE_INSTRUCTION(BinaryMintOp)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    ASSERT(other->IsBinaryMintOp());
    return op_kind() == other->AsBinaryMintOp()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(BinaryMintOpInstr);
};


class ShiftMintOpInstr : public TemplateDefinition<2> {
 public:
  ShiftMintOpInstr(Token::Kind op_kind,
                   Value* left,
                   Value* right,
                   intptr_t deopt_id)
      : op_kind_(op_kind) {
    ASSERT(op_kind == Token::kSHR || op_kind == Token::kSHL);
    SetInputAt(0, left);
    SetInputAt(1, right);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return true; }

  virtual CompileType ComputeType() const;

  virtual Representation representation() const {
    return kUnboxedMint;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((idx == 0) || (idx == 1));
    return (idx == 0) ? kUnboxedMint : kTagged;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(ShiftMintOp)

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsShiftMintOp()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(ShiftMintOpInstr);
};


class UnaryMintOpInstr : public TemplateDefinition<1> {
 public:
  UnaryMintOpInstr(Token::Kind op_kind, Value* value, intptr_t deopt_id)
      : op_kind_(op_kind) {
    ASSERT(op_kind == Token::kBIT_NOT);
    SetInputAt(0, value);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const {
    return FLAG_throw_on_javascript_int_overflow;
  }

  virtual Representation representation() const {
    return kUnboxedMint;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedMint;
  }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  DECLARE_INSTRUCTION(UnaryMintOp)
  virtual CompileType ComputeType() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return op_kind() == other->AsUnaryMintOp()->op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(UnaryMintOpInstr);
};


class BinarySmiOpInstr : public TemplateDefinition<2> {
 public:
  BinarySmiOpInstr(Token::Kind op_kind,
                   Value* left,
                   Value* right,
                   intptr_t deopt_id,
                   intptr_t token_pos)
      : op_kind_(op_kind),
        overflow_(true),
        is_truncating_(false),
        token_pos_(token_pos) {
    SetInputAt(0, left);
    SetInputAt(1, right);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* left() const { return inputs_[0]; }
  Value* right() const { return inputs_[1]; }

  virtual intptr_t token_pos() const { return token_pos_; }
  Token::Kind op_kind() const { return op_kind_; }

  void set_overflow(bool overflow) { overflow_ = overflow; }

  void set_is_truncating(bool value) { is_truncating_ = value; }
  bool is_truncating() const { return is_truncating_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  DECLARE_INSTRUCTION(BinarySmiOp)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const;

  void PrintTo(BufferFormatter* f) const;

  virtual void InferRange();

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

  // Returns true if right is a non-zero Smi constant which absolute value is
  // a power of two.
  bool RightIsPowerOfTwoConstant() const;

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;
  bool overflow_;
  bool is_truncating_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(BinarySmiOpInstr);
};


// Handles both Smi operations: BIT_OR and NEGATE.
class UnarySmiOpInstr : public TemplateDefinition<1> {
 public:
  UnarySmiOpInstr(Token::Kind op_kind,
                  Value* value,
                  intptr_t deopt_id)
      : op_kind_(op_kind) {
    ASSERT((op_kind == Token::kNEGATE) || (op_kind == Token::kBIT_NOT));
    SetInputAt(0, value);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }
  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  DECLARE_INSTRUCTION(UnarySmiOp)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return op_kind() == Token::kNEGATE; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return other->AsUnarySmiOp()->op_kind() == op_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(UnarySmiOpInstr);
};


// Handles only NEGATE.
class UnaryDoubleOpInstr : public TemplateDefinition<1> {
 public:
  UnaryDoubleOpInstr(Token::Kind op_kind,
                     Value* value,
                     intptr_t deopt_id)
      : op_kind_(op_kind) {
    ASSERT(op_kind == Token::kNEGATE);
    SetInputAt(0, value);
    // Overriden generated deopt_id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }
  Token::Kind op_kind() const { return op_kind_; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  DECLARE_INSTRUCTION(UnaryDoubleOp)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual intptr_t DeoptimizationTarget() const {
    // Direct access since this instruction cannot deoptimize, and the deopt-id
    // was inherited from another instruction that could deoptimize.
    return deopt_id_;
  }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  const Token::Kind op_kind_;

  DISALLOW_COPY_AND_ASSIGN(UnaryDoubleOpInstr);
};


class CheckStackOverflowInstr : public TemplateInstruction<0> {
 public:
  CheckStackOverflowInstr(intptr_t token_pos, intptr_t loop_depth)
      : token_pos_(token_pos), loop_depth_(loop_depth) {}

  virtual intptr_t token_pos() const { return token_pos_; }
  bool in_loop() const { return loop_depth_ > 0; }
  intptr_t loop_depth() const { return loop_depth_; }

  DECLARE_INSTRUCTION(CheckStackOverflow)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

  virtual void PrintOperandsTo(BufferFormatter* f) const;

 private:
  const intptr_t token_pos_;
  const intptr_t loop_depth_;

  DISALLOW_COPY_AND_ASSIGN(CheckStackOverflowInstr);
};


class SmiToDoubleInstr : public TemplateDefinition<1> {
 public:
  SmiToDoubleInstr(Value* value, intptr_t token_pos)
      : token_pos_(token_pos) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }
  virtual intptr_t token_pos() const { return token_pos_; }

  DECLARE_INSTRUCTION(SmiToDouble)
  virtual CompileType ComputeType() const;

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual intptr_t ArgumentCount() const { return 1; }

  virtual bool CanDeoptimize() const { return false; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(SmiToDoubleInstr);
};


class DoubleToIntegerInstr : public TemplateDefinition<1> {
 public:
  DoubleToIntegerInstr(Value* value, InstanceCallInstr* instance_call)
      : instance_call_(instance_call) {
    SetInputAt(0, value);
    deopt_id_ = instance_call->deopt_id();
  }

  Value* value() const { return inputs_[0]; }
  InstanceCallInstr* instance_call() const { return instance_call_; }

  DECLARE_INSTRUCTION(DoubleToInteger)
  virtual CompileType ComputeType() const;

  virtual intptr_t ArgumentCount() const { return 1; }

  virtual bool CanDeoptimize() const { return true; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return true; }

 private:
  InstanceCallInstr* instance_call_;

  DISALLOW_COPY_AND_ASSIGN(DoubleToIntegerInstr);
};


// Similar to 'DoubleToIntegerInstr' but expects unboxed double as input
// and creates a Smi.
class DoubleToSmiInstr : public TemplateDefinition<1> {
 public:
  DoubleToSmiInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  DECLARE_INSTRUCTION(DoubleToSmi)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return true; }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const { return deopt_id_; }

  virtual EffectSet Effects() const { return EffectSet::None(); }

  virtual bool MayThrow() const { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DoubleToSmiInstr);
};


class DoubleToDoubleInstr : public TemplateDefinition<1> {
 public:
  DoubleToDoubleInstr(Value* value,
                      MethodRecognizer::Kind recognized_kind,
                      intptr_t deopt_id)
    : recognized_kind_(recognized_kind) {
    SetInputAt(0, value);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  MethodRecognizer::Kind recognized_kind() const { return recognized_kind_; }

  DECLARE_INSTRUCTION(DoubleToDouble)
  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const { return deopt_id_; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    return other->AsDoubleToDouble()->recognized_kind() == recognized_kind();
  }

  virtual bool MayThrow() const { return false; }

 private:
  const MethodRecognizer::Kind recognized_kind_;

  DISALLOW_COPY_AND_ASSIGN(DoubleToDoubleInstr);
};


class DoubleToFloatInstr: public TemplateDefinition<1> {
 public:
  DoubleToFloatInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  DECLARE_INSTRUCTION(DoubleToFloat)

  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    // This works since double is the representation that the typed array
    // store expects.
    // TODO(fschneider): Change this to a genuine float representation once it
    // is supported.
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const { return deopt_id_; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(DoubleToFloatInstr);
};


class FloatToDoubleInstr: public TemplateDefinition<1> {
 public:
  FloatToDoubleInstr(Value* value, intptr_t deopt_id) {
    SetInputAt(0, value);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* value() const { return inputs_[0]; }

  DECLARE_INSTRUCTION(FloatToDouble)

  virtual CompileType ComputeType() const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const { return deopt_id_; }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  virtual Definition* Canonicalize(FlowGraph* flow_graph);

 private:
  DISALLOW_COPY_AND_ASSIGN(FloatToDoubleInstr);
};


class InvokeMathCFunctionInstr : public Definition {
 public:
  InvokeMathCFunctionInstr(ZoneGrowableArray<Value*>* inputs,
                           intptr_t original_deopt_id,
                           MethodRecognizer::Kind recognized_kind,
                           intptr_t token_pos);

  static intptr_t ArgumentCountFor(MethodRecognizer::Kind recognized_kind_);

  const RuntimeEntry& TargetFunction() const;

  MethodRecognizer::Kind recognized_kind() const { return recognized_kind_; }

  virtual intptr_t token_pos() const { return token_pos_; }

  DECLARE_INSTRUCTION(InvokeMathCFunction)
  virtual CompileType ComputeType() const;
  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const { return false; }

  virtual Representation representation() const {
    return kUnboxedDouble;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((0 <= idx) && (idx < InputCount()));
    return kUnboxedDouble;
  }

  virtual intptr_t DeoptimizationTarget() const { return deopt_id_; }

  virtual intptr_t InputCount() const {
    return inputs_->length();
  }

  virtual Value* InputAt(intptr_t i) const {
    return (*inputs_)[i];
  }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    InvokeMathCFunctionInstr* other_invoke = other->AsInvokeMathCFunction();
    return other_invoke->recognized_kind() == recognized_kind();
  }

  virtual bool MayThrow() const { return false; }

  static const intptr_t kSavedSpTempIndex = 0;
  static const intptr_t kObjectTempIndex = 1;
  static const intptr_t kDoubleTempIndex = 2;

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    (*inputs_)[i] = value;
  }

  ZoneGrowableArray<Value*>* inputs_;
  const MethodRecognizer::Kind recognized_kind_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(InvokeMathCFunctionInstr);
};


class ExtractNthOutputInstr : public TemplateDefinition<1> {
 public:
  // Extract the Nth output register from value.
  ExtractNthOutputInstr(Value* value,
                        intptr_t n,
                        Representation definition_rep,
                        intptr_t definition_cid)
      : index_(n),
        definition_rep_(definition_rep),
        definition_cid_(definition_cid) {
    SetInputAt(0, value);
  }

  Value* value() const { return inputs_[0]; }

  DECLARE_INSTRUCTION(ExtractNthOutput)

  virtual CompileType ComputeType() const;
  virtual void PrintOperandsTo(BufferFormatter* f) const;
  virtual bool CanDeoptimize() const { return false; }

  intptr_t index() const { return index_; }

  virtual Representation representation() const {
    return definition_rep_;
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT(idx == 0);
    if (representation() == kTagged) {
      return kPairOfTagged;
    } else if (representation() == kUnboxedDouble) {
      return kPairOfUnboxedDouble;
    }
    UNREACHABLE();
    return definition_rep_;
  }

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    ExtractNthOutputInstr* other_extract = other->AsExtractNthOutput();
    return (other_extract->representation() == representation()) &&
           (other_extract->index() == index());
  }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t index_;
  const Representation definition_rep_;
  const intptr_t definition_cid_;
  DISALLOW_COPY_AND_ASSIGN(ExtractNthOutputInstr);
};


class MergedMathInstr : public Definition {
 public:
  enum Kind {
    kTruncDivMod,
    kSinCos,
  };

  MergedMathInstr(ZoneGrowableArray<Value*>* inputs,
                  intptr_t original_deopt_id,
                  MergedMathInstr::Kind kind);

  static intptr_t InputCountFor(MergedMathInstr::Kind kind) {
    if (kind == kTruncDivMod) {
      return 2;
    } else if (kind == kSinCos) {
      return 1;
    } else {
      UNIMPLEMENTED();
      return -1;
    }
  }

  MergedMathInstr::Kind kind() const { return kind_; }

  virtual intptr_t InputCount() const { return inputs_->length(); }

  virtual Value* InputAt(intptr_t i) const {
    return (*inputs_)[i];
  }

  static intptr_t OutputIndexOf(intptr_t kind);
  static intptr_t OutputIndexOf(Token::Kind token);

  virtual CompileType ComputeType() const;
  virtual void PrintOperandsTo(BufferFormatter* f) const;

  virtual bool CanDeoptimize() const {
    if (kind_ == kTruncDivMod) {
      return true;
    } else if (kind_ == kSinCos) {
      return false;
    } else {
      UNIMPLEMENTED();
      return false;
    }
  }

  virtual Representation representation() const {
    if (kind_ == kTruncDivMod) {
      return kPairOfTagged;
    } else if (kind_ == kSinCos) {
      return kPairOfUnboxedDouble;
    } else {
      UNIMPLEMENTED();
      return kTagged;
    }
  }

  virtual Representation RequiredInputRepresentation(intptr_t idx) const {
    ASSERT((0 <= idx) && (idx < InputCount()));
    if (kind_ == kTruncDivMod) {
      return kTagged;
    } else if (kind_ == kSinCos) {
      return kUnboxedDouble;
    } else {
      UNIMPLEMENTED();
      return kTagged;
    }
  }

  virtual intptr_t DeoptimizationTarget() const { return deopt_id_; }

  DECLARE_INSTRUCTION(MergedMath)

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const {
    MergedMathInstr* other_invoke = other->AsMergedMath();
    return other_invoke->kind() == kind();
  }

  virtual bool MayThrow() const { return false; }

  static const char* KindToCString(MergedMathInstr::Kind kind) {
    if (kind == kTruncDivMod) return "TruncDivMod";
    if (kind == kSinCos) return "SinCos";
    UNIMPLEMENTED();
    return "";
  }

 private:
  virtual void RawSetInputAt(intptr_t i, Value* value) {
    (*inputs_)[i] = value;
  }
  ZoneGrowableArray<Value*>* inputs_;
  MergedMathInstr::Kind kind_;
  DISALLOW_COPY_AND_ASSIGN(MergedMathInstr);
};


class CheckClassInstr : public TemplateInstruction<1> {
 public:
  CheckClassInstr(Value* value,
                  intptr_t deopt_id,
                  const ICData& unary_checks,
                  intptr_t token_pos);

  DECLARE_INSTRUCTION(CheckClass)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return true; }

  virtual intptr_t token_pos() const { return token_pos_; }

  Value* value() const { return inputs_[0]; }

  const ICData& unary_checks() const { return unary_checks_; }

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  virtual void PrintOperandsTo(BufferFormatter* f) const;

  bool IsNullCheck() const;

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const;
  virtual bool AttributesEqual(Instruction* other) const;

  virtual bool MayThrow() const { return false; }

  void set_licm_hoisted(bool value) { licm_hoisted_ = value; }

 private:
  const ICData& unary_checks_;
  bool licm_hoisted_;
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(CheckClassInstr);
};


class CheckSmiInstr : public TemplateInstruction<1> {
 public:
  CheckSmiInstr(Value* value, intptr_t original_deopt_id, intptr_t token_pos)
      : token_pos_(token_pos) {
    ASSERT(original_deopt_id != Isolate::kNoDeoptId);
    SetInputAt(0, value);
    deopt_id_ = original_deopt_id;
  }

  Value* value() const { return inputs_[0]; }
  virtual intptr_t token_pos() const { return token_pos_; }

  DECLARE_INSTRUCTION(CheckSmi)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return true; }

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

 private:
  const intptr_t token_pos_;

  DISALLOW_COPY_AND_ASSIGN(CheckSmiInstr);
};


class CheckArrayBoundInstr : public TemplateInstruction<2> {
 public:
  CheckArrayBoundInstr(Value* length, Value* index, intptr_t deopt_id) {
    SetInputAt(kLengthPos, length);
    SetInputAt(kIndexPos, index);
    // Override generated deopt-id.
    deopt_id_ = deopt_id;
  }

  Value* length() const { return inputs_[kLengthPos]; }
  Value* index() const { return inputs_[kIndexPos]; }

  DECLARE_INSTRUCTION(CheckArrayBound)

  virtual intptr_t ArgumentCount() const { return 0; }

  virtual bool CanDeoptimize() const { return true; }

  bool IsRedundant(RangeBoundary length);

  virtual Instruction* Canonicalize(FlowGraph* flow_graph);

  // Returns the length offset for array and string types.
  static intptr_t LengthOffsetFor(intptr_t class_id);

  static bool IsFixedLengthArrayType(intptr_t class_id);

  virtual bool AllowsCSE() const { return true; }
  virtual EffectSet Effects() const { return EffectSet::None(); }
  virtual EffectSet Dependencies() const { return EffectSet::None(); }
  virtual bool AttributesEqual(Instruction* other) const { return true; }

  virtual bool MayThrow() const { return false; }

  // Give a name to the location/input indices.
  enum {
    kLengthPos = 0,
    kIndexPos = 1
  };

 private:
  DISALLOW_COPY_AND_ASSIGN(CheckArrayBoundInstr);
};


#undef DECLARE_INSTRUCTION

class Environment : public ZoneAllocated {
 public:
  // Iterate the non-NULL values in the innermost level of an environment.
  class ShallowIterator : public ValueObject {
   public:
    explicit ShallowIterator(Environment* environment)
        : environment_(environment), index_(0) { }

    ShallowIterator(const ShallowIterator& other)
        : ValueObject(),
          environment_(other.environment_),
          index_(other.index_) { }

    ShallowIterator& operator=(const ShallowIterator& other) {
      environment_ = other.environment_;
      index_ = other.index_;
      return *this;
    }

    Environment* environment() const { return environment_; }

    void Advance() {
      ASSERT(!Done());
      ++index_;
    }

    bool Done() const {
      return (environment_ == NULL) || (index_ >= environment_->Length());
    }

    Value* CurrentValue() const {
      ASSERT(!Done());
      ASSERT(environment_->values_[index_] != NULL);
      return environment_->values_[index_];
    }

    void SetCurrentValue(Value* value) {
      ASSERT(!Done());
      ASSERT(value != NULL);
      environment_->values_[index_] = value;
    }

    Location CurrentLocation() const {
      ASSERT(!Done());
      return environment_->locations_[index_];
    }

    void SetCurrentLocation(Location loc) {
      ASSERT(!Done());
      environment_->locations_[index_] = loc;
    }

   private:
    Environment* environment_;
    intptr_t index_;
  };

  // Iterate all non-NULL values in an environment, including outer
  // environments.  Note that the iterator skips empty environments.
  class DeepIterator : public ValueObject {
   public:
    explicit DeepIterator(Environment* environment) : iterator_(environment) {
      SkipDone();
    }

    void Advance() {
      ASSERT(!Done());
      iterator_.Advance();
      SkipDone();
    }

    bool Done() const { return iterator_.environment() == NULL; }

    Value* CurrentValue() const {
      ASSERT(!Done());
      return iterator_.CurrentValue();
    }

    void SetCurrentValue(Value* value) {
      ASSERT(!Done());
      iterator_.SetCurrentValue(value);
    }

    Location CurrentLocation() const {
      ASSERT(!Done());
      return iterator_.CurrentLocation();
    }

    void SetCurrentLocation(Location loc) {
      ASSERT(!Done());
      iterator_.SetCurrentLocation(loc);
    }

   private:
    void SkipDone() {
      while (!Done() && iterator_.Done()) {
        iterator_ = ShallowIterator(iterator_.environment()->outer());
      }
    }

    ShallowIterator iterator_;
  };

  // Construct an environment by constructing uses from an array of definitions.
  static Environment* From(Isolate* isolate,
                           const GrowableArray<Definition*>& definitions,
                           intptr_t fixed_parameter_count,
                           const ParsedFunction* parsed_function);

  void set_locations(Location* locations) {
    ASSERT(locations_ == NULL);
    locations_ = locations;
  }

  void set_deopt_id(intptr_t deopt_id) { deopt_id_ = deopt_id; }
  intptr_t deopt_id() const { return deopt_id_; }

  Environment* outer() const { return outer_; }

  Value* ValueAt(intptr_t ix) const {
    return values_[ix];
  }

  intptr_t Length() const {
    return values_.length();
  }

  Location LocationAt(intptr_t index) const {
    ASSERT((index >= 0) && (index < values_.length()));
    return locations_[index];
  }

  // The use index is the index in the flattened environment.
  Value* ValueAtUseIndex(intptr_t index) const {
    const Environment* env = this;
    while (index >= env->Length()) {
      ASSERT(env->outer_ != NULL);
      index -= env->Length();
      env = env->outer_;
    }
    return env->ValueAt(index);
  }

  intptr_t fixed_parameter_count() const {
    return fixed_parameter_count_;
  }

  const Code& code() const { return parsed_function_->code(); }

  Environment* DeepCopy(Isolate* isolate) const {
    return DeepCopy(isolate, Length());
  }

  void DeepCopyTo(Isolate* isolate, Instruction* instr) const;
  void DeepCopyToOuter(Isolate* isolate, Instruction* instr) const;

  void PrintTo(BufferFormatter* f) const;
  const char* ToCString() const;

  // Deep copy an environment.  The 'length' parameter may be less than the
  // environment's length in order to drop values (e.g., passed arguments)
  // from the copy.
  Environment* DeepCopy(Isolate* isolate, intptr_t length) const;

 private:
  friend class ShallowIterator;

  Environment(intptr_t length,
              intptr_t fixed_parameter_count,
              intptr_t deopt_id,
              const ParsedFunction* parsed_function,
              Environment* outer)
      : values_(length),
        locations_(NULL),
        fixed_parameter_count_(fixed_parameter_count),
        deopt_id_(deopt_id),
        parsed_function_(parsed_function),
        outer_(outer) { }


  GrowableArray<Value*> values_;
  Location* locations_;
  const intptr_t fixed_parameter_count_;
  intptr_t deopt_id_;
  const ParsedFunction* parsed_function_;
  Environment* outer_;

  DISALLOW_COPY_AND_ASSIGN(Environment);
};


// Visitor base class to visit each instruction and computation in a flow
// graph as defined by a reversed list of basic blocks.
class FlowGraphVisitor : public ValueObject {
 public:
  explicit FlowGraphVisitor(const GrowableArray<BlockEntryInstr*>& block_order)
      : block_order_(block_order), current_iterator_(NULL) { }
  virtual ~FlowGraphVisitor() { }

  ForwardInstructionIterator* current_iterator() const {
    return current_iterator_;
  }

  // Visit each block in the block order, and for each block its
  // instructions in order from the block entry to exit.
  virtual void VisitBlocks();

  // Visit functions for instruction classes, with an empty default
  // implementation.
#define DECLARE_VISIT_INSTRUCTION(ShortName)                                   \
  virtual void Visit##ShortName(ShortName##Instr* instr) { }

  FOR_EACH_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_INSTRUCTION

 protected:
  const GrowableArray<BlockEntryInstr*>& block_order_;
  ForwardInstructionIterator* current_iterator_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FlowGraphVisitor);
};


}  // namespace dart

#endif  // VM_INTERMEDIATE_LANGUAGE_H_
