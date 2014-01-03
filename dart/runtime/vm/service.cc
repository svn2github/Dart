// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/service.h"

#include "vm/cpu.h"
#include "vm/dart_entry.h"
#include "vm/debugger.h"
#include "vm/heap_histogram.h"
#include "vm/isolate.h"
#include "vm/message.h"
#include "vm/object.h"
#include "vm/object_id_ring.h"
#include "vm/object_store.h"
#include "vm/port.h"
#include "vm/profiler.h"

namespace dart {

typedef void (*ServiceMessageHandler)(Isolate* isolate, JSONStream* stream);

struct ServiceMessageHandlerEntry {
  const char* command;
  ServiceMessageHandler handler;
};

static ServiceMessageHandler FindServiceMessageHandler(const char* command);


static uint8_t* allocator(uint8_t* ptr, intptr_t old_size, intptr_t new_size) {
  void* new_ptr = realloc(reinterpret_cast<void*>(ptr), new_size);
  return reinterpret_cast<uint8_t*>(new_ptr);
}


static void PostReply(const String& reply, const Instance& reply_port) {
  const Object& id_obj = Object::Handle(
      DartLibraryCalls::PortGetId(reply_port));
  if (id_obj.IsError()) {
    Exceptions::PropagateError(Error::Cast(id_obj));
  }
  const Integer& id = Integer::Cast(id_obj);
  Dart_Port port = static_cast<Dart_Port>(id.AsInt64Value());
  ASSERT(port != ILLEGAL_PORT);

  uint8_t* data = NULL;
  MessageWriter writer(&data, &allocator);
  writer.WriteMessage(reply);
  PortMap::PostMessage(new Message(port, data,
                                   writer.BytesWritten(),
                                   Message::kNormalPriority));
}


void Service::HandleServiceMessage(Isolate* isolate, const Instance& msg) {
  ASSERT(isolate != NULL);
  ASSERT(!msg.IsNull());
  ASSERT(msg.IsGrowableObjectArray());

  {
    StackZone zone(isolate);
    HANDLESCOPE(isolate);

    const GrowableObjectArray& message = GrowableObjectArray::Cast(msg);
    // Message is a list with three entries.
    ASSERT(message.Length() == 4);

    Instance& reply_port = Instance::Handle(isolate);
    GrowableObjectArray& path = GrowableObjectArray::Handle(isolate);
    GrowableObjectArray& option_keys = GrowableObjectArray::Handle(isolate);
    GrowableObjectArray& option_values = GrowableObjectArray::Handle(isolate);
    reply_port ^= message.At(0);
    path ^= message.At(1);
    option_keys ^= message.At(2);
    option_values ^= message.At(3);

    ASSERT(!path.IsNull());
    ASSERT(!option_keys.IsNull());
    ASSERT(!option_values.IsNull());
    // Path always has at least one entry in it.
    ASSERT(path.Length() > 0);
    // Same number of option keys as values.
    ASSERT(option_keys.Length() == option_values.Length());

    String& pathSegment = String::Handle();
    pathSegment ^= path.At(0);
    ASSERT(!pathSegment.IsNull());

    ServiceMessageHandler handler =
        FindServiceMessageHandler(pathSegment.ToCString());
    ASSERT(handler != NULL);
    {
      JSONStream js;

      // Setup JSONStream arguments and options. The arguments and options
      // are zone allocated and will be freed immediately after handling the
      // message.
      Zone* zoneAllocator = zone.GetZone();
      const char** arguments = zoneAllocator->Alloc<const char*>(path.Length());
      String& string_iterator = String::Handle();
      for (intptr_t i = 0; i < path.Length(); i++) {
        string_iterator ^= path.At(i);
        arguments[i] =
            zoneAllocator->MakeCopyOfString(string_iterator.ToCString());
      }
      js.SetArguments(arguments, path.Length());
      if (option_keys.Length() > 0) {
        const char** option_keys_native =
            zoneAllocator->Alloc<const char*>(option_keys.Length());
        const char** option_values_native =
            zoneAllocator->Alloc<const char*>(option_keys.Length());
        for (intptr_t i = 0; i < option_keys.Length(); i++) {
          string_iterator ^= option_keys.At(i);
          option_keys_native[i] =
              zoneAllocator->MakeCopyOfString(string_iterator.ToCString());
          string_iterator ^= option_values.At(i);
          option_values_native[i] =
              zoneAllocator->MakeCopyOfString(string_iterator.ToCString());
        }
        js.SetOptions(option_keys_native, option_values_native,
                      option_keys.Length());
      }

      handler(isolate, &js);
      const String& reply = String::Handle(String::New(js.ToCString()));
      ASSERT(!reply.IsNull());
      PostReply(reply, reply_port);
    }
  }
}


static void PrintArgumentsAndOptions(const JSONObject& obj, JSONStream* js) {
  JSONObject jsobj(&obj, "message");
  {
    JSONArray jsarr(&jsobj, "arguments");
    for (intptr_t i = 0; i < js->num_arguments(); i++) {
      jsarr.AddValue(js->GetArgument(i));
    }
  }
  {
    JSONArray jsarr(&jsobj, "option_keys");
    for (intptr_t i = 0; i < js->num_options(); i++) {
      jsarr.AddValue(js->GetOptionKey(i));
    }
  }
  {
    JSONArray jsarr(&jsobj, "option_values");
    for (intptr_t i = 0; i < js->num_options(); i++) {
      jsarr.AddValue(js->GetOptionValue(i));
    }
  }
}


static void PrintGenericError(JSONStream* js) {
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "Error");
  jsobj.AddProperty("text", "Invalid request.");
  PrintArgumentsAndOptions(jsobj, js);
}


static void PrintError(JSONStream* js, const char* format, ...) {
  Isolate* isolate = Isolate::Current();

  va_list args;
  va_start(args, format);
  intptr_t len = OS::VSNPrint(NULL, 0, format, args);
  va_end(args);

  char* buffer = isolate->current_zone()->Alloc<char>(len + 1);
  va_list args2;
  va_start(args2, format);
  OS::VSNPrint(buffer, (len + 1), format, args2);
  va_end(args2);

  JSONObject jsobj(js);
  jsobj.AddProperty("type", "Error");
  jsobj.AddProperty("text", buffer);
  PrintArgumentsAndOptions(jsobj, js);
}


static void HandleName(Isolate* isolate, JSONStream* js) {
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "IsolateName");
  jsobj.AddProperty("id", static_cast<intptr_t>(isolate->main_port()));
  jsobj.AddProperty("name", isolate->name());
}


static void HandleStackTrace(Isolate* isolate, JSONStream* js) {
  DebuggerStackTrace* stack = isolate->debugger()->StackTrace();
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "StackTrace");
  JSONArray jsarr(&jsobj, "members");
  intptr_t n_frames = stack->Length();
  String& function = String::Handle();
  Script& script = Script::Handle();
  for (int i = 0; i < n_frames; i++) {
    ActivationFrame* frame = stack->FrameAt(i);
    script ^= frame->SourceScript();
    function ^= frame->function().UserVisibleName();
    JSONObject jsobj(&jsarr);
    jsobj.AddProperty("name", function.ToCString());
    jsobj.AddProperty("script", script);
    jsobj.AddProperty("line", frame->LineNumber());
    jsobj.AddProperty("function", frame->function());
    jsobj.AddProperty("code", frame->code());
  }
}


static void HandleObjectHistogram(Isolate* isolate, JSONStream* js) {
  ObjectHistogram* histogram = Isolate::Current()->object_histogram();
  if (histogram == NULL) {
    JSONObject jsobj(js);
    jsobj.AddProperty("type", "Error");
    jsobj.AddProperty("text", "Run with --print_object_histogram");
    return;
  }
  histogram->PrintToJSONStream(js);
}


static void HandleEcho(Isolate* isolate, JSONStream* js) {
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "message");
  PrintArgumentsAndOptions(jsobj, js);
}


// Print an error message if there is no ID argument.
#define REQUIRE_COLLECTION_ID(collection)                                     \
  if (js->num_arguments() == 1) {                                             \
    PrintError(js, "Must specify collection object id: /%s/id", collection);  \
    return;                                                                   \
  }


#define CHECK_COLLECTION_ID_BOUNDS(collection, length, arg, id, js)            \
  if (!GetIntegerId(arg, &id)) {                                               \
    PrintError(js, "Must specify collection object id: %s/id", collection);    \
    return;                                                                    \
  }                                                                            \
  if ((id < 0) || (id >= length)) {                                            \
    PrintError(js, "%s id (%" Pd ") must be in [0, %" Pd ").", collection, id, \
                                                               length);        \
    return;                                                                    \
  }


static bool GetIntegerId(const char* s, intptr_t* id, int base = 10) {
  if ((s == NULL) || (*s == '\0')) {
    // Empty string.
    return false;
  }
  if (id == NULL) {
    // No id pointer.
    return false;
  }
  intptr_t r = 0;
  char* end_ptr = NULL;
  r = strtol(s, &end_ptr, base);
  if (end_ptr == s) {
    // String was not advanced at all, cannot be valid.
    return false;
  }
  *id = r;
  return true;
}


static bool GetUnsignedIntegerId(const char* s, uintptr_t* id, int base = 10) {
  if ((s == NULL) || (*s == '\0')) {
    // Empty string.
    return false;
  }
  if (id == NULL) {
    // No id pointer.
    return false;
  }
  uintptr_t r = 0;
  char* end_ptr = NULL;
  r = strtoul(s, &end_ptr, base);
  if (end_ptr == s) {
    // String was not advanced at all, cannot be valid.
    return false;
  }
  *id = r;
  return true;
}


static void HandleClassesClosures(Isolate* isolate, const Class& cls,
                                  JSONStream* js) {
  intptr_t id;
  if (js->num_arguments() > 4) {
    PrintError(js, "Command too long");
    return;
  }
  if (!GetIntegerId(js->GetArgument(3), &id)) {
    PrintError(js, "Must specify collection object id: closures/id");
    return;
  }
  Function& func = Function::Handle();
  func ^= cls.ClosureFunctionFromIndex(id);
  if (func.IsNull()) {
    PrintError(js, "Closure function %" Pd " not found", id);
    return;
  }
  func.PrintToJSONStream(js, false);
}


static void HandleClassesDispatchers(Isolate* isolate, const Class& cls,
                                     JSONStream* js) {
  intptr_t id;
  if (js->num_arguments() > 4) {
    PrintError(js, "Command too long");
    return;
  }
  if (!GetIntegerId(js->GetArgument(3), &id)) {
    PrintError(js, "Must specify collection object id: dispatchers/id");
    return;
  }
  Function& func = Function::Handle();
  func ^= cls.InvocationDispatcherFunctionFromIndex(id);
  if (func.IsNull()) {
    PrintError(js, "Dispatcher %" Pd " not found", id);
    return;
  }
  func.PrintToJSONStream(js, false);
}


static void HandleClassesFunctions(Isolate* isolate, const Class& cls,
                                   JSONStream* js) {
  intptr_t id;
  if (js->num_arguments() > 4) {
    PrintError(js, "Command too long");
    return;
  }
  if (!GetIntegerId(js->GetArgument(3), &id)) {
    PrintError(js, "Must specify collection object id: functions/id");
    return;
  }
  Function& func = Function::Handle();
  func ^= cls.FunctionFromIndex(id);
  if (func.IsNull()) {
    PrintError(js, "Function %" Pd " not found", id);
    return;
  }
  func.PrintToJSONStream(js, false);
}


static void HandleClassesImplicitClosures(Isolate* isolate, const Class& cls,
                                          JSONStream* js) {
  intptr_t id;
  if (js->num_arguments() > 4) {
    PrintError(js, "Command too long");
    return;
  }
  if (!GetIntegerId(js->GetArgument(3), &id)) {
    PrintError(js, "Must specify collection object id: implicit_closures/id");
    return;
  }
  Function& func = Function::Handle();
  func ^= cls.ImplicitClosureFunctionFromIndex(id);
  if (func.IsNull()) {
    PrintError(js, "Implicit closure function %" Pd " not found", id);
    return;
  }
  func.PrintToJSONStream(js, false);
}


static void HandleClassesFields(Isolate* isolate, const Class& cls,
                                JSONStream* js) {
  intptr_t id;
  if (js->num_arguments() > 4) {
    PrintError(js, "Command too long");
    return;
  }
  if (!GetIntegerId(js->GetArgument(3), &id)) {
    PrintError(js, "Must specify collection object id: fields/id");
    return;
  }
  Field& field = Field::Handle(cls.FieldFromIndex(id));
  if (field.IsNull()) {
    PrintError(js, "Field %" Pd " not found", id);
    return;
  }
  field.PrintToJSONStream(js, false);
}


static void HandleClasses(Isolate* isolate, JSONStream* js) {
  if (js->num_arguments() == 1) {
    ClassTable* table = isolate->class_table();
    table->PrintToJSONStream(js);
    return;
  }
  ASSERT(js->num_arguments() >= 2);
  intptr_t id;
  if (!GetIntegerId(js->GetArgument(1), &id)) {
    PrintError(js, "Must specify collection object id: /classes/id");
    return;
  }
  ClassTable* table = isolate->class_table();
  if (!table->IsValidIndex(id)) {
    PrintError(js, "%" Pd " is not a valid class id.", id);;
    return;
  }
  Class& cls = Class::Handle(table->At(id));
  if (js->num_arguments() == 2) {
    cls.PrintToJSONStream(js, false);
    return;
  } else if (js->num_arguments() >= 3) {
    const char* second = js->GetArgument(2);
    if (!strcmp(second, "closures")) {
      HandleClassesClosures(isolate, cls, js);
    } else if (!strcmp(second, "fields")) {
      HandleClassesFields(isolate, cls, js);
    } else if (!strcmp(second, "functions")) {
      HandleClassesFunctions(isolate, cls, js);
    } else if (!strcmp(second, "implicit_closures")) {
      HandleClassesImplicitClosures(isolate, cls, js);
    } else if (!strcmp(second, "dispatchers")) {
      HandleClassesDispatchers(isolate, cls, js);
    } else {
      PrintError(js, "Invalid sub collection %s", second);
    }
    return;
  }
  UNREACHABLE();
}


static void HandleLibrary(Isolate* isolate, JSONStream* js) {
  if (js->num_arguments() == 1) {
    const Library& lib =
        Library::Handle(isolate->object_store()->root_library());
    lib.PrintToJSONStream(js, false);
    return;
  }
  PrintGenericError(js);
}


static void HandleLibraries(Isolate* isolate, JSONStream* js) {
  // TODO(johnmccutchan): Support fields and functions on libraries.
  REQUIRE_COLLECTION_ID("libraries");
  const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(isolate->object_store()->libraries());
  ASSERT(!libs.IsNull());
  intptr_t id = 0;
  CHECK_COLLECTION_ID_BOUNDS("libraries", libs.Length(), js->GetArgument(1),
                             id, js);
  Library& lib = Library::Handle();
  lib ^= libs.At(id);
  ASSERT(!lib.IsNull());
  lib.PrintToJSONStream(js, false);
}


static void HandleObjects(Isolate* isolate, JSONStream* js) {
  REQUIRE_COLLECTION_ID("objects");
  ASSERT(js->num_arguments() >= 2);
  ObjectIdRing* ring = isolate->object_id_ring();
  ASSERT(ring != NULL);
  intptr_t id = -1;
  if (!GetIntegerId(js->GetArgument(1), &id)) {
    Object::null_object().PrintToJSONStream(js, false);
    return;
  }
  Object& obj = Object::Handle(ring->GetObjectForId(id));
  obj.PrintToJSONStream(js, false);
}



static void HandleScriptsEnumerate(Isolate* isolate, JSONStream* js) {
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "ScriptList");
  {
    JSONArray members(&jsobj, "members");
    const GrowableObjectArray& libs =
      GrowableObjectArray::Handle(isolate->object_store()->libraries());
    int num_libs = libs.Length();
    Library &lib = Library::Handle();
    Script& script = Script::Handle();
    for (intptr_t i = 0; i < num_libs; i++) {
      lib ^= libs.At(i);
      ASSERT(!lib.IsNull());
      ASSERT(Smi::IsValid(lib.index()));
      const Array& loaded_scripts = Array::Handle(lib.LoadedScripts());
      ASSERT(!loaded_scripts.IsNull());
      intptr_t num_scripts = loaded_scripts.Length();
      for (intptr_t i = 0; i < num_scripts; i++) {
        script ^= loaded_scripts.At(i);
        members.AddValue(script);
      }
    }
  }
}


static void HandleScriptsFetch(Isolate* isolate, JSONStream* js) {
  const GrowableObjectArray& libs =
    GrowableObjectArray::Handle(isolate->object_store()->libraries());
  int num_libs = libs.Length();
  Library &lib = Library::Handle();
  Script& script = Script::Handle();
  String& url = String::Handle();
  const String& id = String::Handle(String::New(js->GetArgument(1)));
  ASSERT(!id.IsNull());
  // The id is the url of the script % encoded, decode it.
  String& requested_url = String::Handle(String::DecodeURI(id));
  for (intptr_t i = 0; i < num_libs; i++) {
    lib ^= libs.At(i);
    ASSERT(!lib.IsNull());
    ASSERT(Smi::IsValid(lib.index()));
    const Array& loaded_scripts = Array::Handle(lib.LoadedScripts());
    ASSERT(!loaded_scripts.IsNull());
    intptr_t num_scripts = loaded_scripts.Length();
    for (intptr_t i = 0; i < num_scripts; i++) {
      script ^= loaded_scripts.At(i);
      ASSERT(!script.IsNull());
      url ^= script.url();
      if (url.Equals(requested_url)) {
        script.PrintToJSONStream(js, false);
        return;
      }
    }
  }
  PrintError(js, "Cannot find script %s\n", requested_url.ToCString());
}


static void HandleScripts(Isolate* isolate, JSONStream* js) {
  if (js->num_arguments() == 1) {
    // Enumerate all scripts.
    HandleScriptsEnumerate(isolate, js);
  } else if (js->num_arguments() == 2) {
    // Fetch specific script.
    HandleScriptsFetch(isolate, js);
  } else {
    PrintError(js, "Command too long");
  }
}


static void HandleDebug(Isolate* isolate, JSONStream* js) {
  if (js->num_arguments() == 1) {
    PrintError(js, "Must specify a subcommand");
    return;
  }
  const char* command = js->GetArgument(1);
  if (!strcmp(command, "breakpoints")) {
    if (js->num_arguments() == 2) {
      // Print breakpoint list.
      JSONObject jsobj(js);
      jsobj.AddProperty("type", "BreakpointList");
      JSONArray jsarr(&jsobj, "breakpoints");
      isolate->debugger()->PrintBreakpointsToJSONArray(&jsarr);

    } else if (js->num_arguments() == 3) {
      // Print individual breakpoint.
      intptr_t id = 0;
      SourceBreakpoint* bpt = NULL;
      if (GetIntegerId(js->GetArgument(2), &id)) {
        bpt = isolate->debugger()->GetBreakpointById(id);
      }
      if (bpt != NULL) {
        bpt->PrintToJSONStream(js);
      } else {
        PrintError(js, "Unrecognized breakpoint id %s", js->GetArgument(2));
      }

    } else {
      PrintError(js, "Command too long");
    }
  } else {
    PrintError(js, "Unrecognized subcommand '%s'", js->GetArgument(1));
  }
}


static void HandleCpu(Isolate* isolate, JSONStream* js) {
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "CPU");
  jsobj.AddProperty("architecture", CPU::Id());
}


static void HandleCode(Isolate* isolate, JSONStream* js) {
  REQUIRE_COLLECTION_ID("code");
  uintptr_t pc;
  if (!GetUnsignedIntegerId(js->GetArgument(1), &pc, 16)) {
    PrintError(js, "Must specify code address: code/c0deadd0.");
    return;
  }
  Code& code = Code::Handle(Code::LookupCode(pc));
  if (code.IsNull()) {
    PrintError(js, "Could not find code at %" Px "", pc);
    return;
  }
  code.PrintToJSONStream(js, false);
}


static void HandleProfile(Isolate* isolate, JSONStream* js) {
  Profiler::PrintToJSONStream(isolate, js, true);
}


static ServiceMessageHandlerEntry __message_handlers[] = {
  { "_echo", HandleEcho },
  { "classes", HandleClasses },
  { "code", HandleCode },
  { "cpu", HandleCpu },
  { "debug", HandleDebug },
  { "libraries", HandleLibraries },
  { "library", HandleLibrary },
  { "name", HandleName },
  { "objecthistogram", HandleObjectHistogram},
  { "objects", HandleObjects },
  { "profile", HandleProfile },
  { "scripts", HandleScripts },
  { "stacktrace", HandleStackTrace },
};


static void HandleFallthrough(Isolate* isolate, JSONStream* js) {
  JSONObject jsobj(js);
  jsobj.AddProperty("type", "Error");
  jsobj.AddProperty("text", "request not understood.");
  PrintArgumentsAndOptions(jsobj, js);
}


static ServiceMessageHandler FindServiceMessageHandler(const char* command) {
  intptr_t num_message_handlers = sizeof(__message_handlers) /
                                  sizeof(__message_handlers[0]);
  for (intptr_t i = 0; i < num_message_handlers; i++) {
    const ServiceMessageHandlerEntry& entry = __message_handlers[i];
    if (!strcmp(command, entry.command)) {
      return entry.handler;
    }
  }
  return HandleFallthrough;
}

}  // namespace dart
