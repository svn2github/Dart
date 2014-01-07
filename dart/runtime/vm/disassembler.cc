// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/disassembler.h"

#include "vm/assembler.h"
#include "vm/globals.h"
#include "vm/os.h"
#include "vm/json_stream.h"

namespace dart {

void DisassembleToStdout::ConsumeInstruction(char* hex_buffer,
                                             intptr_t hex_size,
                                             char* human_buffer,
                                             intptr_t human_size,
                                             uword pc) {
  static const int kHexColumnWidth = 23;
  uint8_t* pc_ptr = reinterpret_cast<uint8_t*>(pc);
  OS::Print("%p    %s", pc_ptr, hex_buffer);
  int hex_length = strlen(hex_buffer);
  if (hex_length < kHexColumnWidth) {
    for (int i = kHexColumnWidth - hex_length; i > 0; i--) {
      OS::Print(" ");
    }
  }
  OS::Print("%s", human_buffer);
  OS::Print("\n");
}


void DisassembleToStdout::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  OS::VFPrint(stdout, format, args);
  va_end(args);
}


void DisassembleToJSONStream::ConsumeInstruction(char* hex_buffer,
                                                 intptr_t hex_size,
                                                 char* human_buffer,
                                                 intptr_t human_size,
                                                 uword pc) {
  uint8_t* pc_ptr = reinterpret_cast<uint8_t*>(pc);
  JSONObject jsobj(&jsarr_);
  jsobj.AddProperty("type", "DisassembledInstruction");
  jsobj.AddPropertyF("pc", "%p", pc_ptr);
  jsobj.AddProperty("hex", hex_buffer);
  jsobj.AddProperty("human", human_buffer);
}


void DisassembleToJSONStream::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  intptr_t len = OS::VSNPrint(NULL, 0, format, args);
  va_end(args);
  char* p = reinterpret_cast<char*>(malloc(len+1));
  va_start(args, format);
  intptr_t len2 = OS::VSNPrint(p, len, format, args);
  va_end(args);
  ASSERT(len == len2);
  for (intptr_t i = 0; i < len; i++) {
    if (p[i] == '\n' || p[i] == '\r') {
      p[i] = ' ';
    }
  }
  JSONObject jsobj(&jsarr_);
  jsobj.AddProperty("type", "DisassembledInstructionComment");
  jsobj.AddProperty("comment", p);
  free(p);
}


class FindAddrVisitor : public FindObjectVisitor {
   public:
    explicit FindAddrVisitor(uword addr)
        : FindObjectVisitor(Isolate::Current()), addr_(addr) { }
    virtual ~FindAddrVisitor() { }

    virtual uword filter_addr() const { return addr_; }

    // Check if object matches find condition.
    virtual bool FindObject(RawObject* obj) const {
      return obj == reinterpret_cast<RawObject*>(addr_);
    }

   private:
    const uword addr_;

    DISALLOW_COPY_AND_ASSIGN(FindAddrVisitor);
};


bool Disassembler::CanFindOldObject(uword addr) {
  FindAddrVisitor visitor(addr);
  return Isolate::Current()->heap()->FindOldObject(&visitor) != Object::null();
}

}  // namespace dart
