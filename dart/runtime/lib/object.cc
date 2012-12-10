// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/bootstrap_natives.h"

#include "vm/exceptions.h"
#include "vm/native_entry.h"
#include "vm/object.h"

namespace dart {

DEFINE_NATIVE_ENTRY(Object_toString, 1) {
  const Instance& instance = Instance::CheckedHandle(arguments->NativeArgAt(0));
  const char* c_str = instance.ToCString();
  return String::New(c_str);
}


DEFINE_NATIVE_ENTRY(Object_noSuchMethod, 3) {
  const Instance& instance =
      Instance::CheckedHandle(arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(
      String, function_name, arguments->NativeArgAt(1));
  GET_NON_NULL_NATIVE_ARGUMENT(Array, func_args, arguments->NativeArgAt(2));
  const Object& null_object = Object::Handle(Object::null());
  GrowableArray<const Object*> dart_arguments(4);
  dart_arguments.Add(&instance);
  dart_arguments.Add(&function_name);
  dart_arguments.Add(&func_args);
  dart_arguments.Add(&null_object);

  // Report if a function with same name (but different arguments) has been
  // found.
  Class& instance_class = Class::Handle(instance.clazz());
  Function& function =
      Function::Handle(instance_class.LookupDynamicFunction(function_name));
  while (function.IsNull()) {
    instance_class = instance_class.SuperClass();
    if (instance_class.IsNull()) break;
    function = instance_class.LookupDynamicFunction(function_name);
  }
  if (!function.IsNull()) {
    const int total_num_parameters = function.NumParameters();
    const Array& array = Array::Handle(Array::New(total_num_parameters - 1));
    // Skip receiver.
    for (int i = 1; i < total_num_parameters; i++) {
      array.SetAt(i - 1, String::Handle(function.ParameterNameAt(i)));
    }
    dart_arguments.Add(&array);
  }
  Exceptions::ThrowByType(Exceptions::kNoSuchMethod, dart_arguments);
  return Object::null();
}


DEFINE_NATIVE_ENTRY(Object_runtimeType, 1) {
  const Instance& instance = Instance::CheckedHandle(arguments->NativeArgAt(0));
  const Type& type = Type::Handle(instance.GetType());
  return type.Canonicalize();
}


DEFINE_NATIVE_ENTRY(AbstractType_toString, 1) {
  const AbstractType& type =
      AbstractType::CheckedHandle(arguments->NativeArgAt(0));
  return type.Name();
}

}  // namespace dart
