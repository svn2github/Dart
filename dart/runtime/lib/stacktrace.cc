// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/bootstrap_natives.h"
#include "vm/exceptions.h"
#include "vm/object_store.h"
#include "vm/runtime_entry.h"
#include "vm/stack_frame.h"

namespace dart {

// Get a full stack trace.
// Arg0: stack trace object.
// Return value: String that represents the full stack trace.
DEFINE_NATIVE_ENTRY(Stacktrace_getFullStacktrace, 1) {
  const Stacktrace& trace =
      Stacktrace::CheckedHandle(arguments->NativeArgAt(0));
  return trace.FullStacktrace();
}


// Get a concise and pertinent stack trace.
// Arg0: stack trace object.
// Return value: String that represents the concise and pertinent stack trace.
DEFINE_NATIVE_ENTRY(Stacktrace_getStacktrace, 1) {
  const Stacktrace& trace =
      Stacktrace::CheckedHandle(arguments->NativeArgAt(0));
  return String::New(trace.ToCStringInternal(0));
}


static void iterateFrames(const GrowableObjectArray& func_list,
                          const GrowableObjectArray& code_list,
                          const GrowableObjectArray& pc_offset_list) {
  StackFrameIterator frames(StackFrameIterator::kDontValidateFrames);
  StackFrame* frame = frames.NextFrame();
  ASSERT(frame != NULL);  // We expect to find a dart invocation frame.
  Function& func = Function::Handle();
  Code& code = Code::Handle();
  Smi& offset = Smi::Handle();
  bool catch_frame_skipped = false;  // Tracks if catch frame has been skipped.
  while (frame != NULL) {
    if (frame->IsDartFrame()) {
      code = frame->LookupDartCode();
      if (code.is_optimized()) {
        // For optimized frames, extract all the inlined functions if any
        // into the stack trace.
        for (InlinedFunctionsIterator it(code, frame->pc());
              !it.Done(); it.Advance()) {
          func = it.function();
          code = it.code();
          uword pc = it.pc();
          ASSERT(pc != 0);
          ASSERT(code.EntryPoint() <= pc);
          ASSERT(pc < (code.EntryPoint() + code.Size()));
          if (func.is_visible()) {
            if (!catch_frame_skipped) {
              catch_frame_skipped = true;
            } else {
              offset = Smi::New(pc - code.EntryPoint());
              func_list.Add(func);
              code_list.Add(code);
              pc_offset_list.Add(offset);
            }
          }
        }
      } else {
        offset = Smi::New(frame->pc() - code.EntryPoint());
        func = code.function();
        if (func.is_visible()) {
          if (!catch_frame_skipped) {
            catch_frame_skipped = true;
          } else {
            func_list.Add(func);
            code_list.Add(code);
            pc_offset_list.Add(offset);
          }
        }
      }
    }
    frame = frames.NextFrame();
  }
}


// Setup a full stack trace.
// Arg0: stack trace object.
// Return value: None.
DEFINE_NATIVE_ENTRY(Stacktrace_setupFullStacktrace, 1) {
  const Stacktrace& trace =
      Stacktrace::CheckedHandle(arguments->NativeArgAt(0));
  const GrowableObjectArray& func_list =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  const GrowableObjectArray& code_list =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  const GrowableObjectArray& pc_offset_list =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  iterateFrames(func_list, code_list, pc_offset_list);
  const Array& func_array = Array::Handle(Array::MakeArray(func_list));
  const Array& code_array = Array::Handle(Array::MakeArray(code_list));
  const Array& pc_offset_array =
      Array::Handle(Array::MakeArray(pc_offset_list));
  trace.SetCatchStacktrace(func_array, code_array, pc_offset_array);
  return Object::null();
}


// An utility method for convenient printing of dart stack traces when
// inside 'gdb'. Note: This function will only work when there is a
// valid exit frame information. It will not work when a breakpoint is
// set in dart code and control is got inside 'gdb' without going through
// the runtime or native transition stub.
void _printCurrentStacktrace() {
  const GrowableObjectArray& func_list =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  const GrowableObjectArray& code_list =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  const GrowableObjectArray& pc_offset_list =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  iterateFrames(func_list, code_list, pc_offset_list);
  const Array& func_array = Array::Handle(Array::MakeArray(func_list));
  const Array& code_array = Array::Handle(Array::MakeArray(code_list));
  const Array& pc_offset_array =
      Array::Handle(Array::MakeArray(pc_offset_list));
  const Stacktrace& stacktrace = Stacktrace::Handle(
      Stacktrace::New(func_array, code_array, pc_offset_array));
  OS::Print("%s\n", stacktrace.ToCString());
}

}  // namespace dart
