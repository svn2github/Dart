// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/thread.h"
#include "vm/globals.h"
#include "vm/signal_handler.h"
#if defined(TARGET_OS_ANDROID)

namespace dart {

uintptr_t SignalHandler::GetProgramCounter(const mcontext_t& mcontext) {
  UNIMPLEMENTED();
}


uintptr_t SignalHandler::GetFramePointer(const mcontext_t& mcontext) {
  UNIMPLEMENTED();
}


uintptr_t SignalHandler::GetStackPointer(const mcontext_t& mcontext) {
  UNIMPLEMENTED();
}


void SignalHandler::Install(SignalAction action) {
  UNIMPLEMENTED();
}


}  // namespace dart

#endif  // defined(TARGET_OS_ANDROID)
