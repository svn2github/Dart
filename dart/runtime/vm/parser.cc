// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/parser.h"

#include "lib/invocation_mirror.h"
#include "vm/bigint_operations.h"
#include "vm/bootstrap.h"
#include "vm/class_finalizer.h"
#include "vm/compiler.h"
#include "vm/compiler_stats.h"
#include "vm/dart_api_impl.h"
#include "vm/dart_entry.h"
#include "vm/flags.h"
#include "vm/growable_array.h"
#include "vm/longjump.h"
#include "vm/native_entry.h"
#include "vm/object.h"
#include "vm/object_store.h"
#include "vm/resolver.h"
#include "vm/scopes.h"
#include "vm/stack_frame.h"
#include "vm/symbols.h"

namespace dart {

DEFINE_FLAG(bool, enable_asserts, false, "Enable assert statements.");
DEFINE_FLAG(bool, enable_type_checks, false, "Enable type checks.");
DEFINE_FLAG(bool, trace_parser, false, "Trace parser operations.");
DEFINE_FLAG(bool, warning_as_error, false, "Treat warnings as errors.");
DEFINE_FLAG(bool, silent_warnings, false, "Silence warnings.");
DECLARE_FLAG(bool, error_on_bad_type);
DECLARE_FLAG(bool, throw_on_javascript_int_overflow);

static void CheckedModeHandler(bool value) {
  FLAG_enable_asserts = value;
  FLAG_enable_type_checks = value;
}

// --enable-checked-mode and --checked both enable checked mode which is
// equivalent to setting --enable-asserts and --enable-type-checks.
DEFINE_FLAG_HANDLER(CheckedModeHandler,
                    enable_checked_mode,
                    "Enable checked mode.");

DEFINE_FLAG_HANDLER(CheckedModeHandler,
                    checked,
                    "Enable checked mode.");

#if defined(DEBUG)
class TraceParser : public ValueObject {
 public:
  TraceParser(intptr_t token_pos, const Script& script, const char* msg) {
    if (FLAG_trace_parser) {
      // Skips tracing of bootstrap libraries.
      if (script.HasSource()) {
        intptr_t line, column;
        script.GetTokenLocation(token_pos, &line, &column);
        PrintIndent();
        OS::Print("%s (line %" Pd ", col %" Pd ", token %" Pd ")\n",
                  msg, line, column, token_pos);
      }
      indent_++;
    }
  }
  ~TraceParser() { indent_--; }
 private:
  void PrintIndent() {
    for (int i = 0; i < indent_; i++) { OS::Print(". "); }
  }
  static int indent_;
};

int TraceParser::indent_ = 0;

#define TRACE_PARSER(s) \
  TraceParser __p__(this->TokenPos(), this->script_, s)

#else  // not DEBUG
#define TRACE_PARSER(s)
#endif  // DEBUG


static RawTypeArguments* NewTypeArguments(const GrowableObjectArray& objs) {
  const TypeArguments& a =
      TypeArguments::Handle(TypeArguments::New(objs.Length()));
  AbstractType& type = AbstractType::Handle();
  for (int i = 0; i < objs.Length(); i++) {
    type ^= objs.At(i);
    a.SetTypeAt(i, type);
  }
  // Cannot canonicalize TypeArgument yet as its types may not have been
  // finalized yet.
  return a.raw();
}


static ThrowNode* GenerateRethrow(intptr_t token_pos, const Object& obj) {
  const UnhandledException& excp = UnhandledException::Cast(obj);
  Instance& exception = Instance::ZoneHandle(excp.exception());
  if (exception.IsNew()) {
    exception ^= Object::Clone(exception, Heap::kOld);
  }
  Instance& stack_trace = Instance::ZoneHandle(excp.stacktrace());
  if (stack_trace.IsNew()) {
    stack_trace ^= Object::Clone(stack_trace, Heap::kOld);
  }
  return new ThrowNode(token_pos,
                       new LiteralNode(token_pos, exception),
                       new LiteralNode(token_pos, stack_trace));
}


LocalVariable* ParsedFunction::EnsureExpressionTemp() {
  if (!has_expression_temp_var()) {
    LocalVariable* temp =
        new LocalVariable(function_.token_pos(),
                          Symbols::ExprTemp(),
                          Type::ZoneHandle(Type::DynamicType()));
    ASSERT(temp != NULL);
    set_expression_temp_var(temp);
  }
  ASSERT(has_expression_temp_var());
  return expression_temp_var();
}


void ParsedFunction::SetNodeSequence(SequenceNode* node_sequence) {
  ASSERT(node_sequence_ == NULL);
  ASSERT(node_sequence != NULL);
  node_sequence_ = node_sequence;
}


void ParsedFunction::AllocateVariables() {
  LocalScope* scope = node_sequence()->scope();
  const intptr_t num_fixed_params = function().num_fixed_parameters();
  const intptr_t num_opt_params = function().NumOptionalParameters();
  const intptr_t num_params = num_fixed_params + num_opt_params;
  // Compute start indices to parameters and locals, and the number of
  // parameters to copy.
  if (num_opt_params == 0) {
    // Parameter i will be at fp[kParamEndSlotFromFp + num_params - i] and
    // local variable j will be at fp[kFirstLocalSlotFromFp - j].
    first_parameter_index_ = kParamEndSlotFromFp + num_params;
    first_stack_local_index_ = kFirstLocalSlotFromFp;
    num_copied_params_ = 0;
  } else {
    // Parameter i will be at fp[kFirstLocalSlotFromFp - i] and local variable
    // j will be at fp[kFirstLocalSlotFromFp - num_params - j].
    first_parameter_index_ = kFirstLocalSlotFromFp;
    first_stack_local_index_ = first_parameter_index_ - num_params;
    num_copied_params_ = num_params;
  }

  // Allocate parameters and local variables, either in the local frame or
  // in the context(s).
  LocalScope* context_owner = NULL;  // No context needed yet.
  int next_free_frame_index =
      scope->AllocateVariables(first_parameter_index_,
                               num_params,
                               first_stack_local_index_,
                               scope,
                               &context_owner);

  // If this function allocates context variables, but none of its enclosing
  // functions do, the context on entry is not linked as parent of the allocated
  // context but saved on entry and restored on exit as to prevent memory leaks.
  // Add and allocate a local variable to this purpose.
  if (context_owner != NULL) {
    const ContextScope& context_scope =
        ContextScope::Handle(function().context_scope());
    if (context_scope.IsNull() || (context_scope.num_variables() == 0)) {
      LocalVariable* context_var =
          new LocalVariable(function().token_pos(),
                            Symbols::SavedEntryContextVar(),
                            Type::ZoneHandle(Type::DynamicType()));
      context_var->set_index(next_free_frame_index--);
      scope->AddVariable(context_var);
      set_saved_entry_context_var(context_var);
    }
  }

  // Frame indices are relative to the frame pointer and are decreasing.
  ASSERT(next_free_frame_index <= first_stack_local_index_);
  num_stack_locals_ = first_stack_local_index_ - next_free_frame_index;
}


struct Parser::Block : public ZoneAllocated {
  Block(Block* outer_block, LocalScope* local_scope, SequenceNode* seq)
    : parent(outer_block), scope(local_scope), statements(seq) {
    ASSERT(scope != NULL);
    ASSERT(statements != NULL);
  }
  Block* parent;  // Enclosing block, or NULL if outermost.
  LocalScope* scope;
  SequenceNode* statements;
};


// Class which describes an inlined finally block which is used to generate
// inlined code for the finally blocks when there is an exit from a try
// block using 'return', 'break' or 'continue'.
class Parser::TryBlocks : public ZoneAllocated {
 public:
  TryBlocks(Block* try_block, TryBlocks* outer_try_block, intptr_t try_index)
      : try_block_(try_block),
        inlined_finally_nodes_(),
        outer_try_block_(outer_try_block),
        try_index_(try_index) { }

  TryBlocks* outer_try_block() const { return outer_try_block_; }
  Block* try_block() const { return try_block_; }
  intptr_t try_index() const { return try_index_; }

  void AddNodeForFinallyInlining(AstNode* node);
  AstNode* GetNodeToInlineFinally(int index) {
    if (0 <= index && index < inlined_finally_nodes_.length()) {
      return inlined_finally_nodes_[index];
    }
    return NULL;
  }

 private:
  Block* try_block_;
  GrowableArray<AstNode*> inlined_finally_nodes_;
  TryBlocks* outer_try_block_;
  const intptr_t try_index_;

  DISALLOW_COPY_AND_ASSIGN(TryBlocks);
};


void Parser::TryBlocks::AddNodeForFinallyInlining(AstNode* node) {
  inlined_finally_nodes_.Add(node);
}


// For parsing a compilation unit.
Parser::Parser(const Script& script, const Library& library, intptr_t token_pos)
    : isolate_(Isolate::Current()),
      script_(Script::Handle(isolate_, script.raw())),
      tokens_iterator_(TokenStream::Handle(isolate_, script.tokens()),
                       token_pos),
      token_kind_(Token::kILLEGAL),
      current_block_(NULL),
      is_top_level_(false),
      current_member_(NULL),
      allow_function_literals_(true),
      parsed_function_(NULL),
      innermost_function_(Function::Handle(isolate_)),
      literal_token_(LiteralToken::Handle(isolate_)),
      current_class_(Class::Handle(isolate_)),
      library_(Library::Handle(isolate_, library.raw())),
      try_blocks_list_(NULL),
      last_used_try_index_(0),
      unregister_pending_function_(false) {
  ASSERT(tokens_iterator_.IsValid());
  ASSERT(!library.IsNull());
}


// For parsing a function.
Parser::Parser(const Script& script,
               ParsedFunction* parsed_function,
               intptr_t token_position)
    : isolate_(Isolate::Current()),
      script_(Script::Handle(isolate_, script.raw())),
      tokens_iterator_(TokenStream::Handle(isolate_, script.tokens()),
                       token_position),
      token_kind_(Token::kILLEGAL),
      current_block_(NULL),
      is_top_level_(false),
      current_member_(NULL),
      allow_function_literals_(true),
      parsed_function_(parsed_function),
      innermost_function_(Function::Handle(isolate_,
                                           parsed_function->function().raw())),
      literal_token_(LiteralToken::Handle(isolate_)),
      current_class_(Class::Handle(isolate_,
                                   parsed_function->function().Owner())),
      library_(Library::Handle(Class::Handle(
          isolate_,
          parsed_function->function().origin()).library())),
      try_blocks_list_(NULL),
      last_used_try_index_(0),
      unregister_pending_function_(false) {
  ASSERT(tokens_iterator_.IsValid());
  ASSERT(!current_function().IsNull());
  if (FLAG_enable_type_checks) {
    EnsureExpressionTemp();
  }
}


Parser::~Parser() {
  if (unregister_pending_function_) {
    const GrowableObjectArray& pending_functions =
        GrowableObjectArray::Handle(
            isolate()->object_store()->pending_functions());
    ASSERT(pending_functions.Length() > 0);
    ASSERT(pending_functions.At(pending_functions.Length()-1) ==
        current_function().raw());
    pending_functions.RemoveLast();
  }
}


void Parser::SetScript(const Script & script, intptr_t token_pos) {
  script_ = script.raw();
  tokens_iterator_.SetStream(TokenStream::Handle(script.tokens()), token_pos);
  token_kind_ = Token::kILLEGAL;
}


bool Parser::SetAllowFunctionLiterals(bool value) {
  bool current_value = allow_function_literals_;
  allow_function_literals_ = value;
  return current_value;
}


const Function& Parser::current_function() const {
  ASSERT(parsed_function() != NULL);
  return parsed_function()->function();
}


const Function& Parser::innermost_function() const {
  return innermost_function_;
}


const Class& Parser::current_class() const {
  return current_class_;
}


void Parser::set_current_class(const Class& value) {
  current_class_ = value.raw();
}


void Parser::SetPosition(intptr_t position) {
  if (position < TokenPos() && position != 0) {
    CompilerStats::num_tokens_rewind += (TokenPos() - position);
  }
  tokens_iterator_.SetCurrentPosition(position);
  token_kind_ = Token::kILLEGAL;
}


void Parser::ParseCompilationUnit(const Library& library,
                                  const Script& script) {
  ASSERT(Isolate::Current()->long_jump_base()->IsSafeToJump());
  TimerScope timer(FLAG_compiler_stats, &CompilerStats::parser_timer);
  Parser parser(script, library, 0);
  parser.ParseTopLevel();
}


Token::Kind Parser::CurrentToken() {
  if (token_kind_ == Token::kILLEGAL) {
    token_kind_ = tokens_iterator_.CurrentTokenKind();
    if (token_kind_ == Token::kERROR) {
      ErrorMsg(TokenPos(), "%s", CurrentLiteral()->ToCString());
    }
  }
  CompilerStats::num_token_checks++;
  return token_kind_;
}


Token::Kind Parser::LookaheadToken(int num_tokens) {
  CompilerStats::num_tokens_lookahead++;
  CompilerStats::num_token_checks++;
  return tokens_iterator_.LookaheadTokenKind(num_tokens);
}


String* Parser::CurrentLiteral() const {
  String& result = String::ZoneHandle();
  result = tokens_iterator_.CurrentLiteral();
  return &result;
}


RawDouble* Parser::CurrentDoubleLiteral() const {
  literal_token_ ^= tokens_iterator_.CurrentToken();
  ASSERT(literal_token_.kind() == Token::kDOUBLE);
  return Double::RawCast(literal_token_.value());
}


RawInteger* Parser::CurrentIntegerLiteral() const {
  literal_token_ ^= tokens_iterator_.CurrentToken();
  ASSERT(literal_token_.kind() == Token::kINTEGER);
  RawInteger* ri = Integer::RawCast(literal_token_.value());
  if (FLAG_throw_on_javascript_int_overflow) {
    const Integer& i = Integer::Handle(ri);
    if (i.CheckJavascriptIntegerOverflow()) {
      ErrorMsg(TokenPos(),
          "Integer literal does not fit in a Javascript integer: %s.",
          i.ToCString());
    }
  }
  return ri;
}


// A QualIdent is an optionally qualified identifier.
struct QualIdent {
  QualIdent() {
    Clear();
  }
  void Clear() {
    lib_prefix = NULL;
    ident_pos = 0;
    ident = NULL;
  }
  LibraryPrefix* lib_prefix;
  intptr_t ident_pos;
  String* ident;
};


struct ParamDesc {
  ParamDesc()
      : type(NULL),
        name_pos(0),
        name(NULL),
        default_value(NULL),
        is_final(false),
        is_field_initializer(false) { }
  const AbstractType* type;
  intptr_t name_pos;
  const String* name;
  const Object* default_value;  // NULL if not an optional parameter.
  bool is_final;
  bool is_field_initializer;
};


struct ParamList {
  ParamList() {
    Clear();
  }

  void Clear() {
    num_fixed_parameters = 0;
    num_optional_parameters = 0;
    has_optional_positional_parameters = false;
    has_optional_named_parameters = false;
    has_field_initializer = false;
    implicitly_final = false;
    skipped = false;
    this->parameters = new ZoneGrowableArray<ParamDesc>();
  }

  void AddFinalParameter(intptr_t name_pos,
                         const String* name,
                         const AbstractType* type) {
    this->num_fixed_parameters++;
    ParamDesc param;
    param.name_pos = name_pos;
    param.name = name;
    param.is_final = true;
    param.type = type;
    this->parameters->Add(param);
  }

  void AddReceiver(const AbstractType* receiver_type, intptr_t token_pos) {
    ASSERT(this->parameters->is_empty());
    AddFinalParameter(token_pos, &Symbols::This(), receiver_type);
  }

  void SetImplicitlyFinal() {
    implicitly_final = true;
  }

  int num_fixed_parameters;
  int num_optional_parameters;
  bool has_optional_positional_parameters;
  bool has_optional_named_parameters;
  bool has_field_initializer;
  bool implicitly_final;
  bool skipped;
  ZoneGrowableArray<ParamDesc>* parameters;
};


struct MemberDesc {
  MemberDesc() {
    Clear();
  }
  void Clear() {
    has_abstract = false;
    has_external = false;
    has_final = false;
    has_const = false;
    has_static = false;
    has_var = false;
    has_factory = false;
    has_operator = false;
    metadata_pos = -1;
    operator_token = Token::kILLEGAL;
    type = NULL;
    name_pos = 0;
    name = NULL;
    redirect_name = NULL;
    constructor_name = NULL;
    params.Clear();
    kind = RawFunction::kRegularFunction;
  }
  bool IsConstructor() const {
    return (kind == RawFunction::kConstructor) && !has_static;
  }
  bool IsFactory() const {
    return (kind == RawFunction::kConstructor) && has_static;
  }
  bool IsFactoryOrConstructor() const {
    return (kind == RawFunction::kConstructor);
  }
  bool IsGetter() const {
    return kind == RawFunction::kGetterFunction;
  }
  bool IsSetter() const {
    return kind == RawFunction::kSetterFunction;
  }
  bool has_abstract;
  bool has_external;
  bool has_final;
  bool has_const;
  bool has_static;
  bool has_var;
  bool has_factory;
  bool has_operator;
  intptr_t metadata_pos;
  Token::Kind operator_token;
  const AbstractType* type;
  intptr_t name_pos;
  intptr_t decl_begin_pos;
  String* name;
  // For constructors: NULL or name of redirected to constructor.
  String* redirect_name;
  // For constructors: NULL for unnamed constructor,
  // identifier after classname for named constructors.
  String* constructor_name;
  ParamList params;
  RawFunction::Kind kind;
};


class ClassDesc : public ValueObject {
 public:
  ClassDesc(const Class& cls,
            const String& cls_name,
            bool is_interface,
            intptr_t token_pos)
      : clazz_(cls),
        class_name_(cls_name),
        token_pos_(token_pos),
        functions_(GrowableObjectArray::Handle(GrowableObjectArray::New())),
        fields_(GrowableObjectArray::Handle(GrowableObjectArray::New())) {
  }

  // Parameter 'name' is the unmangled name, i.e. without the setter
  // name mangling.
  bool FunctionNameExists(const String& name, RawFunction::Kind kind) const {
    // First check if a function or field of same name exists.
    if ((kind != RawFunction::kSetterFunction) && FunctionExists(name)) {
      return true;
    }
    // Now check whether there is a field and whether its implicit getter
    // or setter collides with the name.
    Field* field = LookupField(name);
    if (field != NULL) {
      if (kind == RawFunction::kSetterFunction) {
        // It's ok to have an implicit getter, it does not collide with
        // this setter function.
        if (!field->is_final()) {
          return true;
        }
      } else {
        // The implicit getter of the field collides with the name.
        return true;
      }
    }

    String& accessor_name = String::Handle();
    if (kind == RawFunction::kSetterFunction) {
      // Check if a setter function of same name exists.
      accessor_name = Field::SetterName(name);
      if (FunctionExists(accessor_name)) {
        return true;
      }
    } else {
      // Check if a getter function of same name exists.
      accessor_name = Field::GetterName(name);
      if (FunctionExists(accessor_name)) {
        return true;
      }
    }
    return false;
  }

  bool FieldNameExists(const String& name, bool check_setter) const {
    // First check if a function or field of same name exists.
    if (FunctionExists(name) || FieldExists(name)) {
      return true;
    }
    // Now check if a getter/setter function of same name exists.
    String& getter_name = String::Handle(Field::GetterName(name));
    if (FunctionExists(getter_name)) {
      return true;
    }
    if (check_setter) {
      String& setter_name = String::Handle(Field::SetterName(name));
      if (FunctionExists(setter_name)) {
        return true;
      }
    }
    return false;
  }

  void AddFunction(const Function& function) {
    ASSERT(!FunctionExists(String::Handle(function.name())));
    functions_.Add(function);
  }

  const GrowableObjectArray& functions() const {
    return functions_;
  }

  void AddField(const Field& field) {
    ASSERT(!FieldExists(String::Handle(field.name())));
    fields_.Add(field);
  }

  const GrowableObjectArray& fields() const {
    return fields_;
  }

  const Class& clazz() const {
    return clazz_;
  }

  const String& class_name() const {
    return class_name_;
  }

  bool has_constructor() const {
    Function& func = Function::Handle();
    for (int i = 0; i < functions_.Length(); i++) {
      func ^= functions_.At(i);
      if (func.kind() == RawFunction::kConstructor) {
        return true;
      }
    }
    return false;
  }

  intptr_t token_pos() const {
    return token_pos_;
  }

  void AddMember(const MemberDesc& member) {
    members_.Add(member);
  }

  const GrowableArray<MemberDesc>& members() const {
    return members_;
  }

  MemberDesc* LookupMember(const String& name) const {
    for (int i = 0; i < members_.length(); i++) {
      if (name.Equals(*members_[i].name)) {
        return &members_[i];
      }
    }
    return NULL;
  }

 private:
  Field* LookupField(const String& name) const {
    String& test_name = String::Handle();
    Field& field = Field::Handle();
    for (int i = 0; i < fields_.Length(); i++) {
      field ^= fields_.At(i);
      test_name = field.name();
      if (name.Equals(test_name)) {
        return &field;
      }
    }
    return NULL;
  }

  bool FieldExists(const String& name) const {
    return LookupField(name) != NULL;
  }

  Function* LookupFunction(const String& name) const {
    String& test_name = String::Handle();
    Function& func = Function::Handle();
    for (int i = 0; i < functions_.Length(); i++) {
      func ^= functions_.At(i);
      test_name = func.name();
      if (name.Equals(test_name)) {
        return &func;
      }
    }
    return NULL;
  }

  bool FunctionExists(const String& name) const {
    return LookupFunction(name) != NULL;
  }

  const Class& clazz_;
  const String& class_name_;
  intptr_t token_pos_;   // Token index of "class" keyword.
  GrowableObjectArray& functions_;
  GrowableObjectArray& fields_;
  GrowableArray<MemberDesc> members_;
};


struct TopLevel {
  TopLevel() :
      fields(GrowableObjectArray::Handle(GrowableObjectArray::New())),
      functions(GrowableObjectArray::Handle(GrowableObjectArray::New())) { }

  GrowableObjectArray& fields;
  GrowableObjectArray& functions;
};


static bool HasReturnNode(SequenceNode* seq) {
  if (seq->length() == 0) {
    return false;
  } else if ((seq->length()) == 1 &&
             (seq->NodeAt(seq->length() - 1)->IsSequenceNode())) {
    return HasReturnNode(seq->NodeAt(seq->length() - 1)->AsSequenceNode());
  } else {
    return seq->NodeAt(seq->length() - 1)->IsReturnNode();
  }
}


void Parser::ParseClass(const Class& cls) {
  if (!cls.is_synthesized_class()) {
    TimerScope timer(FLAG_compiler_stats, &CompilerStats::parser_timer);
    Isolate* isolate = Isolate::Current();
    ASSERT(isolate->long_jump_base()->IsSafeToJump());
    const Script& script = Script::Handle(isolate, cls.script());
    const Library& lib = Library::Handle(isolate, cls.library());
    Parser parser(script, lib, cls.token_pos());
    parser.ParseClassDefinition(cls);
  }
}


static bool IsInvisible(const Function& func) {
  if (!Library::IsPrivate(String::Handle(func.name()))) return false;
  // Check for private function in the core libraries.
  const Class& cls = Class::Handle(func.Owner());
  const Library& library = Library::Handle(cls.library());
  if (library.raw() == Library::CoreLibrary()) return true;
  if (library.raw() == Library::CollectionLibrary()) return true;
  if (library.raw() == Library::TypedDataLibrary()) return true;
  if (library.raw() == Library::MathLibrary()) return true;
  return false;
}


RawObject* Parser::ParseFunctionParameters(const Function& func) {
  ASSERT(!func.IsNull());
  Isolate* isolate = Isolate::Current();
  StackZone zone(isolate);
  LongJump* base = isolate->long_jump_base();
  LongJump jump;
  isolate->set_long_jump_base(&jump);
  if (setjmp(*jump.Set()) == 0) {
    const Script& script = Script::Handle(isolate, func.script());
    const Class& owner = Class::Handle(isolate, func.Owner());
    ASSERT(!owner.IsNull());
    const Library& lib = Library::Handle(isolate, owner.library());
    Parser parser(script, lib, func.token_pos());
    parser.set_current_class(owner);
    parser.SkipFunctionPreamble();
    ParamList params;
    parser.ParseFormalParameterList(true, &params);
    ParamDesc* param = params.parameters->data();
    const int param_cnt = params.num_fixed_parameters +
                          params.num_optional_parameters;
    Array& param_descriptor = Array::Handle(isolate, Array::New(param_cnt * 2));
    for (int i = 0, j = 0; i < param_cnt; i++, j += 2) {
      param_descriptor.SetAt(j, param[i].is_final ? Bool::True() :
                                                    Bool::False());
      param_descriptor.SetAt(j + 1,
          (param[i].default_value == NULL) ? Object::null_instance() :
                                             *(param[i].default_value));
    }
    isolate->set_long_jump_base(base);
    return param_descriptor.raw();
  } else {
    Error& error = Error::Handle();
    error = isolate->object_store()->sticky_error();
    isolate->object_store()->clear_sticky_error();
    isolate->set_long_jump_base(base);
    return error.raw();
  }
  UNREACHABLE();
  return Object::null();
}


void Parser::ParseFunction(ParsedFunction* parsed_function) {
  TimerScope timer(FLAG_compiler_stats, &CompilerStats::parser_timer);
  Isolate* isolate = Isolate::Current();
  ASSERT(isolate->long_jump_base()->IsSafeToJump());
  ASSERT(parsed_function != NULL);
  const Function& func = parsed_function->function();
  // Mark private core library functions as invisible by default.
  if (IsInvisible(func)) func.set_is_visible(false);
  const Script& script = Script::Handle(isolate, func.script());
  Parser parser(script, parsed_function, func.token_pos());
  SequenceNode* node_sequence = NULL;
  Array& default_parameter_values = Array::ZoneHandle(isolate, Array::null());
  switch (func.kind()) {
    case RawFunction::kRegularFunction:
    case RawFunction::kClosureFunction:
    case RawFunction::kGetterFunction:
    case RawFunction::kSetterFunction:
    case RawFunction::kConstructor:
      // The call to a redirecting factory is redirected.
      ASSERT(!func.IsRedirectingFactory());
      if (!func.IsImplicitConstructor()) {
        parser.SkipFunctionPreamble();
      }
      node_sequence = parser.ParseFunc(func, default_parameter_values);
      break;
    case RawFunction::kImplicitGetter:
      ASSERT(!func.is_static());
      node_sequence = parser.ParseInstanceGetter(func);
      break;
    case RawFunction::kImplicitSetter:
      ASSERT(!func.is_static());
      node_sequence = parser.ParseInstanceSetter(func);
      break;
    case RawFunction::kImplicitStaticFinalGetter:
      node_sequence = parser.ParseStaticFinalGetter(func);
      break;
    case RawFunction::kMethodExtractor:
      node_sequence = parser.ParseMethodExtractor(func);
      break;
    case RawFunction::kNoSuchMethodDispatcher:
      node_sequence =
          parser.ParseNoSuchMethodDispatcher(func, default_parameter_values);
      break;
    case RawFunction::kInvokeFieldDispatcher:
      node_sequence =
          parser.ParseInvokeFieldDispatcher(func, default_parameter_values);
      break;
    default:
      UNREACHABLE();
  }

  if (!HasReturnNode(node_sequence)) {
    // Add implicit return node.
    node_sequence->Add(new ReturnNode(func.end_token_pos()));
  }
  if (parsed_function->has_expression_temp_var()) {
    node_sequence->scope()->AddVariable(parsed_function->expression_temp_var());
  }
  if (parsed_function->has_saved_current_context_var()) {
    node_sequence->scope()->AddVariable(
        parsed_function->saved_current_context_var());
  }
  parsed_function->SetNodeSequence(node_sequence);

  // The instantiator may be required at run time for generic type checks or
  // allocation of generic types.
  if (parser.IsInstantiatorRequired()) {
    // In the case of a local function, only set the instantiator if the
    // receiver (or type arguments parameter of a factory) was captured.
    LocalVariable* instantiator = NULL;
    const bool kTestOnly = true;
    if (parser.current_function().IsInFactoryScope()) {
      instantiator = parser.LookupTypeArgumentsParameter(node_sequence->scope(),
                                                         kTestOnly);
    } else {
      instantiator = parser.LookupReceiver(node_sequence->scope(), kTestOnly);
    }
    if (!parser.current_function().IsLocalFunction() ||
        ((instantiator != NULL) && instantiator->is_captured())) {
      parsed_function->set_instantiator(
          new LoadLocalNode(node_sequence->token_pos(), instantiator));
    }
  }

  parsed_function->set_default_parameter_values(default_parameter_values);
}


RawObject* Parser::ParseMetadata(const Class& cls, intptr_t token_pos) {
  Isolate* isolate = Isolate::Current();
  StackZone zone(isolate);
  LongJump* base = isolate->long_jump_base();
  LongJump jump;
  isolate->set_long_jump_base(&jump);
  if (setjmp(*jump.Set()) == 0) {
    const Script& script = Script::Handle(cls.script());
    const Library& lib = Library::Handle(cls.library());
    Parser parser(script, lib, token_pos);
    parser.set_current_class(cls);
    return parser.EvaluateMetadata();
  } else {
    Error& error = Error::Handle();
    error = isolate->object_store()->sticky_error();
    isolate->object_store()->clear_sticky_error();
    isolate->set_long_jump_base(base);
    return error.raw();
  }
  UNREACHABLE();
  return Object::null();
}


RawArray* Parser::EvaluateMetadata() {
  if (CurrentToken() != Token::kAT) {
    ErrorMsg("Metadata character '@' expected");
  }
  GrowableObjectArray& meta_values =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  while (CurrentToken() == Token::kAT) {
    ConsumeToken();
    intptr_t expr_pos = TokenPos();
    if (!IsIdentifier()) {
      ExpectIdentifier("identifier expected");
    }
    AstNode* expr = NULL;
    if ((LookaheadToken(1) == Token::kLPAREN) ||
        ((LookaheadToken(1) == Token::kPERIOD) &&
            (LookaheadToken(3) == Token::kLPAREN)) ||
        ((LookaheadToken(1) == Token::kPERIOD) &&
            (LookaheadToken(3) == Token::kPERIOD) &&
            (LookaheadToken(5) == Token::kLPAREN))) {
      expr = ParseNewOperator(Token::kCONST);
    } else {
      expr = ParsePrimary();
    }
    if (expr->EvalConstExpr() == NULL) {
      ErrorMsg(expr_pos, "expression must be a compile-time constant");
    }
    const Instance& val = EvaluateConstExpr(expr);
    meta_values.Add(val);
  }
  return Array::MakeArray(meta_values);
}


SequenceNode* Parser::ParseStaticFinalGetter(const Function& func) {
  TRACE_PARSER("ParseStaticFinalGetter");
  ParamList params;
  ASSERT(func.num_fixed_parameters() == 0);  // static.
  ASSERT(!func.HasOptionalParameters());
  ASSERT(AbstractType::Handle(func.result_type()).IsResolved());

  // Build local scope for function and populate with the formal parameters.
  OpenFunctionBlock(func);
  AddFormalParamsToScope(&params, current_block_->scope);

  intptr_t ident_pos = TokenPos();
  const String& field_name = *ExpectIdentifier("field name expected");
  const Class& field_class = Class::Handle(func.Owner());
  const Field& field =
      Field::ZoneHandle(field_class.LookupStaticField(field_name));

  if (!field.is_const() &&
      (field.value() != Object::transition_sentinel().raw()) &&
      (field.value() != Object::sentinel().raw())) {
    // The field has already been initialized at compile time (this can
    // happen, e.g., if we are recompiling for optimization).  There is no
    // need to check for initialization and compile the potentially very
    // large initialization code.  By skipping this code, the deoptimization
    // ids will not line up with the original code, but this is safe because
    // LoadStaticField does not deoptimize.
    LoadStaticFieldNode* load_node = new LoadStaticFieldNode(ident_pos, field);
    ReturnNode* return_node = new ReturnNode(ident_pos, load_node);
    current_block_->statements->Add(return_node);
    return CloseBlock();
  }

  // Static const fields must have an initializer.
  ExpectToken(Token::kASSIGN);

  // We don't want to use ParseConstExpr() here because we don't want
  // the constant folding code to create, compile and execute a code
  // fragment to evaluate the expression. Instead, we just make sure
  // the static const field initializer is a constant expression and
  // leave the evaluation to the getter function.
  const intptr_t expr_pos = TokenPos();
  AstNode* expr = ParseExpr(kAllowConst, kConsumeCascades);

  if (field.is_const()) {
    // This getter will only be called once at compile time.
    if (expr->EvalConstExpr() == NULL) {
      ErrorMsg(expr_pos, "initializer must be a compile-time constant");
    }
    ReturnNode* return_node = new ReturnNode(ident_pos, expr);
    current_block_->statements->Add(return_node);
  } else {
    // This getter may be called each time the static field is accessed.
    // The following generated code lazily initializes the field:
    // if (field.value === transition_sentinel) {
    //   field.value = null;
    //   throw("circular dependency in field initialization");
    // }
    // if (field.value === sentinel) {
    //   field.value = transition_sentinel;
    //   field.value = expr;
    // }
    // return field.value;  // Type check is executed here in checked mode.

    // Generate code checking for circular dependency in field initialization.
    AstNode* compare_circular = new ComparisonNode(
        ident_pos,
        Token::kEQ_STRICT,
        new LoadStaticFieldNode(ident_pos, field),
        new LiteralNode(ident_pos, Object::transition_sentinel()));
    // Set field to null prior to throwing exception, so that subsequent
    // accesses to the field do not throw again, since initializers should only
    // be executed once.
    SequenceNode* report_circular = new SequenceNode(ident_pos, NULL);
    report_circular->Add(
        new StoreStaticFieldNode(
            ident_pos,
            field,
            new LiteralNode(ident_pos, Instance::ZoneHandle())));
    // TODO(regis, 5802): Exception to throw is not specified by spec.
    const String& circular_error = String::ZoneHandle(
        Symbols::New("circular dependency in field initialization"));
    report_circular->Add(
        new ThrowNode(ident_pos,
                      new LiteralNode(ident_pos, circular_error),
                      NULL));
    AstNode* circular_check =
        new IfNode(ident_pos, compare_circular, report_circular, NULL);
    current_block_->statements->Add(circular_check);

    // Generate code checking for uninitialized field.
    AstNode* compare_uninitialized = new ComparisonNode(
        ident_pos,
        Token::kEQ_STRICT,
        new LoadStaticFieldNode(ident_pos, field),
        new LiteralNode(ident_pos, Object::sentinel()));
    SequenceNode* initialize_field = new SequenceNode(ident_pos, NULL);
    initialize_field->Add(
        new StoreStaticFieldNode(
            ident_pos,
            field,
            new LiteralNode(ident_pos, Object::transition_sentinel())));
    // TODO(hausner): If evaluation of the field value throws an exception,
    // we leave the field value as 'transition_sentinel', which is wrong.
    // A second reference to the field later throws a circular dependency
    // exception. The field should instead be set to null after an exception.
    initialize_field->Add(new StoreStaticFieldNode(ident_pos, field, expr));
    AstNode* uninitialized_check =
        new IfNode(ident_pos, compare_uninitialized, initialize_field, NULL);
    current_block_->statements->Add(uninitialized_check);

    // Generate code returning the field value.
    ReturnNode* return_node =
        new ReturnNode(ident_pos,
                       new LoadStaticFieldNode(ident_pos, field));
    current_block_->statements->Add(return_node);
  }
  return CloseBlock();
}


// Create AstNodes for an implicit instance getter method:
//   LoadLocalNode 0 ('this');
//   LoadInstanceFieldNode (field_name);
//   ReturnNode (field's value);
SequenceNode* Parser::ParseInstanceGetter(const Function& func) {
  TRACE_PARSER("ParseInstanceGetter");
  ParamList params;
  // func.token_pos() points to the name of the field.
  intptr_t ident_pos = func.token_pos();
  ASSERT(current_class().raw() == func.Owner());
  params.AddReceiver(ReceiverType(current_class()), ident_pos);
  ASSERT(func.num_fixed_parameters() == 1);  // receiver.
  ASSERT(!func.HasOptionalParameters());
  ASSERT(AbstractType::Handle(func.result_type()).IsResolved());

  // Build local scope for function and populate with the formal parameters.
  OpenFunctionBlock(func);
  AddFormalParamsToScope(&params, current_block_->scope);

  // Receiver is local 0.
  LocalVariable* receiver = current_block_->scope->VariableAt(0);
  LoadLocalNode* load_receiver = new LoadLocalNode(ident_pos, receiver);
  ASSERT(IsIdentifier());
  const String& field_name = *CurrentLiteral();
  const Class& field_class = Class::Handle(func.Owner());
  const Field& field =
      Field::ZoneHandle(field_class.LookupInstanceField(field_name));

  LoadInstanceFieldNode* load_field =
      new LoadInstanceFieldNode(ident_pos, load_receiver, field);

  ReturnNode* return_node = new ReturnNode(ident_pos, load_field);
  current_block_->statements->Add(return_node);
  return CloseBlock();
}


// Create AstNodes for an implicit instance setter method:
//   LoadLocalNode 0 ('this')
//   LoadLocalNode 1 ('value')
//   SetInstanceField (field_name);
//   ReturnNode (void);
SequenceNode* Parser::ParseInstanceSetter(const Function& func) {
  TRACE_PARSER("ParseInstanceSetter");
  // func.token_pos() points to the name of the field.
  intptr_t ident_pos = func.token_pos();
  const String& field_name = *CurrentLiteral();
  const Class& field_class = Class::ZoneHandle(func.Owner());
  const Field& field =
      Field::ZoneHandle(field_class.LookupInstanceField(field_name));
  const AbstractType& field_type = AbstractType::ZoneHandle(field.type());

  ParamList params;
  ASSERT(current_class().raw() == func.Owner());
  params.AddReceiver(ReceiverType(current_class()), ident_pos);
  params.AddFinalParameter(ident_pos,
                           &Symbols::Value(),
                           &field_type);
  ASSERT(func.num_fixed_parameters() == 2);  // receiver, value.
  ASSERT(!func.HasOptionalParameters());
  ASSERT(AbstractType::Handle(func.result_type()).IsVoidType());

  // Build local scope for function and populate with the formal parameters.
  OpenFunctionBlock(func);
  AddFormalParamsToScope(&params, current_block_->scope);

  LoadLocalNode* receiver =
      new LoadLocalNode(ident_pos, current_block_->scope->VariableAt(0));
  LoadLocalNode* value =
      new LoadLocalNode(ident_pos, current_block_->scope->VariableAt(1));

  EnsureExpressionTemp();
  StoreInstanceFieldNode* store_field =
      new StoreInstanceFieldNode(ident_pos, receiver, field, value);
  current_block_->statements->Add(store_field);
  current_block_->statements->Add(new ReturnNode(ident_pos));
  return CloseBlock();
}


SequenceNode* Parser::ParseMethodExtractor(const Function& func) {
  TRACE_PARSER("ParseMethodExtractor");
  ParamList params;

  const intptr_t ident_pos = func.token_pos();
  ASSERT(func.token_pos() == 0);
  ASSERT(current_class().raw() == func.Owner());
  params.AddReceiver(ReceiverType(current_class()), ident_pos);
  ASSERT(func.num_fixed_parameters() == 1);  // Receiver.
  ASSERT(!func.HasOptionalParameters());

  // Build local scope for function and populate with the formal parameters.
  OpenFunctionBlock(func);
  AddFormalParamsToScope(&params, current_block_->scope);

  // Receiver is local 0.
  LocalVariable* receiver = current_block_->scope->VariableAt(0);
  LoadLocalNode* load_receiver = new LoadLocalNode(ident_pos, receiver);

  ClosureNode* closure = new ClosureNode(
      ident_pos,
      Function::ZoneHandle(func.extracted_method_closure()),
      load_receiver,
      NULL);

  ReturnNode* return_node = new ReturnNode(ident_pos, closure);
  current_block_->statements->Add(return_node);
  return CloseBlock();
}


void Parser::BuildDispatcherScope(const Function& func,
                                  const ArgumentsDescriptor& desc,
                                  Array& default_values) {
  ParamList params;
  // Receiver first.
  intptr_t token_pos = func.token_pos();
  params.AddReceiver(ReceiverType(current_class()), token_pos);
  // Remaining positional parameters.
  intptr_t i = 1;
  for (; i < desc.PositionalCount(); ++i) {
    ParamDesc p;
    char name[64];
    OS::SNPrint(name, 64, ":p%" Pd, i);
    p.name = &String::ZoneHandle(Symbols::New(name));
    p.type = &Type::ZoneHandle(Type::DynamicType());
    params.parameters->Add(p);
    params.num_fixed_parameters++;
  }
  ASSERT(desc.PositionalCount() == params.num_fixed_parameters);

  // Named parameters.
  for (; i < desc.Count(); ++i) {
    ParamDesc p;
    intptr_t index = i - desc.PositionalCount();
    p.name = &String::ZoneHandle(desc.NameAt(index));
    p.type = &Type::ZoneHandle(Type::DynamicType());
    p.default_value = &Object::ZoneHandle();
    params.parameters->Add(p);
    params.num_optional_parameters++;
    params.has_optional_named_parameters = true;
  }
  ASSERT(desc.NamedCount() == params.num_optional_parameters);

  SetupDefaultsForOptionalParams(&params, default_values);

  // Build local scope for function and populate with the formal parameters.
  OpenFunctionBlock(func);
  AddFormalParamsToScope(&params, current_block_->scope);
}

SequenceNode* Parser::ParseNoSuchMethodDispatcher(const Function& func,
                                                  Array& default_values) {
  TRACE_PARSER("ParseNoSuchMethodDispatcher");

  ASSERT(func.IsNoSuchMethodDispatcher());
  intptr_t token_pos = func.token_pos();
  ASSERT(func.token_pos() == 0);
  ASSERT(current_class().raw() == func.Owner());

  ArgumentsDescriptor desc(Array::Handle(func.saved_args_desc()));
  ASSERT(desc.Count() > 0);

  // Set up scope for this function.
  BuildDispatcherScope(func, desc, default_values);

  // Receiver is local 0.
  LocalScope* scope = current_block_->scope;
  ArgumentListNode* func_args = new ArgumentListNode(token_pos);
  for (intptr_t i = 0; i < desc.Count(); ++i) {
    func_args->Add(new LoadLocalNode(token_pos, scope->VariableAt(i)));
  }

  if (desc.NamedCount() > 0) {
    const Array& arg_names =
        Array::ZoneHandle(Array::New(desc.NamedCount()));
    for (intptr_t i = 0; i < arg_names.Length(); ++i) {
      arg_names.SetAt(i, String::Handle(desc.NameAt(i)));
    }
    func_args->set_names(arg_names);
  }

  const String& func_name = String::ZoneHandle(func.name());
  ArgumentListNode* arguments = BuildNoSuchMethodArguments(token_pos,
                                                           func_name,
                                                           *func_args);
  const Function& no_such_method = Function::ZoneHandle(
        Resolver::ResolveDynamicAnyArgs(Class::Handle(func.Owner()),
                                        Symbols::NoSuchMethod()));
  StaticCallNode* call =
      new StaticCallNode(token_pos, no_such_method, arguments);


  ReturnNode* return_node = new ReturnNode(token_pos, call);
  current_block_->statements->Add(return_node);
  return CloseBlock();
}


SequenceNode* Parser::ParseInvokeFieldDispatcher(const Function& func,
                                                 Array& default_values) {
  TRACE_PARSER("ParseInvokeFieldDispatcher");

  ASSERT(func.IsInvokeFieldDispatcher());
  intptr_t token_pos = func.token_pos();
  ASSERT(func.token_pos() == 0);
  ASSERT(current_class().raw() == func.Owner());

  const Array& args_desc = Array::Handle(func.saved_args_desc());
  ArgumentsDescriptor desc(args_desc);
  ASSERT(desc.Count() > 0);

  // Set up scope for this function.
  BuildDispatcherScope(func, desc, default_values);

  // Receiver is local 0.
  LocalScope* scope = current_block_->scope;
  ArgumentListNode* no_args = new ArgumentListNode(token_pos);
  LoadLocalNode* receiver = new LoadLocalNode(token_pos, scope->VariableAt(0));

  const String& name = String::Handle(func.name());
  const String& getter_name =
      String::ZoneHandle(Symbols::New(String::Handle(Field::GetterName(name))));
  InstanceCallNode* getter_call = new InstanceCallNode(token_pos,
                                                       receiver,
                                                       getter_name,
                                                       no_args);

  // Pass arguments 1..n to the closure call.
  ArgumentListNode* closure_args = new ArgumentListNode(token_pos);
  const Array& names = Array::Handle(Array::New(desc.NamedCount(), Heap::kOld));
  // Positional parameters.
  intptr_t i = 1;
  for (; i < desc.PositionalCount(); ++i) {
    closure_args->Add(new LoadLocalNode(token_pos, scope->VariableAt(i)));
  }
  // Named parameters.
  for (; i < desc.Count(); i++) {
    closure_args->Add(new LoadLocalNode(token_pos, scope->VariableAt(i)));
    intptr_t index = i - desc.PositionalCount();
    names.SetAt(index, String::Handle(desc.NameAt(index)));
  }
  closure_args->set_names(names);

  EnsureSavedCurrentContext();
  ClosureCallNode* closure_call = new ClosureCallNode(token_pos,
                                                      getter_call,
                                                      closure_args);

  ReturnNode* return_node = new ReturnNode(token_pos, closure_call);
  current_block_->statements->Add(return_node);
  return CloseBlock();
}


void Parser::SkipBlock() {
  ASSERT(CurrentToken() == Token::kLBRACE);
  GrowableArray<Token::Kind> token_stack(8);
  const intptr_t block_start_pos = TokenPos();
  bool is_match = true;
  bool unexpected_token_found = false;
  Token::Kind token;
  intptr_t token_pos;
  do {
    token = CurrentToken();
    token_pos = TokenPos();
    switch (token) {
      case Token::kLBRACE:
      case Token::kLPAREN:
      case Token::kLBRACK:
        token_stack.Add(token);
        break;
      case Token::kRBRACE:
        is_match = token_stack.RemoveLast() == Token::kLBRACE;
        break;
      case Token::kRPAREN:
        is_match = token_stack.RemoveLast() == Token::kLPAREN;
        break;
      case Token::kRBRACK:
        is_match = token_stack.RemoveLast() == Token::kLBRACK;
        break;
      case Token::kEOS:
        unexpected_token_found = true;
        break;
      default:
        // nothing.
        break;
    }
    ConsumeToken();
  } while (!token_stack.is_empty() && is_match && !unexpected_token_found);
  if (!is_match) {
    ErrorMsg(token_pos, "unbalanced '%s'", Token::Str(token));
  } else if (unexpected_token_found) {
    ErrorMsg(block_start_pos, "unterminated block");
  }
}


void Parser::ParseFormalParameter(bool allow_explicit_default_value,
                                  ParamList* params) {
  TRACE_PARSER("ParseFormalParameter");
  ParamDesc parameter;
  bool var_seen = false;
  bool this_seen = false;

  SkipMetadata();
  if (CurrentToken() == Token::kFINAL) {
    ConsumeToken();
    parameter.is_final = true;
  } else if (CurrentToken() == Token::kVAR) {
    ConsumeToken();
    var_seen = true;
    // The parameter type is the 'dynamic' type.
    parameter.type = &Type::ZoneHandle(Type::DynamicType());
  }
  if (CurrentToken() == Token::kTHIS) {
    ConsumeToken();
    ExpectToken(Token::kPERIOD);
    this_seen = true;
    parameter.is_field_initializer = true;
  }
  if (params->implicitly_final) {
    parameter.is_final = true;
  }
  if ((parameter.type == NULL) && (CurrentToken() == Token::kVOID)) {
    ConsumeToken();
    // This must later be changed to a closure type if we recognize
    // a closure/function type parameter. We check this at the end
    // of ParseFormalParameter.
    parameter.type = &Type::ZoneHandle(Type::VoidType());
  }
  if (parameter.type == NULL) {
    // At this point, we must see an identifier for the type or the
    // function parameter.
    if (!IsIdentifier()) {
      ErrorMsg("parameter name or type expected");
    }
    // We have not seen a parameter type yet, so we check if the next
    // identifier could represent a type before parsing it.
    Token::Kind follower = LookaheadToken(1);
    // We have an identifier followed by a 'follower' token.
    // We either parse a type or assume that no type is specified.
    if ((follower == Token::kLT) ||  // Parameterized type.
        (follower == Token::kPERIOD) ||  // Qualified class name of type.
        Token::IsIdentifier(follower) ||  // Parameter name following a type.
        (follower == Token::kTHIS)) {  // Field parameter following a type.
      // The types of formal parameters are never ignored, even in unchecked
      // mode, because they are part of the function type of closurized
      // functions appearing in type tests with typedefs.
      parameter.type = &AbstractType::ZoneHandle(
          ParseType(is_top_level_ ? ClassFinalizer::kResolveTypeParameters :
                                    ClassFinalizer::kCanonicalize));
    } else {
      parameter.type = &Type::ZoneHandle(Type::DynamicType());
    }
  }
  if (!this_seen && (CurrentToken() == Token::kTHIS)) {
    ConsumeToken();
    ExpectToken(Token::kPERIOD);
    this_seen = true;
    parameter.is_field_initializer = true;
  }

  // At this point, we must see an identifier for the parameter name.
  parameter.name_pos = TokenPos();
  parameter.name = ExpectIdentifier("parameter name expected");
  if (parameter.is_field_initializer) {
    params->has_field_initializer = true;
  }

  if (params->has_optional_named_parameters &&
      (parameter.name->CharAt(0) == '_')) {
    ErrorMsg(parameter.name_pos, "named parameter must not be private");
  }

  // Check for duplicate formal parameters.
  const intptr_t num_existing_parameters =
      params->num_fixed_parameters + params->num_optional_parameters;
  for (intptr_t i = 0; i < num_existing_parameters; i++) {
    ParamDesc& existing_parameter = (*params->parameters)[i];
    if (existing_parameter.name->Equals(*parameter.name)) {
      ErrorMsg(parameter.name_pos, "duplicate formal parameter '%s'",
               parameter.name->ToCString());
    }
  }

  if (CurrentToken() == Token::kLPAREN) {
    // This parameter is probably a closure. If we saw the keyword 'var'
    // or 'final', a closure is not legal here and we ignore the
    // opening parens.
    if (!var_seen && !parameter.is_final) {
      // The parsed parameter type is actually the function result type.
      const AbstractType& result_type =
          AbstractType::Handle(parameter.type->raw());

      // Finish parsing the function type parameter.
      ParamList func_params;

      // Add implicit closure object parameter.
      func_params.AddFinalParameter(
          TokenPos(),
          &Symbols::ClosureParameter(),
          &Type::ZoneHandle(Type::DynamicType()));

      const bool no_explicit_default_values = false;
      ParseFormalParameterList(no_explicit_default_values, &func_params);

      // The field 'is_static' has no meaning for signature functions.
      const Function& signature_function = Function::Handle(
          Function::New(*parameter.name,
                        RawFunction::kSignatureFunction,
                        /* is_static = */ false,
                        /* is_const = */ false,
                        /* is_abstract = */ false,
                        /* is_external = */ false,
                        current_class(),
                        parameter.name_pos));
      signature_function.set_result_type(result_type);
      AddFormalParamsToFunction(&func_params, signature_function);
      const String& signature = String::Handle(signature_function.Signature());
      // Lookup the signature class, i.e. the class whose name is the signature.
      // We only lookup in the current library, but not in its imports, and only
      // create a new canonical signature class if it does not exist yet.
      Class& signature_class = Class::ZoneHandle(
          library_.LookupLocalClass(signature));
      if (signature_class.IsNull()) {
        signature_class = Class::NewSignatureClass(signature,
                                                   signature_function,
                                                   script_,
                                                   parameter.name_pos);
        // Record the function signature class in the current library, unless
        // we are currently skipping a formal parameter list, in which case
        // the signature class could remain unfinalized.
        if (!params->skipped) {
          library_.AddClass(signature_class);
        }
      } else {
        signature_function.set_signature_class(signature_class);
      }
      ASSERT(signature_function.signature_class() == signature_class.raw());
      Type& signature_type = Type::ZoneHandle(signature_class.SignatureType());
      if (!is_top_level_ && !signature_type.IsFinalized()) {
        signature_type ^= ClassFinalizer::FinalizeType(
            signature_class, signature_type, ClassFinalizer::kCanonicalize);
      }
      // The type of the parameter is now the signature type.
      parameter.type = &signature_type;
    }
  }

  if ((CurrentToken() == Token::kASSIGN) || (CurrentToken() == Token::kCOLON)) {
    if ((!params->has_optional_positional_parameters &&
         !params->has_optional_named_parameters) ||
        !allow_explicit_default_value) {
      ErrorMsg("parameter must not specify a default value");
    }
    if (params->has_optional_positional_parameters) {
      ExpectToken(Token::kASSIGN);
    } else {
      ExpectToken(Token::kCOLON);
    }
    params->num_optional_parameters++;
    if (is_top_level_) {
      // Skip default value parsing.
      SkipExpr();
    } else {
      const Object& const_value = ParseConstExpr()->literal();
      parameter.default_value = &const_value;
    }
  } else {
    if (params->has_optional_positional_parameters ||
        params->has_optional_named_parameters) {
      // Implicit default value is null.
      params->num_optional_parameters++;
      parameter.default_value = &Object::ZoneHandle();
    } else {
      params->num_fixed_parameters++;
      ASSERT(params->num_optional_parameters == 0);
    }
  }
  if (parameter.type->IsVoidType()) {
    ErrorMsg("parameter '%s' may not be 'void'", parameter.name->ToCString());
  }
  params->parameters->Add(parameter);
}


// Parses a sequence of normal or optional formal parameters.
void Parser::ParseFormalParameters(bool allow_explicit_default_values,
                                   ParamList* params) {
  TRACE_PARSER("ParseFormalParameters");
  do {
    ConsumeToken();
    if (!params->has_optional_positional_parameters &&
        !params->has_optional_named_parameters &&
        (CurrentToken() == Token::kLBRACK)) {
      // End of normal parameters, start of optional positional parameters.
      params->has_optional_positional_parameters = true;
      return;
    }
    if (!params->has_optional_positional_parameters &&
        !params->has_optional_named_parameters &&
        (CurrentToken() == Token::kLBRACE)) {
      // End of normal parameters, start of optional named parameters.
      params->has_optional_named_parameters = true;
      return;
    }
    ParseFormalParameter(allow_explicit_default_values, params);
  } while (CurrentToken() == Token::kCOMMA);
}


void Parser::ParseFormalParameterList(bool allow_explicit_default_values,
                                      ParamList* params) {
  TRACE_PARSER("ParseFormalParameterList");
  ASSERT(CurrentToken() == Token::kLPAREN);

  if (LookaheadToken(1) != Token::kRPAREN) {
    // Parse fixed parameters.
    ParseFormalParameters(allow_explicit_default_values, params);
    if (params->has_optional_positional_parameters ||
        params->has_optional_named_parameters) {
      // Parse optional parameters.
      ParseFormalParameters(allow_explicit_default_values, params);
      if (params->has_optional_positional_parameters) {
        if (CurrentToken() != Token::kRBRACK) {
          ErrorMsg("',' or ']' expected");
        }
      } else {
        if (CurrentToken() != Token::kRBRACE) {
          ErrorMsg("',' or '}' expected");
        }
      }
      ConsumeToken();  // ']' or '}'.
    }
    if ((CurrentToken() != Token::kRPAREN) &&
        !params->has_optional_positional_parameters &&
        !params->has_optional_named_parameters) {
      ErrorMsg("',' or ')' expected");
    }
  } else {
    ConsumeToken();
  }
  ExpectToken(Token::kRPAREN);
}


String& Parser::ParseNativeDeclaration() {
  TRACE_PARSER("ParseNativeDeclaration");
  ASSERT(IsLiteral("native"));
  ConsumeToken();
  if (CurrentToken() != Token::kSTRING) {
    ErrorMsg("string literal expected");
  }
  String& native_name = *CurrentLiteral();
  ConsumeToken();
  return native_name;
}


// Resolve and return the dynamic function of the given name in the superclass.
// If it is not found, and resolve_getter is true, try to resolve a getter of
// the same name. If it is still not found, return noSuchMethod and
// set is_no_such_method to true..
RawFunction* Parser::GetSuperFunction(intptr_t token_pos,
                                      const String& name,
                                      ArgumentListNode* arguments,
                                      bool resolve_getter,
                                      bool* is_no_such_method) {
  const Class& super_class = Class::Handle(current_class().SuperClass());
  if (super_class.IsNull()) {
    ErrorMsg(token_pos, "class '%s' does not have a superclass",
             String::Handle(current_class().Name()).ToCString());
  }
  Function& super_func = Function::Handle(
      Resolver::ResolveDynamicAnyArgs(super_class, name));
  if (!super_func.IsNull() &&
      !super_func.AreValidArguments(arguments->length(),
                                    arguments->names(),
                                    NULL)) {
    super_func = Function::null();
  } else if (super_func.IsNull() && resolve_getter) {
    const String& getter_name = String::ZoneHandle(Field::GetterName(name));
    super_func = Resolver::ResolveDynamicAnyArgs(super_class, getter_name);
    ASSERT(super_func.IsNull() ||
           (super_func.kind() != RawFunction::kImplicitStaticFinalGetter));
  }
  if (super_func.IsNull()) {
    super_func =
        Resolver::ResolveDynamicAnyArgs(super_class, Symbols::NoSuchMethod());
    ASSERT(!super_func.IsNull());
    *is_no_such_method = true;
  } else {
    *is_no_such_method = false;
  }
  return super_func.raw();
}


// Lookup class in the core lib which also contains various VM
// helper methods and classes. Allow look up of private classes.
static RawClass* LookupCoreClass(const String& class_name) {
  const Library& core_lib = Library::Handle(Library::CoreLibrary());
  String& name = String::Handle(class_name.raw());
  if (class_name.CharAt(0) == Scanner::kPrivateIdentifierStart) {
    // Private identifiers are mangled on a per script basis.
    name = String::Concat(name, String::Handle(core_lib.private_key()));
    name = Symbols::New(name);
  }
  return core_lib.LookupClass(name, NULL);  // No ambiguity error expected.
}


static const String& PrivateCoreLibName(const String& str) {
  const Library& core_lib = Library::Handle(Library::CoreLibrary());
  const String& private_name = String::ZoneHandle(core_lib.PrivateName(str));
  return private_name;
}


StaticCallNode* Parser::BuildInvocationMirrorAllocation(
    intptr_t call_pos,
    const String& function_name,
    const ArgumentListNode& function_args,
    const LocalVariable* temp_for_last_arg) {
  const intptr_t args_pos = function_args.token_pos();
  // Build arguments to the call to the static
  // InvocationMirror._allocateInvocationMirror method.
  ArgumentListNode* arguments = new ArgumentListNode(args_pos);
  // The first argument is the original function name.
  arguments->Add(new LiteralNode(args_pos, function_name));
  // The second argument is the arguments descriptor of the original function.
  const Array& args_descriptor =
      Array::ZoneHandle(ArgumentsDescriptor::New(function_args.length(),
                                                 function_args.names()));
  arguments->Add(new LiteralNode(args_pos, args_descriptor));
  // The third argument is an array containing the original function arguments,
  // including the receiver.
  ArrayNode* args_array =
      new ArrayNode(args_pos, Type::ZoneHandle(Type::ArrayType()));
  for (intptr_t i = 0; i < function_args.length(); i++) {
    AstNode* arg = function_args.NodeAt(i);
    if ((temp_for_last_arg != NULL) && (i == function_args.length() - 1)) {
      LetNode* store_arg = new LetNode(arg->token_pos());
      store_arg->AddNode(new StoreLocalNode(arg->token_pos(),
                                           temp_for_last_arg,
                                           arg));
      store_arg->AddNode(new LoadLocalNode(arg->token_pos(),
                                           temp_for_last_arg));
      args_array->AddElement(store_arg);
    } else {
      args_array->AddElement(arg);
    }
  }
  arguments->Add(args_array);
  // Lookup the static InvocationMirror._allocateInvocationMirror method.
  const Class& mirror_class =
      Class::Handle(LookupCoreClass(Symbols::InvocationMirror()));
  ASSERT(!mirror_class.IsNull());
  const Function& allocation_function = Function::ZoneHandle(
      mirror_class.LookupStaticFunction(
          PrivateCoreLibName(Symbols::AllocateInvocationMirror())));
  ASSERT(!allocation_function.IsNull());
  return new StaticCallNode(call_pos, allocation_function, arguments);
}


ArgumentListNode* Parser::BuildNoSuchMethodArguments(
    intptr_t call_pos,
    const String& function_name,
    const ArgumentListNode& function_args,
    const LocalVariable* temp_for_last_arg) {
  ASSERT(function_args.length() >= 1);  // The receiver is the first argument.
  const intptr_t args_pos = function_args.token_pos();
  ArgumentListNode* arguments = new ArgumentListNode(args_pos);
  arguments->Add(function_args.NodeAt(0));
  // The second argument is the invocation mirror.
  arguments->Add(BuildInvocationMirrorAllocation(
      call_pos, function_name, function_args, temp_for_last_arg));
  return arguments;
}


AstNode* Parser::ParseSuperCall(const String& function_name) {
  TRACE_PARSER("ParseSuperCall");
  ASSERT(CurrentToken() == Token::kLPAREN);
  const intptr_t supercall_pos = TokenPos();

  // 'this' parameter is the first argument to super call.
  ArgumentListNode* arguments = new ArgumentListNode(supercall_pos);
  AstNode* receiver = LoadReceiver(supercall_pos);
  arguments->Add(receiver);
  ParseActualParameters(arguments, kAllowConst);

  const bool kResolveGetter = true;
  bool is_no_such_method = false;
  const Function& super_function = Function::ZoneHandle(
      GetSuperFunction(supercall_pos,
                       function_name,
                       arguments,
                       kResolveGetter,
                       &is_no_such_method));
  if (super_function.IsGetterFunction() ||
      super_function.IsImplicitGetterFunction()) {
    const Class& super_class = Class::ZoneHandle(current_class().SuperClass());
    AstNode* closure = new StaticGetterNode(supercall_pos,
                                            LoadReceiver(supercall_pos),
                                            /* is_super_getter */ true,
                                            super_class,
                                            function_name);
    EnsureSavedCurrentContext();
    // 'this' is not passed as parameter to the closure.
    ArgumentListNode* closure_arguments = new ArgumentListNode(supercall_pos);
    for (int i = 1; i < arguments->length(); i++) {
      closure_arguments->Add(arguments->NodeAt(i));
    }
    return new ClosureCallNode(supercall_pos, closure, closure_arguments);
  }
  if (is_no_such_method) {
    arguments = BuildNoSuchMethodArguments(
        supercall_pos, function_name, *arguments);
  }
  return new StaticCallNode(supercall_pos, super_function, arguments);
}


// Simple test if a node is side effect free.
static bool IsSimpleLocalOrLiteralNode(AstNode* node) {
  return node->IsLiteralNode() || node->IsLoadLocalNode();
}


AstNode* Parser::BuildUnarySuperOperator(Token::Kind op, PrimaryNode* super) {
  ASSERT(super->IsSuper());
  AstNode* super_op = NULL;
  const intptr_t super_pos = super->token_pos();
  if ((op == Token::kNEGATE) ||
      (op == Token::kBIT_NOT)) {
    // Resolve the operator function in the superclass.
    const String& operator_function_name =
        String::ZoneHandle(Symbols::New(Token::Str(op)));
    ArgumentListNode* op_arguments = new ArgumentListNode(super_pos);
    AstNode* receiver = LoadReceiver(super_pos);
    op_arguments->Add(receiver);
    const bool kResolveGetter = false;
    bool is_no_such_method = false;
    const Function& super_operator = Function::ZoneHandle(
        GetSuperFunction(super_pos,
                         operator_function_name,
                         op_arguments,
                         kResolveGetter,
                         &is_no_such_method));
    if (is_no_such_method) {
      op_arguments = BuildNoSuchMethodArguments(
          super_pos, operator_function_name, *op_arguments);
    }
    super_op = new StaticCallNode(super_pos, super_operator, op_arguments);
  } else {
    ErrorMsg(super_pos, "illegal super operator call");
  }
  return super_op;
}


AstNode* Parser::ParseSuperOperator() {
  TRACE_PARSER("ParseSuperOperator");
  AstNode* super_op = NULL;
  const intptr_t operator_pos = TokenPos();

  if (CurrentToken() == Token::kLBRACK) {
    ConsumeToken();
    AstNode* index_expr = ParseExpr(kAllowConst, kConsumeCascades);
    ExpectToken(Token::kRBRACK);
    AstNode* receiver = LoadReceiver(operator_pos);
    const Class& super_class = Class::ZoneHandle(current_class().SuperClass());
    ASSERT(!super_class.IsNull());
    super_op =
        new LoadIndexedNode(operator_pos, receiver, index_expr, super_class);
  } else {
    ASSERT(Token::CanBeOverloaded(CurrentToken()) ||
           (CurrentToken() == Token::kNE));
    Token::Kind op = CurrentToken();
    ConsumeToken();

    bool negate_result = false;
    if (op == Token::kNE) {
      op = Token::kEQ;
      negate_result = true;
    }

    ASSERT(Token::Precedence(op) >= Token::Precedence(Token::kBIT_OR));
    AstNode* other_operand = ParseBinaryExpr(Token::Precedence(op) + 1);

    ArgumentListNode* op_arguments = new ArgumentListNode(operator_pos);
    AstNode* receiver = LoadReceiver(operator_pos);
    op_arguments->Add(receiver);
    op_arguments->Add(other_operand);

    // Resolve the operator function in the superclass.
    const String& operator_function_name =
        String::ZoneHandle(Symbols::New(Token::Str(op)));
    const bool kResolveGetter = false;
    bool is_no_such_method = false;
    const Function& super_operator = Function::ZoneHandle(
        GetSuperFunction(operator_pos,
                         operator_function_name,
                         op_arguments,
                         kResolveGetter,
                         &is_no_such_method));
    if (is_no_such_method) {
      op_arguments = BuildNoSuchMethodArguments(
          operator_pos, operator_function_name, *op_arguments);
    }
    super_op = new StaticCallNode(operator_pos, super_operator, op_arguments);
    if (negate_result) {
      super_op = new UnaryOpNode(operator_pos, Token::kNOT, super_op);
    }
  }
  return super_op;
}


AstNode* Parser::CreateImplicitClosureNode(const Function& func,
                                           intptr_t token_pos,
                                           AstNode* receiver) {
  Function& implicit_closure_function =
      Function::ZoneHandle(func.ImplicitClosureFunction());
  if (receiver != NULL) {
    // If we create an implicit instance closure from inside a closure of a
    // parameterized class, make sure that the receiver is captured as
    // instantiator.
    if (current_block_->scope->function_level() > 0) {
      const Class& signature_class = Class::Handle(
          implicit_closure_function.signature_class());
      if (signature_class.NumTypeParameters() > 0) {
        CaptureInstantiator();
      }
    }
  }
  return new ClosureNode(token_pos, implicit_closure_function, receiver, NULL);
}


AstNode* Parser::ParseSuperFieldAccess(const String& field_name) {
  TRACE_PARSER("ParseSuperFieldAccess");
  const intptr_t field_pos = TokenPos();
  const Class& super_class = Class::ZoneHandle(current_class().SuperClass());
  if (super_class.IsNull()) {
    ErrorMsg("class '%s' does not have a superclass",
             String::Handle(current_class().Name()).ToCString());
  }
  AstNode* implicit_argument = LoadReceiver(field_pos);

  const String& getter_name =
      String::ZoneHandle(Field::GetterName(field_name));
  const Function& super_getter = Function::ZoneHandle(
      Resolver::ResolveDynamicAnyArgs(super_class, getter_name));
  if (super_getter.IsNull()) {
    const String& setter_name =
        String::ZoneHandle(Field::SetterName(field_name));
    const Function& super_setter = Function::ZoneHandle(
        Resolver::ResolveDynamicAnyArgs(super_class, setter_name));
    if (super_setter.IsNull()) {
      // Check if this is an access to an implicit closure using 'super'.
      // If a function exists of the specified field_name then try
      // accessing it as a getter, at runtime we will handle this by
      // creating an implicit closure of the function and returning it.
      const Function& super_function = Function::ZoneHandle(
          Resolver::ResolveDynamicAnyArgs(super_class, field_name));
      if (!super_function.IsNull()) {
        // In case CreateAssignmentNode is called later on this
        // CreateImplicitClosureNode, it will be replaced by a StaticSetterNode.
        return CreateImplicitClosureNode(super_function,
                                         field_pos,
                                         implicit_argument);
      }
      // No function or field exists of the specified field_name.
      // Emit a StaticGetterNode anyway, so that noSuchMethod gets called.
    }
  }
  return new StaticGetterNode(
      field_pos, implicit_argument, true, super_class, field_name);
}


void Parser::GenerateSuperConstructorCall(const Class& cls,
                                          LocalVariable* receiver,
                                          ArgumentListNode* forwarding_args) {
  const intptr_t supercall_pos = TokenPos();
  const Class& super_class = Class::Handle(cls.SuperClass());
  // Omit the implicit super() if there is no super class (i.e.
  // we're not compiling class Object), or if the super class is an
  // artificially generated "wrapper class" that has no constructor.
  if (super_class.IsNull() ||
      (super_class.num_native_fields() > 0 &&
       Class::Handle(super_class.SuperClass()).IsObjectClass())) {
    return;
  }
  String& super_ctor_name = String::Handle(super_class.Name());
  super_ctor_name = String::Concat(super_ctor_name, Symbols::Dot());

  ArgumentListNode* arguments = new ArgumentListNode(supercall_pos);
  // Implicit 'this' parameter is the first argument.
  AstNode* implicit_argument = new LoadLocalNode(supercall_pos, receiver);
  arguments->Add(implicit_argument);
  // Implicit construction phase parameter is second argument.
  AstNode* phase_parameter =
      new LiteralNode(supercall_pos,
                      Smi::ZoneHandle(Smi::New(Function::kCtorPhaseAll)));
  arguments->Add(phase_parameter);

  // If this is a super call in a forwarding constructor, add the user-
  // defined arguments to the super call and adjust the the super
  // constructor name to the respective named constructor if necessary.
  if (forwarding_args != NULL) {
    for (int i = 0; i < forwarding_args->length(); i++) {
      arguments->Add(forwarding_args->NodeAt(i));
    }
    String& ctor_name = String::Handle(current_function().name());
    String& class_name = String::Handle(cls.Name());
    if (ctor_name.Length() > class_name.Length() + 1) {
      // Generating a forwarding call to a named constructor 'C.n'.
      // Add the constructor name 'n' to the super constructor.
      ctor_name = String::SubString(ctor_name, class_name.Length() + 1);
      super_ctor_name = String::Concat(super_ctor_name, ctor_name);
    }
  }

  // Resolve super constructor function and check arguments.
  const Function& super_ctor = Function::ZoneHandle(
      super_class.LookupConstructor(super_ctor_name));
  if (super_ctor.IsNull()) {
      ErrorMsg(supercall_pos,
               "unresolved implicit call to super constructor '%s()'",
               String::Handle(super_class.Name()).ToCString());
  }
  if (current_function().is_const() && !super_ctor.is_const()) {
    ErrorMsg(supercall_pos, "implicit call to non-const super constructor");
  }

  String& error_message = String::Handle();
  if (!super_ctor.AreValidArguments(arguments->length(),
                                    arguments->names(),
                                    &error_message)) {
    ErrorMsg(supercall_pos,
             "invalid arguments passed to super constructor '%s()': %s",
             String::Handle(super_class.Name()).ToCString(),
             error_message.ToCString());
  }
  current_block_->statements->Add(
      new StaticCallNode(supercall_pos, super_ctor, arguments));
}


AstNode* Parser::ParseSuperInitializer(const Class& cls,
                                       LocalVariable* receiver) {
  TRACE_PARSER("ParseSuperInitializer");
  ASSERT(CurrentToken() == Token::kSUPER);
  const intptr_t supercall_pos = TokenPos();
  ConsumeToken();
  const Class& super_class = Class::Handle(cls.SuperClass());
  ASSERT(!super_class.IsNull());
  String& ctor_name = String::Handle(super_class.Name());
  ctor_name = String::Concat(ctor_name, Symbols::Dot());
  if (CurrentToken() == Token::kPERIOD) {
    ConsumeToken();
    ctor_name = String::Concat(ctor_name,
                               *ExpectIdentifier("constructor name expected"));
  }
  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg("parameter list expected");
  }

  ArgumentListNode* arguments = new ArgumentListNode(supercall_pos);
  // 'this' parameter is the first argument to super class constructor.
  AstNode* implicit_argument = new LoadLocalNode(supercall_pos, receiver);
  arguments->Add(implicit_argument);
  // Second implicit parameter is the construction phase. We optimistically
  // assume that we can execute both the super initializer and the super
  // constructor body. We may later change this to only execute the
  // super initializer.
  AstNode* phase_parameter =
      new LiteralNode(supercall_pos,
                      Smi::ZoneHandle(Smi::New(Function::kCtorPhaseAll)));
  arguments->Add(phase_parameter);
  // 'this' parameter must not be accessible to the other super call arguments.
  receiver->set_invisible(true);
  ParseActualParameters(arguments, kAllowConst);
  receiver->set_invisible(false);

  // Resolve the constructor.
  const Function& super_ctor = Function::ZoneHandle(
      super_class.LookupConstructor(ctor_name));
  if (super_ctor.IsNull()) {
    ErrorMsg(supercall_pos,
             "super class constructor '%s' not found",
             ctor_name.ToCString());
  }
  if (current_function().is_const() && !super_ctor.is_const()) {
    ErrorMsg(supercall_pos, "super constructor must be const");
  }
  String& error_message = String::Handle();
  if (!super_ctor.AreValidArguments(arguments->length(),
                                    arguments->names(),
                                    &error_message)) {
    ErrorMsg(supercall_pos,
             "invalid arguments passed to super class constructor '%s': %s",
             ctor_name.ToCString(),
             error_message.ToCString());
  }
  return new StaticCallNode(supercall_pos, super_ctor, arguments);
}


AstNode* Parser::ParseInitializer(const Class& cls,
                                  LocalVariable* receiver,
                                  GrowableArray<Field*>* initialized_fields) {
  TRACE_PARSER("ParseInitializer");
  const intptr_t field_pos = TokenPos();
  if (CurrentToken() == Token::kTHIS) {
    ConsumeToken();
    ExpectToken(Token::kPERIOD);
  }
  const String& field_name = *ExpectIdentifier("field name expected");
  ExpectToken(Token::kASSIGN);

  const bool saved_mode = SetAllowFunctionLiterals(false);
  // "this" must not be accessible in initializer expressions.
  receiver->set_invisible(true);
  AstNode* init_expr = ParseConditionalExpr();
  if (CurrentToken() == Token::kCASCADE) {
    init_expr = ParseCascades(init_expr);
  }
  receiver->set_invisible(false);
  SetAllowFunctionLiterals(saved_mode);
  if (current_function().is_const() && !init_expr->IsPotentiallyConst()) {
    ErrorMsg(field_pos,
             "initializer expression must be compile time constant.");
  }
  Field& field = Field::ZoneHandle(cls.LookupInstanceField(field_name));
  if (field.IsNull()) {
    ErrorMsg(field_pos, "unresolved reference to instance field '%s'",
             field_name.ToCString());
  }
  CheckDuplicateFieldInit(field_pos, initialized_fields, &field);
  AstNode* instance = new LoadLocalNode(field_pos, receiver);
  EnsureExpressionTemp();
  return new StoreInstanceFieldNode(field_pos, instance, field, init_expr);
}


void Parser::CheckConstFieldsInitialized(const Class& cls) {
  const Array& fields = Array::Handle(cls.fields());
  Field& field = Field::Handle();
  SequenceNode* initializers = current_block_->statements;
  for (int field_num = 0; field_num < fields.Length(); field_num++) {
    field ^= fields.At(field_num);
    if (field.is_static()) {
      continue;
    }

    bool found = false;
    for (int i = 0; i < initializers->length(); i++) {
      found = false;
      if (initializers->NodeAt(i)->IsStoreInstanceFieldNode()) {
        StoreInstanceFieldNode* initializer =
            initializers->NodeAt(i)->AsStoreInstanceFieldNode();
        if (initializer->field().raw() == field.raw()) {
          found = true;
          break;
        }
      }
    }

    if (found) continue;

    if (field.is_final()) {
      ErrorMsg("final field '%s' not initialized",
               String::Handle(field.name()).ToCString());
    } else {
      field.UpdateCid(kNullCid);
      field.UpdateLength(Field::kNoFixedLength);
    }
  }
}


AstNode* Parser::ParseExternalInitializedField(const Field& field) {
  // Only use this function if the initialized field originates
  // from a different class. We need to save and restore current
  // class, library, and token stream (script).
  ASSERT(current_class().raw() != field.origin());
  const Class& saved_class = Class::Handle(current_class().raw());
  const Library& saved_library = Library::Handle(library().raw());
  const Script& saved_script = Script::Handle(script().raw());
  const intptr_t saved_token_pos = TokenPos();

  set_current_class(Class::Handle(field.origin()));
  set_library(Library::Handle(current_class().library()));
  SetScript(Script::Handle(current_class().script()), field.token_pos());

  ASSERT(IsIdentifier());
  ConsumeToken();
  ExpectToken(Token::kASSIGN);
  AstNode* init_expr = NULL;
  if (field.is_const()) {
    init_expr = ParseConstExpr();
  } else {
    init_expr = ParseExpr(kAllowConst, kConsumeCascades);
    if (init_expr->EvalConstExpr() != NULL) {
      init_expr =
          new LiteralNode(field.token_pos(), EvaluateConstExpr(init_expr));
    }
  }
  set_current_class(saved_class);
  set_library(saved_library);
  SetScript(saved_script, saved_token_pos);
  return init_expr;
}


void Parser::ParseInitializedInstanceFields(const Class& cls,
                 LocalVariable* receiver,
                 GrowableArray<Field*>* initialized_fields) {
  TRACE_PARSER("ParseInitializedInstanceFields");
  const Array& fields = Array::Handle(cls.fields());
  Field& f = Field::Handle();
  const intptr_t saved_pos = TokenPos();
  for (int i = 0; i < fields.Length(); i++) {
    f ^= fields.At(i);
    if (!f.is_static() && f.has_initializer()) {
      Field& field = Field::ZoneHandle();
      field ^= fields.At(i);
      if (field.is_final()) {
        // Final fields with initializer expression may not be initialized
        // again by constructors. Remember that this field is already
        // initialized.
        initialized_fields->Add(&field);
      }
      AstNode* init_expr = NULL;
      if (current_class().raw() != field.origin()) {
        init_expr = ParseExternalInitializedField(field);
      } else {
        SetPosition(field.token_pos());
        ASSERT(IsIdentifier());
        ConsumeToken();
        ExpectToken(Token::kASSIGN);
        if (field.is_const()) {
          init_expr = ParseConstExpr();
        } else {
          init_expr = ParseExpr(kAllowConst, kConsumeCascades);
          if (init_expr->EvalConstExpr() != NULL) {
            init_expr = new LiteralNode(field.token_pos(),
                                        EvaluateConstExpr(init_expr));
          }
        }
      }
      ASSERT(init_expr != NULL);
      AstNode* instance = new LoadLocalNode(field.token_pos(), receiver);
      EnsureExpressionTemp();
      AstNode* field_init =
          new StoreInstanceFieldNode(field.token_pos(),
                                     instance,
                                     field,
                                     init_expr);
      current_block_->statements->Add(field_init);
    }
  }
  SetPosition(saved_pos);
}


void Parser::CheckDuplicateFieldInit(intptr_t init_pos,
                                    GrowableArray<Field*>* initialized_fields,
                                    Field* field) {
  ASSERT(!field->is_static());
  for (int i = 0; i < initialized_fields->length(); i++) {
    Field* initialized_field = (*initialized_fields)[i];
    if (initialized_field->raw() == field->raw()) {
      ErrorMsg(init_pos,
               "duplicate initialization for field %s",
               String::Handle(field->name()).ToCString());
    }
  }
  initialized_fields->Add(field);
}


void Parser::ParseInitializers(const Class& cls,
                               LocalVariable* receiver,
                               GrowableArray<Field*>* initialized_fields) {
  TRACE_PARSER("ParseInitializers");
  bool super_init_seen = false;
  if (CurrentToken() == Token::kCOLON) {
    if ((LookaheadToken(1) == Token::kTHIS) &&
        ((LookaheadToken(2) == Token::kLPAREN) ||
        ((LookaheadToken(2) == Token::kPERIOD) &&
            (LookaheadToken(4) == Token::kLPAREN)))) {
      // Either we see this(...) or this.xxx(...) which is a
      // redirected constructor. We don't need to check whether
      // const fields are initialized. The other constructor will
      // guarantee that.
      ConsumeToken();  // Colon.
      ParseConstructorRedirection(cls, receiver);
      return;
    }
    do {
      ConsumeToken();  // Colon or comma.
      AstNode* init_statement;
      if (CurrentToken() == Token::kSUPER) {
        if (super_init_seen) {
          ErrorMsg("duplicate call to super constructor");
        }
        init_statement = ParseSuperInitializer(cls, receiver);
        super_init_seen = true;
      } else {
        init_statement = ParseInitializer(cls, receiver, initialized_fields);
      }
      current_block_->statements->Add(init_statement);
    } while (CurrentToken() == Token::kCOMMA);
  }
  if (!super_init_seen) {
    // Generate implicit super() if we haven't seen an explicit super call
    // or constructor redirection.
    GenerateSuperConstructorCall(cls, receiver, NULL);
  }
  CheckConstFieldsInitialized(cls);
}


void Parser::ParseConstructorRedirection(const Class& cls,
                                         LocalVariable* receiver) {
  TRACE_PARSER("ParseConstructorRedirection");
  ASSERT(CurrentToken() == Token::kTHIS);
  const intptr_t call_pos = TokenPos();
  ConsumeToken();
  String& ctor_name = String::Handle(cls.Name());

  ctor_name = String::Concat(ctor_name, Symbols::Dot());
  if (CurrentToken() == Token::kPERIOD) {
    ConsumeToken();
    ctor_name = String::Concat(ctor_name,
                               *ExpectIdentifier("constructor name expected"));
  }
  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg("parameter list expected");
  }

  ArgumentListNode* arguments = new ArgumentListNode(call_pos);
  // 'this' parameter is the first argument to constructor.
  AstNode* implicit_argument = new LoadLocalNode(call_pos, receiver);
  arguments->Add(implicit_argument);
  // Construction phase parameter is second argument.
  LocalVariable* phase_param = LookupPhaseParameter();
  ASSERT(phase_param != NULL);
  AstNode* phase_argument = new LoadLocalNode(call_pos, phase_param);
  arguments->Add(phase_argument);
  receiver->set_invisible(true);
  ParseActualParameters(arguments, kAllowConst);
  receiver->set_invisible(false);
  // Resolve the constructor.
  const Function& redirect_ctor = Function::ZoneHandle(
      cls.LookupConstructor(ctor_name));
  if (redirect_ctor.IsNull()) {
    ErrorMsg(call_pos, "constructor '%s' not found", ctor_name.ToCString());
  }
  String& error_message = String::Handle();
  if (!redirect_ctor.AreValidArguments(arguments->length(),
                                       arguments->names(),
                                       &error_message)) {
    ErrorMsg(call_pos,
             "invalid arguments passed to constructor '%s': %s",
             ctor_name.ToCString(),
             error_message.ToCString());
  }
  current_block_->statements->Add(
      new StaticCallNode(call_pos, redirect_ctor, arguments));
}


SequenceNode* Parser::MakeImplicitConstructor(const Function& func) {
  ASSERT(func.IsConstructor());
  ASSERT(func.Owner() == current_class().raw());
  const intptr_t ctor_pos = TokenPos();
  OpenFunctionBlock(func);

  LocalVariable* receiver = new LocalVariable(
      ctor_pos, Symbols::This(), *ReceiverType(current_class()));
  current_block_->scope->AddVariable(receiver);

  LocalVariable* phase_parameter = new LocalVariable(
      ctor_pos, Symbols::PhaseParameter(), Type::ZoneHandle(Type::SmiType()));
  current_block_->scope->AddVariable(phase_parameter);

  // Parse expressions of instance fields that have an explicit
  // initializer expression.
  // The receiver must not be visible to field initializer expressions.
  receiver->set_invisible(true);
  GrowableArray<Field*> initialized_fields;
  ParseInitializedInstanceFields(
      current_class(), receiver, &initialized_fields);
  receiver->set_invisible(false);

  // If the class of this implicit constructor is a mixin application class,
  // it is a forwarding constructor of the mixin. The forwarding
  // constructor initializes the instance fields that have initializer
  // expressions and then calls the respective super constructor with
  // the same name and number of parameters.
  ArgumentListNode* forwarding_args = NULL;
  if (current_class().mixin() != Type::null()) {
    // At this point we don't support forwarding constructors
    // that have optional parameters because we don't know the default
    // values of the optional parameters. We would have to compile the super
    // constructor to get the default values. Also, the spec is not clear
    // whether optional parameters are even allowed in this situation.
    // TODO(hausner): Remove this limitation if the language spec indeed
    // allows optional parameters.
    if (func.HasOptionalParameters()) {
      ErrorMsg(ctor_pos,
               "forwarding constructors must not have optional parameters");
    }

    // Prepare user-defined arguments to be forwarded to super call.
    // The first user-defined argument is at position 2.
    forwarding_args = new ArgumentListNode(ctor_pos);
    for (int i = 2; i < func.NumParameters(); i++) {
      LocalVariable* param = new LocalVariable(
          ctor_pos,
          String::ZoneHandle(func.ParameterNameAt(i)),
          Type::ZoneHandle(Type::DynamicType()));
      current_block_->scope->AddVariable(param);
      forwarding_args->Add(new LoadLocalNode(ctor_pos, param));
    }
  }

  GenerateSuperConstructorCall(current_class(), receiver, forwarding_args);
  CheckConstFieldsInitialized(current_class());

  // Empty constructor body.
  SequenceNode* statements = CloseBlock();
  return statements;
}


// Helper function to make the first num_variables variables in the
// given scope visible/invisible.
static void SetInvisible(LocalScope* scope, int num_variables, bool invisible) {
  ASSERT(num_variables <= scope->num_variables());
  for (int i = 0; i < num_variables; i++) {
    scope->VariableAt(i)->set_invisible(invisible);
  }
}


void Parser::CheckRecursiveInvocation() {
  const GrowableObjectArray& pending_functions =
      GrowableObjectArray::Handle(
          isolate()->object_store()->pending_functions());
  for (int i = 0; i < pending_functions.Length(); i++) {
    if (pending_functions.At(i) == current_function().raw()) {
      const String& fname =
          String::Handle(current_function().UserVisibleName());
      ErrorMsg("circular dependency for function %s", fname.ToCString());
    }
  }
  ASSERT(!unregister_pending_function_);
  pending_functions.Add(current_function());
  unregister_pending_function_ = true;
}


// Parser is at the opening parenthesis of the formal parameter declaration
// of function. Parse the formal parameters, initializers and code.
SequenceNode* Parser::ParseConstructor(const Function& func,
                                       Array& default_parameter_values) {
  TRACE_PARSER("ParseConstructor");
  ASSERT(func.IsConstructor());
  ASSERT(!func.IsFactory());
  ASSERT(!func.is_static());
  ASSERT(!func.IsLocalFunction());
  const Class& cls = Class::Handle(func.Owner());
  ASSERT(!cls.IsNull());

  CheckRecursiveInvocation();

  if (func.IsImplicitConstructor()) {
    // Special case: implicit constructor.
    // The parser adds an implicit default constructor when a class
    // does not have any explicit constructor or factory (see
    // Parser::AddImplicitConstructor).
    // There is no source text to parse. We just build the
    // sequence node by hand.
    return MakeImplicitConstructor(func);
  }

  OpenFunctionBlock(func);
  ParamList params;
  const bool allow_explicit_default_values = true;
  ASSERT(CurrentToken() == Token::kLPAREN);

  // Add implicit receiver parameter which is passed the allocated
  // but uninitialized instance to construct.
  ASSERT(current_class().raw() == func.Owner());
  params.AddReceiver(ReceiverType(current_class()), func.token_pos());

  // Add implicit parameter for construction phase.
  params.AddFinalParameter(
      TokenPos(),
      &Symbols::PhaseParameter(),
      &Type::ZoneHandle(Type::SmiType()));

  if (func.is_const()) {
    params.SetImplicitlyFinal();
  }
  ParseFormalParameterList(allow_explicit_default_values, &params);

  SetupDefaultsForOptionalParams(&params, default_parameter_values);
  ASSERT(AbstractType::Handle(func.result_type()).IsResolved());
  ASSERT(func.NumParameters() == params.parameters->length());

  // Now populate function scope with the formal parameters.
  AddFormalParamsToScope(&params, current_block_->scope);

  // Initialize instance fields that have an explicit initializer expression.
  // The formal parameter names must not be visible to the instance
  // field initializer expressions, yet the parameters must be added to
  // the scope so the expressions use the correct offsets for 'this' when
  // storing values. We make the formal parameters temporarily invisible
  // while parsing the instance field initializer expressions.
  SetInvisible(current_block_->scope, params.parameters->length(), true);
  GrowableArray<Field*> initialized_fields;
  LocalVariable* receiver = current_block_->scope->VariableAt(0);
  OpenBlock();
  ParseInitializedInstanceFields(cls, receiver, &initialized_fields);
  // Make the parameters (which are in the outer scope) visible again.
  SetInvisible(current_block_->scope->parent(),
               params.parameters->length(), false);

  // Turn formal field parameters into field initializers or report error
  // if the function is not a constructor.
  if (params.has_field_initializer) {
    for (int i = 0; i < params.parameters->length(); i++) {
      ParamDesc& param = (*params.parameters)[i];
      if (param.is_field_initializer) {
        const String& field_name = *param.name;
        Field& field = Field::ZoneHandle(cls.LookupInstanceField(field_name));
        if (field.IsNull()) {
          ErrorMsg(param.name_pos,
                   "unresolved reference to instance field '%s'",
                   field_name.ToCString());
        }
        CheckDuplicateFieldInit(param.name_pos, &initialized_fields, &field);
        AstNode* instance = new LoadLocalNode(param.name_pos, receiver);
        LocalVariable* p =
            current_block_->scope->LookupVariable(*param.name, false);
        ASSERT(p != NULL);
        // Initializing formals cannot be used in the explicit initializer
        // list, nor can they be used in the constructor body.
        // Thus, make the parameter invisible.
        p->set_invisible(true);
        AstNode* value = new LoadLocalNode(param.name_pos, p);
        EnsureExpressionTemp();
        AstNode* initializer = new StoreInstanceFieldNode(
            param.name_pos, instance, field, value);
        current_block_->statements->Add(initializer);
      }
    }
  }

  // Now parse the explicit initializer list or constructor redirection.
  ParseInitializers(cls, receiver, &initialized_fields);

  SequenceNode* init_statements = CloseBlock();
  if (init_statements->length() > 0) {
    // Generate guard around the initializer code.
    LocalVariable* phase_param = LookupPhaseParameter();
    AstNode* phase_value = new LoadLocalNode(TokenPos(), phase_param);
    AstNode* phase_check = new BinaryOpNode(
        TokenPos(), Token::kBIT_AND, phase_value,
        new LiteralNode(TokenPos(),
                        Smi::ZoneHandle(Smi::New(Function::kCtorPhaseInit))));
    AstNode* comparison =
        new ComparisonNode(TokenPos(), Token::kNE_STRICT,
                           phase_check,
                           new LiteralNode(TokenPos(),
                                           Smi::ZoneHandle(Smi::New(0))));
    AstNode* guarded_init_statements =
        new IfNode(TokenPos(), comparison, init_statements, NULL);
    current_block_->statements->Add(guarded_init_statements);
  }

  // Parsing of initializers done. Now we parse the constructor body
  // and add the implicit super call to the super constructor's body
  // if necessary.
  StaticCallNode* super_call = NULL;
  // Look for the super initializer call in the sequence of initializer
  // statements. If it exists and is not the last initializer statement,
  // we need to create an implicit super call to the super constructor's
  // body.
  // Thus, iterate over all but the last initializer to see whether
  // it's a super constructor call.
  for (int i = 0; i < init_statements->length() - 1; i++) {
    if (init_statements->NodeAt(i)->IsStaticCallNode()) {
      StaticCallNode* static_call =
      init_statements->NodeAt(i)->AsStaticCallNode();
      if (static_call->function().IsConstructor()) {
        super_call = static_call;
        break;
      }
    }
  }
  if (super_call != NULL) {
    // Generate an implicit call to the super constructor's body.
    // We need to patch the super _initializer_ call so that it
    // saves the evaluated actual arguments in temporary variables.
    // The temporary variables are necessary so that the argument
    // expressions are not evaluated twice.
    ArgumentListNode* ctor_args = super_call->arguments();
    // The super initializer call has at least 2 arguments: the
    // implicit receiver, and the hidden construction phase.
    ASSERT(ctor_args->length() >= 2);
    for (int i = 2; i < ctor_args->length(); i++) {
      AstNode* arg = ctor_args->NodeAt(i);
      if (!IsSimpleLocalOrLiteralNode(arg)) {
        LocalVariable* temp =
            CreateTempConstVariable(arg->token_pos(), "sca");
        AstNode* save_temp = new StoreLocalNode(arg->token_pos(), temp, arg);
        ctor_args->SetNodeAt(i, save_temp);
      }
    }
  }
  OpenBlock();  // Block to collect constructor body nodes.
  intptr_t body_pos = TokenPos();

  // Insert the implicit super call to the super constructor body.
  if (super_call != NULL) {
    ArgumentListNode* initializer_args = super_call->arguments();
    const Function& super_ctor = super_call->function();
    // Patch the initializer call so it only executes the super initializer.
    initializer_args->SetNodeAt(1,
        new LiteralNode(body_pos,
                        Smi::ZoneHandle(Smi::New(Function::kCtorPhaseInit))));

    ArgumentListNode* super_call_args = new ArgumentListNode(body_pos);
    // First argument is the receiver.
    super_call_args->Add(new LoadLocalNode(body_pos, receiver));
    // Second argument is the construction phase argument.
    AstNode* phase_parameter =
        new LiteralNode(body_pos,
                        Smi::ZoneHandle(Smi::New(Function::kCtorPhaseBody)));
    super_call_args->Add(phase_parameter);
    super_call_args->set_names(initializer_args->names());
    for (int i = 2; i < initializer_args->length(); i++) {
      AstNode* arg = initializer_args->NodeAt(i);
      if (arg->IsLiteralNode()) {
        LiteralNode* lit = arg->AsLiteralNode();
        super_call_args->Add(new LiteralNode(body_pos, lit->literal()));
      } else {
        ASSERT(arg->IsLoadLocalNode() || arg->IsStoreLocalNode());
        if (arg->IsLoadLocalNode()) {
          const LocalVariable& temp = arg->AsLoadLocalNode()->local();
          super_call_args->Add(new LoadLocalNode(body_pos, &temp));
        } else if (arg->IsStoreLocalNode()) {
          const LocalVariable& temp = arg->AsStoreLocalNode()->local();
          super_call_args->Add(new LoadLocalNode(body_pos, &temp));
        }
      }
    }
    ASSERT(super_ctor.AreValidArguments(super_call_args->length(),
                                        super_call_args->names(),
                                        NULL));
    current_block_->statements->Add(
        new StaticCallNode(body_pos, super_ctor, super_call_args));
  }

  if (CurrentToken() == Token::kLBRACE) {
    ConsumeToken();
    ParseStatementSequence();
    ExpectToken(Token::kRBRACE);
  } else if (CurrentToken() == Token::kARROW) {
    ErrorMsg("constructors may not return a value");
  } else if (IsLiteral("native")) {
    ErrorMsg("native constructors not supported");
  } else if (CurrentToken() == Token::kSEMICOLON) {
    // Some constructors have no function body.
    ConsumeToken();
  } else {
    UnexpectedToken();
  }

  SequenceNode* ctor_block = CloseBlock();
  if (ctor_block->length() > 0) {
    // Generate guard around the constructor body code.
    LocalVariable* phase_param = LookupPhaseParameter();
    AstNode* phase_value = new LoadLocalNode(body_pos, phase_param);
    AstNode* phase_check =
        new BinaryOpNode(body_pos, Token::kBIT_AND,
            phase_value,
            new LiteralNode(body_pos,
                Smi::ZoneHandle(Smi::New(Function::kCtorPhaseBody))));
    AstNode* comparison =
       new ComparisonNode(body_pos, Token::kNE_STRICT,
                         phase_check,
                         new LiteralNode(body_pos,
                                         Smi::ZoneHandle(Smi::New(0))));
    AstNode* guarded_block_statements =
        new IfNode(body_pos, comparison, ctor_block, NULL);
    current_block_->statements->Add(guarded_block_statements);
  }

  SequenceNode* statements = CloseBlock();
  return statements;
}


// Parser is at the opening parenthesis of the formal parameter
// declaration of the function or constructor.
// Parse the formal parameters and code.
SequenceNode* Parser::ParseFunc(const Function& func,
                                Array& default_parameter_values) {
  TRACE_PARSER("ParseFunc");
  Function& saved_innermost_function =
      Function::Handle(innermost_function().raw());
  innermost_function_ = func.raw();

  // Save current try index. Try index starts at zero for each function.
  intptr_t saved_try_index = last_used_try_index_;
  last_used_try_index_ = 0;

  // TODO(12455) : Need better validation mechanism.

  if (func.IsConstructor()) {
    SequenceNode* statements = ParseConstructor(func, default_parameter_values);
    innermost_function_ = saved_innermost_function.raw();
    last_used_try_index_ = saved_try_index;
    return statements;
  }

  ASSERT(!func.IsConstructor());
  OpenFunctionBlock(func);  // Build local scope for function.

  ParamList params;
  // An instance closure function may capture and access the receiver, but via
  // the context and not via the first formal parameter.
  if (func.IsClosureFunction()) {
    // The first parameter of a closure function is the closure object.
    ASSERT(!func.is_const());  // Closure functions cannot be const.
    params.AddFinalParameter(
        TokenPos(),
        &Symbols::ClosureParameter(),
        &Type::ZoneHandle(Type::DynamicType()));
  } else if (!func.is_static()) {
    // Static functions do not have a receiver.
    ASSERT(current_class().raw() == func.Owner());
    params.AddReceiver(ReceiverType(current_class()), func.token_pos());
  } else if (func.IsFactory()) {
    // The first parameter of a factory is the AbstractTypeArguments vector of
    // the type of the instance to be allocated.
    params.AddFinalParameter(
        TokenPos(),
        &Symbols::TypeArgumentsParameter(),
        &Type::ZoneHandle(Type::DynamicType()));
  }
  ASSERT((CurrentToken() == Token::kLPAREN) || func.IsGetterFunction());
  const bool allow_explicit_default_values = true;
  if (func.IsGetterFunction()) {
    // Populate function scope with the formal parameters. Since in this case
    // we are compiling a getter this will at most populate the receiver.
    AddFormalParamsToScope(&params, current_block_->scope);
  } else {
    ParseFormalParameterList(allow_explicit_default_values, &params);

    // The number of parameters and their type are not yet set in local
    // functions, since they are not 'top-level' parsed.
    if (func.IsLocalFunction()) {
      AddFormalParamsToFunction(&params, func);
    }
    SetupDefaultsForOptionalParams(&params, default_parameter_values);
    ASSERT(AbstractType::Handle(func.result_type()).IsResolved());
    ASSERT(func.NumParameters() == params.parameters->length());

    // Check whether the function has any field initializer formal parameters,
    // which are not allowed in non-constructor functions.
    if (params.has_field_initializer) {
      for (int i = 0; i < params.parameters->length(); i++) {
        ParamDesc& param = (*params.parameters)[i];
        if (param.is_field_initializer) {
          ErrorMsg(param.name_pos,
                   "field initializer only allowed in constructors");
        }
      }
    }
    // Populate function scope with the formal parameters.
    AddFormalParamsToScope(&params, current_block_->scope);

    if (FLAG_enable_type_checks &&
        (current_block_->scope->function_level() > 0)) {
      // We are parsing, but not compiling, a local function.
      // The instantiator may be required at run time for generic type checks.
      if (IsInstantiatorRequired()) {
        // Make sure that the receiver of the enclosing instance function
        // (or implicit first parameter of an enclosing factory) is marked as
        // captured if type checks are enabled, because they may access it to
        // instantiate types.
        CaptureInstantiator();
      }
    }
  }

  OpenBlock();  // Open a nested scope for the outermost function block.
  intptr_t end_token_pos = 0;
  if (CurrentToken() == Token::kLBRACE) {
    ConsumeToken();
    ParseStatementSequence();
    end_token_pos = TokenPos();
    ExpectToken(Token::kRBRACE);
  } else if (CurrentToken() == Token::kARROW) {
    ConsumeToken();
    const intptr_t expr_pos = TokenPos();
    AstNode* expr = ParseExpr(kAllowConst, kConsumeCascades);
    ASSERT(expr != NULL);
    current_block_->statements->Add(new ReturnNode(expr_pos, expr));
    end_token_pos = TokenPos();
  } else if (IsLiteral("native")) {
    ParseNativeFunctionBlock(&params, func);
    end_token_pos = TokenPos();
    ExpectSemicolon();
  } else if (func.is_external()) {
    // Body of an external method contains a single throw.
    const String& function_name = String::ZoneHandle(func.name());
    // TODO(regis): For an instance function, pass the receiver to
    // NoSuchMethodError.
    current_block_->statements->Add(
        ThrowNoSuchMethodError(TokenPos(),
                               current_class(),
                               function_name,
                               NULL,   // No arguments.
                               func.is_static() ?
                                   InvocationMirror::kStatic :
                                   InvocationMirror::kDynamic,
                               InvocationMirror::kMethod));
    end_token_pos = TokenPos();
  } else {
    UnexpectedToken();
  }
  ASSERT(func.end_token_pos() == func.token_pos() ||
         func.end_token_pos() == end_token_pos);
  func.set_end_token_pos(end_token_pos);
  SequenceNode* body = CloseBlock();
  current_block_->statements->Add(body);
  innermost_function_ = saved_innermost_function.raw();
  last_used_try_index_ = saved_try_index;
  return CloseBlock();
}


void Parser::SkipIf(Token::Kind token) {
  if (CurrentToken() == token) {
    ConsumeToken();
  }
}


// Skips tokens up to matching closing parenthesis.
void Parser::SkipToMatchingParenthesis() {
  ASSERT(CurrentToken() == Token::kLPAREN);
  int level = 0;
  do {
    if (CurrentToken() == Token::kLPAREN) {
      level++;
    } else if (CurrentToken() == Token::kRPAREN) {
      level--;
    }
    ConsumeToken();
  } while ((level > 0) && (CurrentToken() != Token::kEOS));
}


void Parser::SkipInitializers() {
  ASSERT(CurrentToken() == Token::kCOLON);
  do {
    ConsumeToken();  // Colon or comma.
    if (CurrentToken() == Token::kSUPER) {
      ConsumeToken();
      if (CurrentToken() == Token::kPERIOD) {
        ConsumeToken();
        ExpectIdentifier("identifier expected");
      }
      if (CurrentToken() != Token::kLPAREN) {
        ErrorMsg("'(' expected");
      }
      SkipToMatchingParenthesis();
    } else {
      SkipIf(Token::kTHIS);
      SkipIf(Token::kPERIOD);
      ExpectIdentifier("identifier expected");
      ExpectToken(Token::kASSIGN);
      SetAllowFunctionLiterals(false);
      SkipExpr();
      SetAllowFunctionLiterals(true);
    }
  } while (CurrentToken() == Token::kCOMMA);
}


void Parser::ParseQualIdent(QualIdent* qual_ident) {
  TRACE_PARSER("ParseQualIdent");
  ASSERT(IsIdentifier());
  ASSERT(!current_class().IsNull());
  qual_ident->ident_pos = TokenPos();
  qual_ident->ident = CurrentLiteral();
  qual_ident->lib_prefix = NULL;
  ConsumeToken();
  if (CurrentToken() == Token::kPERIOD) {
    // An identifier cannot be resolved in a local scope when top level parsing.
    if (is_top_level_ ||
        !ResolveIdentInLocalScope(qual_ident->ident_pos,
                                  *(qual_ident->ident),
                                  NULL)) {
      LibraryPrefix& lib_prefix = LibraryPrefix::ZoneHandle();
      if (current_class().mixin() == Type::null()) {
        lib_prefix = current_class().LookupLibraryPrefix(*(qual_ident->ident));
      } else {
        // TODO(hausner): Should we resolve the prefix via the library scope
        // rather than via the class?
        Class& cls = Class::Handle(parsed_function()->function().origin());
        lib_prefix = cls.LookupLibraryPrefix(*(qual_ident->ident));
      }
      if (!lib_prefix.IsNull()) {
        // We have a library prefix qualified identifier, unless the prefix is
        // shadowed by a type parameter in scope.
        if (current_class().IsNull() ||
            (current_class().LookupTypeParameter(*(qual_ident->ident)) ==
             TypeParameter::null())) {
          ConsumeToken();  // Consume the kPERIOD token.
          qual_ident->lib_prefix = &lib_prefix;
          qual_ident->ident_pos = TokenPos();
          qual_ident->ident =
              ExpectIdentifier("identifier expected after '.'");
        }
      }
    }
  }
}


void Parser::ParseMethodOrConstructor(ClassDesc* members, MemberDesc* method) {
  TRACE_PARSER("ParseMethodOrConstructor");
  ASSERT(CurrentToken() == Token::kLPAREN || method->IsGetter());
  ASSERT(method->type != NULL);
  ASSERT(method->name_pos > 0);
  ASSERT(current_member_ == method);

  if (method->has_var) {
    ErrorMsg(method->name_pos, "keyword var not allowed for methods");
  }
  if (method->has_final) {
    ErrorMsg(method->name_pos, "'final' not allowed for methods");
  }
  if (method->has_abstract && method->has_static) {
    ErrorMsg(method->name_pos,
             "static method '%s' cannot be abstract",
             method->name->ToCString());
  }
  if (method->has_const && !method->IsFactoryOrConstructor()) {
    ErrorMsg(method->name_pos, "'const' not allowed for methods");
  }
  if (method->has_abstract && method->IsFactoryOrConstructor()) {
    ErrorMsg(method->name_pos, "constructor cannot be abstract");
  }
  if (method->has_const && method->IsConstructor()) {
    current_class().set_is_const();
  }

  // Parse the formal parameters.
  const bool are_implicitly_final = method->has_const;
  const bool allow_explicit_default_values = true;
  const intptr_t formal_param_pos = TokenPos();
  method->params.Clear();
  // Static functions do not have a receiver.
  // The first parameter of a factory is the AbstractTypeArguments vector of
  // the type of the instance to be allocated.
  if (!method->has_static || method->IsConstructor()) {
    method->params.AddReceiver(ReceiverType(current_class()), formal_param_pos);
  } else if (method->IsFactory()) {
    method->params.AddFinalParameter(
        formal_param_pos,
        &Symbols::TypeArgumentsParameter(),
        &Type::ZoneHandle(Type::DynamicType()));
  }
  // Constructors have an implicit parameter for the construction phase.
  if (method->IsConstructor()) {
    method->params.AddFinalParameter(
        TokenPos(),
        &Symbols::PhaseParameter(),
        &Type::ZoneHandle(Type::SmiType()));
  }
  if (are_implicitly_final) {
    method->params.SetImplicitlyFinal();
  }
  if (!method->IsGetter()) {
    ParseFormalParameterList(allow_explicit_default_values, &method->params);
  }

  // Now that we know the parameter list, we can distinguish between the
  // unary and binary operator -.
  if (method->has_operator) {
    if ((method->operator_token == Token::kSUB) &&
       (method->params.num_fixed_parameters == 1)) {
      // Patch up name for unary operator - so it does not clash with the
      // name for binary operator -.
      method->operator_token = Token::kNEGATE;
      *method->name = Symbols::New(Token::Str(Token::kNEGATE));
    }
    CheckOperatorArity(*method);
  }

  if (members->FunctionNameExists(*method->name, method->kind)) {
    ErrorMsg(method->name_pos,
             "field or method '%s' already defined", method->name->ToCString());
  }

  // Mangle the name for getter and setter functions and check function
  // arity.
  if (method->IsGetter() || method->IsSetter()) {
    int expected_num_parameters = 0;
    if (method->IsGetter()) {
      expected_num_parameters = (method->has_static) ? 0 : 1;
      method->name = &String::ZoneHandle(Field::GetterSymbol(*method->name));
    } else {
      ASSERT(method->IsSetter());
      expected_num_parameters = (method->has_static) ? 1 : 2;
      method->name = &String::ZoneHandle(Field::SetterSymbol(*method->name));
    }
    if ((method->params.num_fixed_parameters != expected_num_parameters) ||
        (method->params.num_optional_parameters != 0)) {
      ErrorMsg(method->name_pos, "illegal %s parameters",
               method->IsGetter() ? "getter" : "setter");
    }
  }

  // Parse redirecting factory constructor.
  Type& redirection_type = Type::Handle();
  String& redirection_identifier = String::Handle();
  if (method->IsFactory() && (CurrentToken() == Token::kASSIGN)) {
    ConsumeToken();
    const intptr_t type_pos = TokenPos();
    const AbstractType& type = AbstractType::Handle(
        ParseType(ClassFinalizer::kResolveTypeParameters));
    if (!type.IsMalformed() && type.IsTypeParameter()) {
      // Replace the type with a malformed type and compile a throw when called.
      redirection_type = ClassFinalizer::NewFinalizedMalformedType(
          Error::Handle(),  // No previous error.
          current_class(),
          type_pos,
          "factory '%s' may not redirect to type parameter '%s'",
          method->name->ToCString(),
          String::Handle(type.UserVisibleName()).ToCString());
    } else {
      // TODO(regis): What if the redirection type is malbounded?
      redirection_type ^= type.raw();
    }
    if (CurrentToken() == Token::kPERIOD) {
      // Named constructor or factory.
      ConsumeToken();
      redirection_identifier = ExpectIdentifier("identifier expected")->raw();
    }
  } else if (CurrentToken() == Token::kCOLON) {
    // Parse initializers.
    if (!method->IsConstructor()) {
      ErrorMsg("initializers only allowed on constructors");
    }
    if ((LookaheadToken(1) == Token::kTHIS) &&
        ((LookaheadToken(2) == Token::kLPAREN) ||
         LookaheadToken(4) == Token::kLPAREN)) {
      // Redirected constructor: either this(...) or this.xxx(...).
      if (method->params.has_field_initializer) {
        // Constructors that redirect to another constructor must not
        // initialize any fields using field initializer parameters.
        ErrorMsg(formal_param_pos, "Redirecting constructor "
                 "may not use field initializer parameters");
      }
      ConsumeToken();  // Colon.
      ExpectToken(Token::kTHIS);
      String& redir_name = String::ZoneHandle(
          String::Concat(members->class_name(), Symbols::Dot()));
      if (CurrentToken() == Token::kPERIOD) {
        ConsumeToken();
        redir_name = String::Concat(redir_name,
            *ExpectIdentifier("constructor name expected"));
      }
      method->redirect_name = &redir_name;
      if (CurrentToken() != Token::kLPAREN) {
        ErrorMsg("'(' expected");
      }
      SkipToMatchingParenthesis();
    } else {
      SkipInitializers();
    }
  }

  // Only constructors can redirect to another method.
  ASSERT((method->redirect_name == NULL) || method->IsConstructor());

  intptr_t method_end_pos = TokenPos();
  if ((CurrentToken() == Token::kLBRACE) ||
      (CurrentToken() == Token::kARROW)) {
    if (method->has_abstract) {
      ErrorMsg(method->name_pos,
               "abstract method '%s' may not have a function body",
               method->name->ToCString());
    } else if (method->has_external) {
      ErrorMsg(method->name_pos,
               "external method '%s' may not have a function body",
               method->name->ToCString());
    } else if (method->IsFactoryOrConstructor() && method->has_const) {
      ErrorMsg(method->name_pos,
               "const constructor or factory '%s' may not have a function body",
               method->name->ToCString());
    }
    if (method->redirect_name != NULL) {
      ErrorMsg(method->name_pos,
               "Constructor with redirection may not have a function body");
    }
    if (CurrentToken() == Token::kLBRACE) {
      SkipBlock();
    } else {
      ConsumeToken();
      SkipExpr();
      ExpectSemicolon();
    }
    method_end_pos = TokenPos() - 1;
  } else if (IsLiteral("native")) {
    if (method->has_abstract) {
      ErrorMsg(method->name_pos,
               "abstract method '%s' may not have a function body",
               method->name->ToCString());
    } else if (method->IsFactoryOrConstructor() && method->has_const) {
      ErrorMsg(method->name_pos,
               "const constructor or factory '%s' may not be native",
               method->name->ToCString());
    }
    if (method->redirect_name != NULL) {
      ErrorMsg(method->name_pos,
               "Constructor with redirection may not have a function body");
    }
    ParseNativeDeclaration();
    method_end_pos = TokenPos();
    ExpectSemicolon();
  } else {
    // We haven't found a method body. Issue error if one is required.
    const bool must_have_body =
        method->has_static &&
        !method->has_external &&
        redirection_type.IsNull();
    if (must_have_body) {
      ErrorMsg(method->name_pos,
               "function body expected for method '%s'",
               method->name->ToCString());
    }

    if (CurrentToken() == Token::kSEMICOLON) {
      ConsumeToken();
      if (!method->has_static &&
          !method->has_external &&
          !method->IsConstructor()) {
          // Methods, getters and setters without a body are
          // implicitly abstract.
        method->has_abstract = true;
      }
    } else {
      // Signature is not followed by semicolon or body. Issue an
      // appropriate error.
      const bool must_have_semicolon =
          (method->redirect_name != NULL) ||
          (method->IsConstructor() && method->has_const) ||
          method->has_external;
      if (must_have_semicolon) {
        ExpectSemicolon();
      } else {
        ErrorMsg(method->name_pos,
                 "function body or semicolon expected for method '%s'",
                 method->name->ToCString());
      }
    }
  }

  RawFunction::Kind function_kind;
  if (method->IsFactoryOrConstructor()) {
    function_kind = RawFunction::kConstructor;
  } else if (method->IsGetter()) {
    function_kind = RawFunction::kGetterFunction;
  } else if (method->IsSetter()) {
    function_kind = RawFunction::kSetterFunction;
  } else {
    function_kind = RawFunction::kRegularFunction;
  }
  Function& func = Function::Handle(
      Function::New(*method->name,
                    function_kind,
                    method->has_static,
                    method->has_const,
                    method->has_abstract,
                    method->has_external,
                    current_class(),
                    method->decl_begin_pos));
  func.set_result_type(*method->type);
  func.set_end_token_pos(method_end_pos);
  if (method->metadata_pos > 0) {
    library_.AddFunctionMetadata(func, method->metadata_pos);
  }

  // If this method is a redirecting factory, set the redirection information.
  if (!redirection_type.IsNull()) {
    ASSERT(func.IsFactory());
    func.SetRedirectionType(redirection_type);
    if (!redirection_identifier.IsNull()) {
      func.SetRedirectionIdentifier(redirection_identifier);
    }
  }

  // No need to resolve parameter types yet, or add parameters to local scope.
  ASSERT(is_top_level_);
  AddFormalParamsToFunction(&method->params, func);
  members->AddFunction(func);
}


void Parser::ParseFieldDefinition(ClassDesc* members, MemberDesc* field) {
  TRACE_PARSER("ParseFieldDefinition");
  // The parser has read the first field name and is now at the token
  // after the field name.
  ASSERT(CurrentToken() == Token::kSEMICOLON ||
         CurrentToken() == Token::kCOMMA ||
         CurrentToken() == Token::kASSIGN);
  ASSERT(field->type != NULL);
  ASSERT(field->name_pos > 0);
  ASSERT(current_member_ == field);
  // All const fields are also final.
  ASSERT(!field->has_const || field->has_final);

  if (field->has_abstract) {
    ErrorMsg("keyword 'abstract' not allowed in field declaration");
  }
  if (field->has_external) {
    ErrorMsg("keyword 'external' not allowed in field declaration");
  }
  if (field->has_factory) {
    ErrorMsg("keyword 'factory' not allowed in field declaration");
  }
  if (members->FieldNameExists(*field->name, !field->has_final)) {
    ErrorMsg(field->name_pos,
             "field or method '%s' already defined", field->name->ToCString());
  }
  Function& getter = Function::Handle();
  Function& setter = Function::Handle();
  Field& class_field = Field::Handle();
  Instance& init_value = Instance::Handle();
  while (true) {
    bool has_initializer = CurrentToken() == Token::kASSIGN;
    bool has_simple_literal = false;
    if (has_initializer) {
      ConsumeToken();
      init_value = Object::sentinel().raw();
      // For static const fields and static final non-const fields, the
      // initialization expression will be parsed through the
      // kImplicitStaticFinalGetter method invocation/compilation.
      // For instance fields, the expression is parsed when a constructor
      // is compiled.
      // For static const fields and static final non-const fields with very
      // simple initializer expressions (e.g. a literal number or string), we
      // optimize away the kImplicitStaticFinalGetter and initialize the field
      // here. However, the class finalizer will check the value type for
      // assignability once the declared field type can be resolved. If the
      // value is not assignable (assuming checked mode and disregarding actual
      // mode), the field value is reset and a kImplicitStaticFinalGetter is
      // created at finalization time.

      if (field->has_static && (field->has_const || field->has_final) &&
          (LookaheadToken(1) == Token::kSEMICOLON)) {
        has_simple_literal = IsSimpleLiteral(*field->type, &init_value);
      }
      SkipExpr();
    } else {
      if (field->has_const || (field->has_static && field->has_final)) {
        ErrorMsg(field->name_pos,
                 "%s%s field '%s' must have an initializer expression",
                 field->has_static ? "static " : "",
                 field->has_const ? "const" : "final",
                 field->name->ToCString());
      }
    }

    // Create the field object.
    class_field = Field::New(*field->name,
                             field->has_static,
                             field->has_final,
                             field->has_const,
                             current_class(),
                             field->name_pos);
    class_field.set_type(*field->type);
    class_field.set_has_initializer(has_initializer);
    members->AddField(class_field);
    if (field->metadata_pos >= 0) {
      library_.AddFieldMetadata(class_field, field->metadata_pos);
    }

    // For static final fields (this includes static const fields), set value to
    // "uninitialized" and create a kFinalImplicitGetter getter method.
    if (field->has_static && has_initializer) {
      class_field.set_value(init_value);
      if (!has_simple_literal) {
        String& getter_name = String::Handle(Field::GetterSymbol(*field->name));
        getter = Function::New(getter_name,
                               RawFunction::kImplicitStaticFinalGetter,
                               field->has_static,
                               field->has_const,
                               /* is_abstract = */ false,
                               /* is_external = */ false,
                               current_class(),
                               field->name_pos);
        getter.set_result_type(*field->type);
        members->AddFunction(getter);
      }
    }

    // For instance fields, we create implicit getter and setter methods.
    if (!field->has_static) {
      String& getter_name = String::Handle(Field::GetterSymbol(*field->name));
      getter = Function::New(getter_name, RawFunction::kImplicitGetter,
                             field->has_static,
                             field->has_final,
                             /* is_abstract = */ false,
                             /* is_external = */ false,
                             current_class(),
                             field->name_pos);
      ParamList params;
      ASSERT(current_class().raw() == getter.Owner());
      params.AddReceiver(ReceiverType(current_class()), field->name_pos);
      getter.set_result_type(*field->type);
      AddFormalParamsToFunction(&params, getter);
      members->AddFunction(getter);
      if (!field->has_final) {
        // Build a setter accessor for non-const fields.
        String& setter_name = String::Handle(Field::SetterSymbol(*field->name));
        setter = Function::New(setter_name, RawFunction::kImplicitSetter,
                               field->has_static,
                               field->has_final,
                               /* is_abstract = */ false,
                               /* is_external = */ false,
                               current_class(),
                               field->name_pos);
        ParamList params;
        ASSERT(current_class().raw() == setter.Owner());
        params.AddReceiver(ReceiverType(current_class()), field->name_pos);
        params.AddFinalParameter(TokenPos(),
                                 &Symbols::Value(),
                                 field->type);
        setter.set_result_type(Type::Handle(Type::VoidType()));
        AddFormalParamsToFunction(&params, setter);
        members->AddFunction(setter);
      }
    }

    if (CurrentToken() != Token::kCOMMA) {
      break;
    }
    ConsumeToken();
    field->name_pos = this->TokenPos();
    field->name = ExpectIdentifier("field name expected");
  }
  ExpectSemicolon();
}


void Parser::CheckOperatorArity(const MemberDesc& member) {
  intptr_t expected_num_parameters;  // Includes receiver.
  Token::Kind op = member.operator_token;
  if (op == Token::kASSIGN_INDEX) {
    expected_num_parameters = 3;
  } else if ((op == Token::kBIT_NOT) || (op == Token::kNEGATE)) {
    expected_num_parameters = 1;
  } else {
    expected_num_parameters = 2;
  }
  if ((member.params.num_optional_parameters > 0) ||
      member.params.has_optional_positional_parameters ||
      member.params.has_optional_named_parameters ||
      (member.params.num_fixed_parameters != expected_num_parameters)) {
    // Subtract receiver when reporting number of expected arguments.
    ErrorMsg(member.name_pos, "operator %s expects %" Pd " argument(s)",
        member.name->ToCString(), (expected_num_parameters - 1));
  }
}


void Parser::ParseClassMemberDefinition(ClassDesc* members,
                                        intptr_t metadata_pos) {
  TRACE_PARSER("ParseClassMemberDefinition");
  MemberDesc member;
  current_member_ = &member;
  member.metadata_pos = metadata_pos;
  member.decl_begin_pos = TokenPos();
  if ((CurrentToken() == Token::kEXTERNAL) &&
      (LookaheadToken(1) != Token::kLPAREN)) {
    ConsumeToken();
    member.has_external = true;
  }
  if ((CurrentToken() == Token::kSTATIC) &&
      (LookaheadToken(1) != Token::kLPAREN)) {
    ConsumeToken();
    member.has_static = true;
  }
  if (CurrentToken() == Token::kCONST) {
    ConsumeToken();
    member.has_const = true;
  } else if (CurrentToken() == Token::kFINAL) {
    ConsumeToken();
    member.has_final = true;
  }
  if (CurrentToken() == Token::kVAR) {
    if (member.has_const) {
      ErrorMsg("identifier expected after 'const'");
    }
    if (member.has_final) {
      ErrorMsg("identifier expected after 'final'");
    }
    ConsumeToken();
    member.has_var = true;
    // The member type is the 'dynamic' type.
    member.type = &Type::ZoneHandle(Type::DynamicType());
  } else if (CurrentToken() == Token::kFACTORY) {
    ConsumeToken();
    if (member.has_static) {
      ErrorMsg("factory method cannot be explicitly marked static");
    }
    member.has_factory = true;
    member.has_static = true;
    // The result type depends on the name of the factory method.
  }
  // Optionally parse a type.
  if (CurrentToken() == Token::kVOID) {
    if (member.has_var || member.has_factory) {
      ErrorMsg("void not expected");
    }
    ConsumeToken();
    ASSERT(member.type == NULL);
    member.type = &Type::ZoneHandle(Type::VoidType());
  } else if (CurrentToken() == Token::kIDENT) {
    // This is either a type name or the name of a method/constructor/field.
    if ((member.type == NULL) && !member.has_factory) {
      // We have not seen a member type yet, so we check if the next
      // identifier could represent a type before parsing it.
      Token::Kind follower = LookaheadToken(1);
      // We have an identifier followed by a 'follower' token.
      // We either parse a type or assume that no type is specified.
      if ((follower == Token::kLT) ||  // Parameterized type.
          (follower == Token::kGET) ||  // Getter following a type.
          (follower == Token::kSET) ||  // Setter following a type.
          (follower == Token::kOPERATOR) ||  // Operator following a type.
          (Token::IsIdentifier(follower)) ||  // Member name following a type.
          ((follower == Token::kPERIOD) &&    // Qualified class name of type,
           (LookaheadToken(3) != Token::kLPAREN))) {  // but not a named constr.
        ASSERT(is_top_level_);
        // The declared type of fields is never ignored, even in unchecked mode,
        // because getters and setters could be closurized at some time (not
        // supported yet).
        member.type = &AbstractType::ZoneHandle(
            ParseType(ClassFinalizer::kResolveTypeParameters));
      }
    }
  }

  // Optionally parse a (possibly named) constructor name or factory.
  if (IsIdentifier() &&
      (CurrentLiteral()->Equals(members->class_name()) || member.has_factory)) {
    member.name_pos = TokenPos();
    member.name = CurrentLiteral();  // Unqualified identifier.
    ConsumeToken();
    if (member.has_factory) {
      // The factory name may be qualified, but the first identifier must match
      // the name of the immediately enclosing class.
      if (!member.name->Equals(members->class_name())) {
        ErrorMsg(member.name_pos, "factory name must be '%s'",
                 members->class_name().ToCString());
      }
    } else if (member.has_static) {
      ErrorMsg(member.name_pos, "constructor cannot be static");
    }
    if (member.type != NULL) {
      ErrorMsg(member.name_pos, "constructor must not specify return type");
    }
    // Do not bypass class resolution by using current_class() directly, since
    // it may be a patch class.
    const Object& result_type_class = Object::Handle(
        UnresolvedClass::New(LibraryPrefix::Handle(),
                             *member.name,
                             member.name_pos));
    // The type arguments of the result type are the type parameters of the
    // current class. Note that in the case of a patch class, they are copied
    // from the class being patched.
    member.type = &Type::ZoneHandle(Type::New(
        result_type_class,
        TypeArguments::Handle(current_class().type_parameters()),
        member.name_pos));

    // We must be dealing with a constructor or named constructor.
    member.kind = RawFunction::kConstructor;
    *member.name = String::Concat(*member.name, Symbols::Dot());
    if (CurrentToken() == Token::kPERIOD) {
      // Named constructor.
      ConsumeToken();
      member.constructor_name = ExpectIdentifier("identifier expected");
      *member.name = String::Concat(*member.name, *member.constructor_name);
    }
    // Ensure that names are symbols.
    *member.name = Symbols::New(*member.name);

    if (CurrentToken() != Token::kLPAREN) {
      ErrorMsg("left parenthesis expected");
    }
  } else if ((CurrentToken() == Token::kGET) && !member.has_var &&
             (LookaheadToken(1) != Token::kLPAREN) &&
             (LookaheadToken(1) != Token::kASSIGN) &&
             (LookaheadToken(1) != Token::kCOMMA)  &&
             (LookaheadToken(1) != Token::kSEMICOLON)) {
    ConsumeToken();
    member.kind = RawFunction::kGetterFunction;
    member.name_pos = this->TokenPos();
    member.name = ExpectIdentifier("identifier expected");
    // If the result type was not specified, it will be set to DynamicType.
  } else if ((CurrentToken() == Token::kSET) && !member.has_var &&
             (LookaheadToken(1) != Token::kLPAREN) &&
             (LookaheadToken(1) != Token::kASSIGN) &&
             (LookaheadToken(1) != Token::kCOMMA)  &&
             (LookaheadToken(1) != Token::kSEMICOLON))  {
    ConsumeToken();
    member.kind = RawFunction::kSetterFunction;
    member.name_pos = this->TokenPos();
    member.name = ExpectIdentifier("identifier expected");
    if (CurrentToken() != Token::kLPAREN) {
      ErrorMsg("'(' expected");
    }
    // The grammar allows a return type, so member.type is not always NULL here.
    // If no return type is specified, the return type of the setter is dynamic.
    if (member.type == NULL) {
      member.type = &Type::ZoneHandle(Type::DynamicType());
    }
  } else if ((CurrentToken() == Token::kOPERATOR) && !member.has_var &&
             (LookaheadToken(1) != Token::kLPAREN) &&
             (LookaheadToken(1) != Token::kASSIGN) &&
             (LookaheadToken(1) != Token::kCOMMA)  &&
             (LookaheadToken(1) != Token::kSEMICOLON)) {
    ConsumeToken();
    if (!Token::CanBeOverloaded(CurrentToken())) {
      ErrorMsg("invalid operator overloading");
    }
    if (member.has_static) {
      ErrorMsg("operator overloading functions cannot be static");
    }
    member.operator_token = CurrentToken();
    member.has_operator = true;
    member.kind = RawFunction::kRegularFunction;
    member.name_pos = this->TokenPos();
    member.name =
        &String::ZoneHandle(Symbols::New(Token::Str(member.operator_token)));
    ConsumeToken();
  } else if (IsIdentifier()) {
    member.name = CurrentLiteral();
    member.name_pos = TokenPos();
    ConsumeToken();
  } else {
    ErrorMsg("identifier expected");
  }

  ASSERT(member.name != NULL);
  if (CurrentToken() == Token::kLPAREN || member.IsGetter()) {
    // Constructor or method.
    if (member.type == NULL) {
      member.type = &Type::ZoneHandle(Type::DynamicType());
    }
    ASSERT(member.IsFactory() == member.has_factory);
    ParseMethodOrConstructor(members, &member);
  } else if (CurrentToken() ==  Token::kSEMICOLON ||
             CurrentToken() == Token::kCOMMA ||
             CurrentToken() == Token::kASSIGN) {
    // Field definition.
    if (member.has_const) {
      // const fields are implicitly final.
      member.has_final = true;
    }
    if (member.type == NULL) {
      if (member.has_final) {
        member.type = &Type::ZoneHandle(Type::DynamicType());
      } else {
        ErrorMsg("missing 'var', 'final', 'const' or type"
                 " in field declaration");
      }
    }
    ParseFieldDefinition(members, &member);
  } else {
    UnexpectedToken();
  }
  current_member_ = NULL;
  members->AddMember(member);
}


void Parser::ParseClassDeclaration(const GrowableObjectArray& pending_classes,
                                   intptr_t metadata_pos) {
  TRACE_PARSER("ParseClassDeclaration");
  bool is_patch = false;
  bool is_abstract = false;
  if (is_patch_source() &&
      (CurrentToken() == Token::kIDENT) &&
      CurrentLiteral()->Equals("patch")) {
    ConsumeToken();
    is_patch = true;
  } else if (CurrentToken() == Token::kABSTRACT) {
    is_abstract = true;
    ConsumeToken();
  }
  ExpectToken(Token::kCLASS);
  const intptr_t classname_pos = TokenPos();
  String& class_name = *ExpectUserDefinedTypeIdentifier("class name expected");
  if (FLAG_trace_parser) {
    OS::Print("TopLevel parsing class '%s'\n", class_name.ToCString());
  }
  Class& cls = Class::Handle();
  TypeArguments& orig_type_parameters = TypeArguments::Handle();
  Object& obj = Object::Handle(library_.LookupLocalObject(class_name));
  if (obj.IsNull()) {
    if (is_patch) {
      ErrorMsg(classname_pos, "missing class '%s' cannot be patched",
               class_name.ToCString());
    }
    cls = Class::New(class_name, script_, classname_pos);
    library_.AddClass(cls);
  } else {
    if (!obj.IsClass()) {
      ErrorMsg(classname_pos, "'%s' is already defined",
               class_name.ToCString());
    }
    cls ^= obj.raw();
    if (is_patch) {
      // Preserve and reuse the original type parameters and bounds since the
      // ones defined in the patch class will not be finalized.
      orig_type_parameters = cls.type_parameters();
      // A patch class must be given the same name as the class it is patching,
      // otherwise the generic signature classes it defines will not match the
      // patched generic signature classes. Therefore, new signature classes
      // will be introduced and the original ones will not get finalized.
      cls = Class::New(class_name, script_, classname_pos);
      cls.set_library(library_);
    } else {
      // Not patching a class, but it has been found. This must be one of the
      // pre-registered classes from object.cc or a duplicate definition.
      if (!(cls.is_prefinalized() ||
            RawObject::IsTypedDataViewClassId(cls.id()))) {
        ErrorMsg(classname_pos, "class '%s' is already defined",
                 class_name.ToCString());
      }
      // Pre-registered classes need their scripts connected at this time.
      cls.set_script(script_);
      cls.set_token_pos(classname_pos);
    }
  }
  ASSERT(!cls.IsNull());
  ASSERT(cls.functions() == Object::empty_array().raw());
  set_current_class(cls);
  ParseTypeParameters(cls);
  if (is_patch) {
    // Check that the new type parameters are identical to the original ones.
    const TypeArguments& new_type_parameters =
        TypeArguments::Handle(cls.type_parameters());
    const int new_type_params_count =
        new_type_parameters.IsNull() ? 0 : new_type_parameters.Length();
    const int orig_type_params_count =
        orig_type_parameters.IsNull() ? 0 : orig_type_parameters.Length();
    if (new_type_params_count != orig_type_params_count) {
      ErrorMsg(classname_pos,
               "class '%s' must be patched with identical type parameters",
               class_name.ToCString());
    }
    TypeParameter& new_type_param = TypeParameter::Handle();
    TypeParameter& orig_type_param = TypeParameter::Handle();
    String& new_name = String::Handle();
    String& orig_name = String::Handle();
    for (int i = 0; i < new_type_params_count; i++) {
      new_type_param ^= new_type_parameters.TypeAt(i);
      orig_type_param ^= orig_type_parameters.TypeAt(i);
      new_name = new_type_param.name();
      orig_name = orig_type_param.name();
      if (!new_name.Equals(orig_name)) {
        ErrorMsg(new_type_param.token_pos(),
                 "type parameter '%s' of patch class '%s' does not match "
                 "original type parameter '%s'",
                 new_name.ToCString(),
                 class_name.ToCString(),
                 orig_name.ToCString());
      }
      // We do not check that the bounds are repeated. We use the original ones.
      // TODO(regis): Should we check?
    }
    cls.set_type_parameters(orig_type_parameters);
  }
  AbstractType& super_type = Type::Handle();
  if (CurrentToken() == Token::kEXTENDS) {
    ConsumeToken();
    const intptr_t type_pos = TokenPos();
    super_type = ParseType(ClassFinalizer::kResolveTypeParameters);
    if (super_type.IsTypeParameter()) {
      ErrorMsg(type_pos,
               "class '%s' may not extend type parameter '%s'",
               class_name.ToCString(),
               String::Handle(super_type.UserVisibleName()).ToCString());
    }
    if (CurrentToken() == Token::kWITH) {
      super_type = ParseMixins(super_type);
    }
  } else {
    // No extends clause: implicitly extend Object, unless Object itself.
    if (!cls.IsObjectClass()) {
      super_type = Type::ObjectType();
    }
  }
  ASSERT(!super_type.IsNull() || cls.IsObjectClass());
  cls.set_super_type(super_type);

  if (CurrentToken() == Token::kIMPLEMENTS) {
    ParseInterfaceList(cls);
  }

  if (is_abstract) {
    cls.set_is_abstract();
  }
  if (is_patch) {
    // Apply the changes to the patched class looked up above.
    ASSERT(obj.raw() == library_.LookupLocalObject(class_name));
    // The patched class must not be finalized yet.
    const Class& orig_class = Class::Cast(obj);
    ASSERT(!orig_class.is_finalized());
    orig_class.set_patch_class(cls);
    cls.set_is_patch();
  }
  pending_classes.Add(cls, Heap::kOld);
  if (metadata_pos >= 0) {
    library_.AddClassMetadata(cls, metadata_pos);
  }

  if (CurrentToken() != Token::kLBRACE) {
    ErrorMsg("{ expected");
  }
  SkipBlock();
}


void Parser::ParseClassDefinition(const Class& cls) {
  TRACE_PARSER("ParseClassDefinition");
  set_current_class(cls);
  is_top_level_ = true;
  String& class_name = String::Handle(cls.Name());
  const intptr_t class_pos = TokenPos();
  ClassDesc members(cls, class_name, false, class_pos);
  while (CurrentToken() != Token::kLBRACE) {
    ConsumeToken();
  }
  ExpectToken(Token::kLBRACE);
  while (CurrentToken() != Token::kRBRACE) {
    intptr_t metadata_pos = SkipMetadata();
    ParseClassMemberDefinition(&members, metadata_pos);
  }
  ExpectToken(Token::kRBRACE);

  CheckConstructors(&members);

  // Need to compute this here since MakeArray() will clear the
  // functions array in members.
  const bool need_implicit_constructor =
      !members.has_constructor() && !cls.is_patch();

  Array& array = Array::Handle();
  array = Array::MakeArray(members.fields());
  cls.SetFields(array);

  // Creating a new array for functions marks the class as parsed.
  array = Array::MakeArray(members.functions());
  cls.SetFunctions(array);

  // Add an implicit constructor if no explicit constructor is present.
  // No implicit constructors are needed for patch classes.
  if (need_implicit_constructor) {
    AddImplicitConstructor(cls);
  }

  if (cls.is_patch()) {
    // Apply the changes to the patched class looked up above.
    Object& obj = Object::Handle(library_.LookupLocalObject(class_name));
    // The patched class must not be finalized yet.
    const Class& orig_class = Class::Cast(obj);
    ASSERT(!orig_class.is_finalized());
    Error& error = Error::Handle();
    if (!orig_class.ApplyPatch(cls, &error)) {
      AppendErrorMsg(error, class_pos, "applying patch failed");
    }
  }
}


// Add an implicit constructor to the given class.
void Parser::AddImplicitConstructor(const Class& cls) {
  // The implicit constructor is unnamed, has no explicit parameter.
  String& ctor_name = String::ZoneHandle(cls.Name());
  ctor_name = String::Concat(ctor_name, Symbols::Dot());
  ctor_name = Symbols::New(ctor_name);
  // To indicate that this is an implicit constructor, we set the
  // token position and end token position of the function
  // to the token position of the class.
  Function& ctor = Function::Handle(
      Function::New(ctor_name,
                    RawFunction::kConstructor,
                    /* is_static = */ false,
                    /* is_const = */ false,
                    /* is_abstract = */ false,
                    /* is_external = */ false,
                    cls,
                    cls.token_pos()));
  ctor.set_end_token_pos(ctor.token_pos());

  ParamList params;
  // Add implicit 'this' parameter.
  const AbstractType* receiver_type = ReceiverType(cls);
  params.AddReceiver(receiver_type, cls.token_pos());
  // Add implicit parameter for construction phase.
  params.AddFinalParameter(cls.token_pos(),
                           &Symbols::PhaseParameter(),
                           &Type::ZoneHandle(Type::SmiType()));

  AddFormalParamsToFunction(&params, ctor);
  // The body of the constructor cannot modify the type of the constructed
  // instance, which is passed in as the receiver.
  ctor.set_result_type(*receiver_type);
  cls.AddFunction(ctor);
}


// Check for cycles in constructor redirection. Also check whether a
// named constructor collides with the name of another class member.
void Parser::CheckConstructors(ClassDesc* class_desc) {
  // Check for cycles in constructor redirection.
  const GrowableArray<MemberDesc>& members = class_desc->members();
  for (int i = 0; i < members.length(); i++) {
    MemberDesc* member = &members[i];

    if (member->constructor_name != NULL) {
      // Check whether constructor name conflicts with a member name.
      if (class_desc->FunctionNameExists(
          *member->constructor_name, member->kind)) {
        ErrorMsg(member->name_pos,
                 "Named constructor '%s' conflicts with method or field '%s'",
                 member->name->ToCString(),
                 member->constructor_name->ToCString());
      }
    }

    GrowableArray<MemberDesc*> ctors;
    while ((member != NULL) && (member->redirect_name != NULL)) {
      ASSERT(member->IsConstructor());
      // Check whether we have already seen this member.
      for (int i = 0; i < ctors.length(); i++) {
        if (ctors[i] == member) {
          ErrorMsg(member->name_pos,
                   "cyclic reference in constructor redirection");
        }
      }
      // We haven't seen this member. Add it to the list and follow
      // the next redirection. If we can't find the constructor to
      // which the current one redirects, we ignore the unresolved
      // reference. We'll catch it later when the constructor gets
      // compiled.
      ctors.Add(member);
      member = class_desc->LookupMember(*member->redirect_name);
    }
  }
}


void Parser::ParseMixinTypedef(const GrowableObjectArray& pending_classes,
                               intptr_t metadata_pos) {
  TRACE_PARSER("ParseMixinTypedef");
  const intptr_t classname_pos = TokenPos();
  String& class_name = *ExpectUserDefinedTypeIdentifier("class name expected");
  if (FLAG_trace_parser) {
    OS::Print("toplevel parsing typedef class '%s'\n", class_name.ToCString());
  }
  const Object& obj = Object::Handle(library_.LookupLocalObject(class_name));
  if (!obj.IsNull()) {
    ErrorMsg(classname_pos, "'%s' is already defined",
             class_name.ToCString());
  }
  const Class& mixin_application =
      Class::Handle(Class::New(class_name, script_, classname_pos));
  mixin_application.set_is_mixin_typedef();
  library_.AddClass(mixin_application);
  set_current_class(mixin_application);
  ParseTypeParameters(mixin_application);

  ExpectToken(Token::kASSIGN);

  if (CurrentToken() == Token::kABSTRACT) {
    mixin_application.set_is_abstract();
    ConsumeToken();
  }

  const intptr_t type_pos = TokenPos();
  AbstractType& type =
      AbstractType::Handle(ParseType(ClassFinalizer::kResolveTypeParameters));
  if (type.IsTypeParameter()) {
    ErrorMsg(type_pos,
             "class '%s' may not extend type parameter '%s'",
             class_name.ToCString(),
             String::Handle(type.UserVisibleName()).ToCString());
  }

  if (CurrentToken() != Token::kWITH) {
    ErrorMsg("mixin application 'with Type' expected");
  }
  type = ParseMixins(type);

  // TODO(12773): Treat the mixin application as an alias, not as a base
  // class whose super class is the mixin application! This is difficult because
  // of issues involving subsitution of type parameters
  mixin_application.set_super_type(type);
  mixin_application.set_is_synthesized_class();

  // This mixin application typedef needs an implicit constructor, but it is
  // too early to call 'AddImplicitConstructor(mixin_application)' here,
  // because this class should be lazily compiled.
  if (CurrentToken() == Token::kIMPLEMENTS) {
    // At this point, the mixin_application alias already has an interface, but
    // ParseInterfaceList will add to the list and not lose the one already
    // there.
    ParseInterfaceList(mixin_application);
  }
  ExpectSemicolon();
  pending_classes.Add(mixin_application, Heap::kOld);
  if (metadata_pos >= 0) {
    library_.AddClassMetadata(mixin_application, metadata_pos);
  }
}


// Look ahead to detect if we are seeing ident [ TypeParameters ] "(".
// We need this lookahead to distinguish between the optional return type
// and the alias name of a function type alias.
// Token position remains unchanged.
bool Parser::IsFunctionTypeAliasName() {
  if (IsIdentifier() && (LookaheadToken(1) == Token::kLPAREN)) {
    return true;
  }
  const intptr_t saved_pos = TokenPos();
  bool is_alias_name = false;
  if (IsIdentifier() && (LookaheadToken(1) == Token::kLT)) {
    ConsumeToken();
    if (TryParseTypeParameter() && (CurrentToken() == Token::kLPAREN)) {
      is_alias_name = true;
    }
  }
  SetPosition(saved_pos);
  return is_alias_name;
}


// Look ahead to detect if we are seeing ident [ TypeParameters ] "=".
// Token position remains unchanged.
bool Parser::IsMixinTypedef() {
  if (IsIdentifier() && (LookaheadToken(1) == Token::kASSIGN)) {
    return true;
  }
  const intptr_t saved_pos = TokenPos();
  bool is_mixin_def = false;
  if (IsIdentifier() && (LookaheadToken(1) == Token::kLT)) {
    ConsumeToken();
    if (TryParseTypeParameter() && (CurrentToken() == Token::kASSIGN)) {
      is_mixin_def = true;
    }
  }
  SetPosition(saved_pos);
  return is_mixin_def;
}


void Parser::ParseTypedef(const GrowableObjectArray& pending_classes,
                          intptr_t metadata_pos) {
  TRACE_PARSER("ParseTypedef");
  ExpectToken(Token::kTYPEDEF);

  if (IsMixinTypedef()) {
    ParseMixinTypedef(pending_classes, metadata_pos);
    return;
  }

  // Parse the result type of the function type.
  AbstractType& result_type = Type::Handle(Type::DynamicType());
  if (CurrentToken() == Token::kVOID) {
    ConsumeToken();
    result_type = Type::VoidType();
  } else if (!IsFunctionTypeAliasName()) {
    // Type annotations in typedef are never ignored, even in unchecked mode.
    // Wait until we have an owner class before resolving the result type.
    result_type = ParseType(ClassFinalizer::kDoNotResolve);
  }

  const intptr_t alias_name_pos = TokenPos();
  const String* alias_name =
      ExpectUserDefinedTypeIdentifier("function alias name expected");

  // Lookup alias name and report an error if it is already defined in
  // the library scope.
  const Object& obj = Object::Handle(library_.LookupLocalObject(*alias_name));
  if (!obj.IsNull()) {
    ErrorMsg(alias_name_pos,
             "'%s' is already defined", alias_name->ToCString());
  }

  // Create the function type alias signature class. It will be linked to its
  // signature function after it has been parsed. The type parameters, in order
  // to be properly finalized, need to be associated to this signature class as
  // they are parsed.
  const Class& function_type_alias = Class::Handle(
      Class::NewSignatureClass(*alias_name,
                               Function::Handle(),
                               script_,
                               alias_name_pos));
  library_.AddClass(function_type_alias);
  set_current_class(function_type_alias);
  // Parse the type parameters of the function type.
  ParseTypeParameters(function_type_alias);
  // At this point, the type parameters have been parsed, so we can resolve the
  // result type.
  if (!result_type.IsNull()) {
    ResolveTypeFromClass(function_type_alias,
                         ClassFinalizer::kResolveTypeParameters,
                         &result_type);
  }
  // Parse the formal parameters of the function type.
  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg("formal parameter list expected");
  }
  ParamList func_params;

  // Add implicit closure object parameter.
  func_params.AddFinalParameter(
      TokenPos(),
      &Symbols::ClosureParameter(),
      &Type::ZoneHandle(Type::DynamicType()));

  const bool no_explicit_default_values = false;
  ParseFormalParameterList(no_explicit_default_values, &func_params);
  ExpectSemicolon();
  // The field 'is_static' has no meaning for signature functions.
  Function& signature_function = Function::Handle(
      Function::New(*alias_name,
                    RawFunction::kSignatureFunction,
                    /* is_static = */ false,
                    /* is_const = */ false,
                    /* is_abstract = */ false,
                    /* is_external = */ false,
                    function_type_alias,
                    alias_name_pos));
  signature_function.set_result_type(result_type);
  AddFormalParamsToFunction(&func_params, signature_function);

  // Patch the signature function in the signature class.
  function_type_alias.PatchSignatureFunction(signature_function);

  const String& signature = String::Handle(signature_function.Signature());
  if (FLAG_trace_parser) {
    OS::Print("TopLevel parsing function type alias '%s'\n",
              signature.ToCString());
  }
  // Lookup the signature class, i.e. the class whose name is the signature.
  // We only lookup in the current library, but not in its imports, and only
  // create a new canonical signature class if it does not exist yet.
  Class& signature_class = Class::ZoneHandle(
      library_.LookupLocalClass(signature));
  if (signature_class.IsNull()) {
    signature_class = Class::NewSignatureClass(signature,
                                               signature_function,
                                               script_,
                                               alias_name_pos);
    // Record the function signature class in the current library.
    library_.AddClass(signature_class);
  } else {
    // Forget the just created signature function and use the existing one.
    signature_function = signature_class.signature_function();
    function_type_alias.PatchSignatureFunction(signature_function);
  }
  ASSERT(signature_function.signature_class() == signature_class.raw());

  // The alias should not be marked as finalized yet, since it needs to be
  // checked in the class finalizer for illegal self references.
  ASSERT(!function_type_alias.IsCanonicalSignatureClass());
  ASSERT(!function_type_alias.is_finalized());
  pending_classes.Add(function_type_alias, Heap::kOld);
  if (metadata_pos >= 0) {
    library_.AddClassMetadata(function_type_alias, metadata_pos);
  }
}


// Consumes exactly one right angle bracket. If the current token is a single
// bracket token, it is consumed normally. However, if it is a double or triple
// bracket, it is replaced by a single or double bracket token without
// incrementing the token index.
void Parser::ConsumeRightAngleBracket() {
  if (token_kind_ == Token::kGT) {
    ConsumeToken();
  } else if (token_kind_ == Token::kSHR) {
    token_kind_ = Token::kGT;
  } else {
    UNREACHABLE();
  }
}


intptr_t Parser::SkipMetadata() {
  if (CurrentToken() != Token::kAT) {
    return -1;
  }
  intptr_t metadata_pos = TokenPos();
  while (CurrentToken() == Token::kAT) {
    ConsumeToken();
    ExpectIdentifier("identifier expected");
    if (CurrentToken() == Token::kPERIOD) {
      ConsumeToken();
      ExpectIdentifier("identifier expected");
      if (CurrentToken() == Token::kPERIOD) {
        ConsumeToken();
        ExpectIdentifier("identifier expected");
      }
    }
    if (CurrentToken() == Token::kLPAREN) {
      SkipToMatchingParenthesis();
    }
  }
  return metadata_pos;
}


void Parser::SkipTypeArguments() {
  if (CurrentToken() == Token::kLT) {
    do {
      ConsumeToken();
      SkipType(false);
    } while (CurrentToken() == Token::kCOMMA);
    Token::Kind token = CurrentToken();
    if ((token == Token::kGT) || (token == Token::kSHR)) {
      ConsumeRightAngleBracket();
    } else {
      ErrorMsg("right angle bracket expected");
    }
  }
}


void Parser::SkipType(bool allow_void) {
  if (CurrentToken() == Token::kVOID) {
    if (!allow_void) {
      ErrorMsg("'void' not allowed here");
    }
    ConsumeToken();
  } else {
    ExpectIdentifier("type name expected");
    if (CurrentToken() == Token::kPERIOD) {
      ConsumeToken();
      ExpectIdentifier("name expected");
    }
    SkipTypeArguments();
  }
}


void Parser::ParseTypeParameters(const Class& cls) {
  TRACE_PARSER("ParseTypeParameters");
  if (CurrentToken() == Token::kLT) {
    const GrowableObjectArray& type_parameters_array =
        GrowableObjectArray::Handle(GrowableObjectArray::New());
    intptr_t index = 0;
    TypeParameter& type_parameter = TypeParameter::Handle();
    TypeParameter& existing_type_parameter = TypeParameter::Handle();
    String& existing_type_parameter_name = String::Handle();
    AbstractType& type_parameter_bound = Type::Handle();
    do {
      ConsumeToken();
      SkipMetadata();
      const intptr_t type_parameter_pos = TokenPos();
      String& type_parameter_name =
          *ExpectUserDefinedTypeIdentifier("type parameter expected");
      // Check for duplicate type parameters.
      for (intptr_t i = 0; i < index; i++) {
        existing_type_parameter ^= type_parameters_array.At(i);
        existing_type_parameter_name = existing_type_parameter.name();
        if (existing_type_parameter_name.Equals(type_parameter_name)) {
          ErrorMsg(type_parameter_pos, "duplicate type parameter '%s'",
                   type_parameter_name.ToCString());
        }
      }
      if (CurrentToken() == Token::kEXTENDS) {
        ConsumeToken();
        // A bound may refer to the owner of the type parameter it applies to,
        // i.e. to the class or interface currently being parsed.
        // Postpone resolution in order to avoid resolving the class and its
        // type parameters, as they are not fully parsed yet.
        type_parameter_bound = ParseType(ClassFinalizer::kDoNotResolve);
      } else {
        type_parameter_bound = isolate()->object_store()->object_type();
      }
      type_parameter = TypeParameter::New(cls,
                                          index,
                                          type_parameter_name,
                                          type_parameter_bound,
                                          type_parameter_pos);
      type_parameters_array.Add(type_parameter);
      index++;
    } while (CurrentToken() == Token::kCOMMA);
    Token::Kind token = CurrentToken();
    if ((token == Token::kGT) || (token == Token::kSHR)) {
      ConsumeRightAngleBracket();
    } else {
      ErrorMsg("right angle bracket expected");
    }
    const TypeArguments& type_parameters =
        TypeArguments::Handle(NewTypeArguments(type_parameters_array));
    cls.set_type_parameters(type_parameters);
    // Try to resolve the upper bounds, which will at least resolve the
    // referenced type parameters.
    const intptr_t num_types = type_parameters.Length();
    for (intptr_t i = 0; i < num_types; i++) {
      type_parameter ^= type_parameters.TypeAt(i);
      type_parameter_bound = type_parameter.bound();
      ResolveTypeFromClass(cls,
                           ClassFinalizer::kResolveTypeParameters,
                           &type_parameter_bound);
      type_parameter.set_bound(type_parameter_bound);
    }
  }
}


RawAbstractTypeArguments* Parser::ParseTypeArguments(
    ClassFinalizer::FinalizationKind finalization) {
  TRACE_PARSER("ParseTypeArguments");
  if (CurrentToken() == Token::kLT) {
    const GrowableObjectArray& types =
        GrowableObjectArray::Handle(GrowableObjectArray::New());
    AbstractType& type = AbstractType::Handle();
    do {
      ConsumeToken();
      type = ParseType(finalization);
      // Map a malformed type argument to dynamic.
      if (type.IsMalformed()) {
        type = Type::DynamicType();
      }
      types.Add(type);
    } while (CurrentToken() == Token::kCOMMA);
    Token::Kind token = CurrentToken();
    if ((token == Token::kGT) || (token == Token::kSHR)) {
      ConsumeRightAngleBracket();
    } else {
      ErrorMsg("right angle bracket expected");
    }
    if (finalization != ClassFinalizer::kIgnore) {
      return NewTypeArguments(types);
    }
  }
  return TypeArguments::null();
}


// Parse interface list and add to class cls.
void Parser::ParseInterfaceList(const Class& cls) {
  TRACE_PARSER("ParseInterfaceList");
  ASSERT(CurrentToken() == Token::kIMPLEMENTS);
  const GrowableObjectArray& all_interfaces =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  AbstractType& interface = AbstractType::Handle();
  // First get all the interfaces already implemented by class.
  Array& cls_interfaces = Array::Handle(cls.interfaces());
  for (intptr_t i = 0; i < cls_interfaces.Length(); i++) {
    interface ^= cls_interfaces.At(i);
    all_interfaces.Add(interface);
  }
  // Now parse and add the new interfaces.
  do {
    ConsumeToken();
    intptr_t interface_pos = TokenPos();
    interface = ParseType(ClassFinalizer::kResolveTypeParameters);
    if (interface.IsTypeParameter()) {
      ErrorMsg(interface_pos,
               "type parameter '%s' may not be used in interface list",
               String::Handle(interface.UserVisibleName()).ToCString());
    }
    all_interfaces.Add(interface);
  } while (CurrentToken() == Token::kCOMMA);
  cls_interfaces = Array::MakeArray(all_interfaces);
  cls.set_interfaces(cls_interfaces);
}


RawAbstractType* Parser::ParseMixins(const AbstractType& super_type) {
  TRACE_PARSER("ParseMixins");
  ASSERT(CurrentToken() == Token::kWITH);

  const GrowableObjectArray& mixin_apps =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  AbstractType& mixin_type = AbstractType::Handle();
  AbstractTypeArguments& mixin_type_arguments =
      AbstractTypeArguments::Handle();
  Class& mixin_application = Class::Handle();
  Type& mixin_application_type = Type::Handle();
  Type& mixin_super_type = Type::Handle();
  ASSERT(super_type.IsType());
  mixin_super_type ^= super_type.raw();
  Array& mixin_application_interfaces = Array::Handle();
  do {
    ConsumeToken();
    const intptr_t mixin_pos = TokenPos();
    mixin_type = ParseType(ClassFinalizer::kResolveTypeParameters);
    if (mixin_type.IsTypeParameter()) {
      ErrorMsg(mixin_pos,
               "mixin type '%s' may not be a type parameter",
               String::Handle(mixin_type.UserVisibleName()).ToCString());
    }

    // The name of the mixin application class is a combination of
    // the superclass and mixin class.
    String& mixin_app_name = String::Handle();
    mixin_app_name = mixin_super_type.ClassName();
    mixin_app_name = String::Concat(mixin_app_name, Symbols::Ampersand());
    mixin_app_name = String::Concat(mixin_app_name,
                                    String::Handle(mixin_type.ClassName()));
    mixin_app_name = Symbols::New(mixin_app_name);

    mixin_application = Class::New(mixin_app_name, script_, mixin_pos);
    mixin_application.set_super_type(mixin_super_type);
    mixin_application.set_mixin(Type::Cast(mixin_type));
    mixin_application.set_library(library_);
    mixin_application.set_is_synthesized_class();

    // Add the mixin type to the interfaces that the mixin application
    // class implements. This is necessary so that type tests work.
    mixin_application_interfaces = Array::New(1);
    mixin_application_interfaces.SetAt(0, mixin_type);
    mixin_application.set_interfaces(mixin_application_interfaces);

    // For the type arguments of the mixin application type, we need
    // a copy of the type arguments to the mixin type. The simplest way
    // to get the copy is to rewind the parser, parse the mixin type
    // again and steal its type arguments.
    SetPosition(mixin_pos);
    mixin_type = ParseType(ClassFinalizer::kResolveTypeParameters);
    mixin_type_arguments = mixin_type.arguments();

    mixin_application_type = Type::New(mixin_application,
                                       mixin_type_arguments,
                                       mixin_pos);
    mixin_super_type = mixin_application_type.raw();
    mixin_apps.Add(mixin_application_type);
  } while (CurrentToken() == Token::kCOMMA);
  return MixinAppType::New(super_type,
                           Array::Handle(Array::MakeArray(mixin_apps)));
}


void Parser::ParseTopLevelVariable(TopLevel* top_level,
                                   intptr_t metadata_pos) {
  TRACE_PARSER("ParseTopLevelVariable");
  const bool is_const = (CurrentToken() == Token::kCONST);
  // Const fields are implicitly final.
  const bool is_final = is_const || (CurrentToken() == Token::kFINAL);
  const bool is_static = true;
  const AbstractType& type = AbstractType::ZoneHandle(ParseConstFinalVarOrType(
      ClassFinalizer::kResolveTypeParameters));
  Field& field = Field::Handle();
  Function& getter = Function::Handle();
  while (true) {
    const intptr_t name_pos = TokenPos();
    String& var_name = *ExpectIdentifier("variable name expected");

    if (library_.LookupLocalObject(var_name) != Object::null()) {
      ErrorMsg(name_pos, "'%s' is already defined", var_name.ToCString());
    }
    String& accessor_name = String::Handle(Field::GetterName(var_name));
    if (library_.LookupLocalObject(accessor_name) != Object::null()) {
      ErrorMsg(name_pos, "getter for '%s' is already defined",
               var_name.ToCString());
    }
    // A const or final variable does not define an implicit setter,
    // so we only check setters for non-final variables.
    if (!is_final) {
      accessor_name = Field::SetterName(var_name);
      if (library_.LookupLocalObject(accessor_name) != Object::null()) {
        ErrorMsg(name_pos, "setter for '%s' is already defined",
                 var_name.ToCString());
      }
    }

    field = Field::New(var_name, is_static, is_final, is_const,
                       current_class(), name_pos);
    field.set_type(type);
    field.set_value(Instance::Handle(Instance::null()));
    top_level->fields.Add(field);
    library_.AddObject(field, var_name);
    if (metadata_pos >= 0) {
      library_.AddFieldMetadata(field, metadata_pos);
    }
    if (CurrentToken() == Token::kASSIGN) {
      ConsumeToken();
      Instance& field_value = Instance::Handle(Object::sentinel().raw());
      bool has_simple_literal = false;
      if ((is_const || is_final) && (LookaheadToken(1) == Token::kSEMICOLON)) {
        has_simple_literal = IsSimpleLiteral(type, &field_value);
      }
      SkipExpr();
      field.set_value(field_value);
      if (!has_simple_literal) {
        // Create a static final getter.
        String& getter_name = String::Handle(Field::GetterSymbol(var_name));
        getter = Function::New(getter_name,
                               RawFunction::kImplicitStaticFinalGetter,
                               is_static,
                               is_const,
                               /* is_abstract = */ false,
                               /* is_external = */ false,
                               current_class(),
                               name_pos);
        getter.set_result_type(type);
        top_level->functions.Add(getter);
      }
    } else if (is_final) {
      ErrorMsg(name_pos, "missing initializer for final or const variable");
    }

    if (CurrentToken() == Token::kCOMMA) {
      ConsumeToken();
    } else if (CurrentToken() == Token::kSEMICOLON) {
      ConsumeToken();
      break;
    } else {
      ExpectSemicolon();  // Reports error.
    }
  }
}


void Parser::ParseTopLevelFunction(TopLevel* top_level,
                                   intptr_t metadata_pos) {
  TRACE_PARSER("ParseTopLevelFunction");
  const intptr_t decl_begin_pos = TokenPos();
  AbstractType& result_type = Type::Handle(Type::DynamicType());
  const bool is_static = true;
  bool is_external = false;
  bool is_patch = false;
  if (is_patch_source() &&
      (CurrentToken() == Token::kIDENT) &&
      CurrentLiteral()->Equals("patch") &&
      (LookaheadToken(1) != Token::kLPAREN)) {
    ConsumeToken();
    is_patch = true;
  } else if (CurrentToken() == Token::kEXTERNAL) {
    ConsumeToken();
    is_external = true;
  }
  if (CurrentToken() == Token::kVOID) {
    ConsumeToken();
    result_type = Type::VoidType();
  } else {
    // Parse optional type.
    if ((CurrentToken() == Token::kIDENT) &&
        (LookaheadToken(1) != Token::kLPAREN)) {
      result_type = ParseType(ClassFinalizer::kResolveTypeParameters);
    }
  }
  const intptr_t name_pos = TokenPos();
  const String& func_name = *ExpectIdentifier("function name expected");

  bool found = library_.LookupLocalObject(func_name) != Object::null();
  if (found && !is_patch) {
    ErrorMsg(name_pos, "'%s' is already defined", func_name.ToCString());
  } else if (!found && is_patch) {
    ErrorMsg(name_pos, "missing '%s' cannot be patched", func_name.ToCString());
  }
  String& accessor_name = String::Handle(Field::GetterName(func_name));
  if (library_.LookupLocalObject(accessor_name) != Object::null()) {
    ErrorMsg(name_pos, "'%s' is already defined as getter",
             func_name.ToCString());
  }
  // A setter named x= may co-exist with a function named x, thus we do
  // not need to check setters.

  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg("'(' expected");
  }
  const intptr_t function_pos = TokenPos();
  ParamList params;
  const bool allow_explicit_default_values = true;
  ParseFormalParameterList(allow_explicit_default_values, &params);

  intptr_t function_end_pos = function_pos;
  if (is_external) {
    function_end_pos = TokenPos();
    ExpectSemicolon();
  } else if (CurrentToken() == Token::kLBRACE) {
    SkipBlock();
    function_end_pos = TokenPos() - 1;
  } else if (CurrentToken() == Token::kARROW) {
    ConsumeToken();
    SkipExpr();
    function_end_pos = TokenPos();
    ExpectSemicolon();
  } else if (IsLiteral("native")) {
    ParseNativeDeclaration();
    function_end_pos = TokenPos();
    ExpectSemicolon();
  } else {
    ErrorMsg("function block expected");
  }
  Function& func = Function::Handle(
      Function::New(func_name,
                    RawFunction::kRegularFunction,
                    is_static,
                    /* is_const = */ false,
                    /* is_abstract = */ false,
                    is_external,
                    current_class(),
                    decl_begin_pos));
  func.set_result_type(result_type);
  func.set_end_token_pos(function_end_pos);
  AddFormalParamsToFunction(&params, func);
  top_level->functions.Add(func);
  if (!is_patch) {
    library_.AddObject(func, func_name);
  } else {
    library_.ReplaceObject(func, func_name);
  }
  if (metadata_pos >= 0) {
    library_.AddFunctionMetadata(func, metadata_pos);
  }
}


void Parser::ParseTopLevelAccessor(TopLevel* top_level,
                                   intptr_t metadata_pos) {
  TRACE_PARSER("ParseTopLevelAccessor");
  const intptr_t decl_begin_pos = TokenPos();
  const bool is_static = true;
  bool is_external = false;
  bool is_patch = false;
  AbstractType& result_type = AbstractType::Handle();
  if (is_patch_source() &&
      (CurrentToken() == Token::kIDENT) &&
      CurrentLiteral()->Equals("patch")) {
    ConsumeToken();
    is_patch = true;
  } else if (CurrentToken() == Token::kEXTERNAL) {
    ConsumeToken();
    is_external = true;
  }
  bool is_getter = (CurrentToken() == Token::kGET);
  if (CurrentToken() == Token::kGET ||
      CurrentToken() == Token::kSET) {
    ConsumeToken();
    result_type = Type::DynamicType();
  } else {
    if (CurrentToken() == Token::kVOID) {
      ConsumeToken();
      result_type = Type::VoidType();
    } else {
      result_type = ParseType(ClassFinalizer::kResolveTypeParameters);
    }
    is_getter = (CurrentToken() == Token::kGET);
    if (CurrentToken() == Token::kGET || CurrentToken() == Token::kSET) {
      ConsumeToken();
    } else {
      UnexpectedToken();
    }
  }
  const intptr_t name_pos = TokenPos();
  const String* field_name = ExpectIdentifier("accessor name expected");

  const intptr_t accessor_pos = TokenPos();
  ParamList params;

  if (!is_getter) {
    const bool allow_explicit_default_values = true;
    ParseFormalParameterList(allow_explicit_default_values, &params);
  }
  String& accessor_name = String::ZoneHandle();
  int expected_num_parameters = -1;
  if (is_getter) {
    expected_num_parameters = 0;
    accessor_name = Field::GetterSymbol(*field_name);
  } else {
    expected_num_parameters = 1;
    accessor_name = Field::SetterSymbol(*field_name);
  }
  if ((params.num_fixed_parameters != expected_num_parameters) ||
      (params.num_optional_parameters != 0)) {
    ErrorMsg(name_pos, "illegal %s parameters",
             is_getter ? "getter" : "setter");
  }

  if (is_getter && library_.LookupLocalObject(*field_name) != Object::null()) {
    ErrorMsg(name_pos, "'%s' is already defined in this library",
             field_name->ToCString());
  }
  if (!is_getter) {
    // Check whether there is a field with the same name that has an implicit
    // setter.
    const Field& field = Field::Handle(library_.LookupLocalField(*field_name));
    if (!field.IsNull() && !field.is_final()) {
      ErrorMsg(name_pos, "Variable '%s' is already defined in this library",
               field_name->ToCString());
    }
  }
  bool found = library_.LookupLocalObject(accessor_name) != Object::null();
  if (found && !is_patch) {
    ErrorMsg(name_pos, "%s for '%s' is already defined",
             is_getter ? "getter" : "setter",
             field_name->ToCString());
  } else if (!found && is_patch) {
    ErrorMsg(name_pos, "missing %s for '%s' cannot be patched",
             is_getter ? "getter" : "setter",
             field_name->ToCString());
  }

  intptr_t accessor_end_pos = accessor_pos;
  if (is_external) {
    accessor_end_pos = TokenPos();
    ExpectSemicolon();
  } else if (CurrentToken() == Token::kLBRACE) {
    SkipBlock();
    accessor_end_pos = TokenPos() - 1;
  } else if (CurrentToken() == Token::kARROW) {
    ConsumeToken();
    SkipExpr();
    accessor_end_pos = TokenPos();
    ExpectSemicolon();
  } else if (IsLiteral("native")) {
    ParseNativeDeclaration();
    accessor_end_pos = TokenPos();
    ExpectSemicolon();
  } else {
    ErrorMsg("function block expected");
  }
  Function& func = Function::Handle(
      Function::New(accessor_name,
                    is_getter? RawFunction::kGetterFunction :
                               RawFunction::kSetterFunction,
                    is_static,
                    /* is_const = */ false,
                    /* is_abstract = */ false,
                    is_external,
                    current_class(),
                    decl_begin_pos));
  func.set_result_type(result_type);
  func.set_end_token_pos(accessor_end_pos);
  AddFormalParamsToFunction(&params, func);
  top_level->functions.Add(func);
  if (!is_patch) {
    library_.AddObject(func, accessor_name);
  } else {
    library_.ReplaceObject(func, accessor_name);
  }
  if (metadata_pos >= 0) {
    library_.AddFunctionMetadata(func, metadata_pos);
  }
}



RawObject* Parser::CallLibraryTagHandler(Dart_LibraryTag tag,
                                          intptr_t token_pos,
                                          const String& url) {
  Dart_LibraryTagHandler handler = isolate()->library_tag_handler();
  if (handler == NULL) {
    if (url.StartsWith(Symbols::DartScheme())) {
      if (tag == Dart_kCanonicalizeUrl) {
        return url.raw();
      }
      return Object::null();
    }
    ErrorMsg(token_pos, "no library handler registered");
  }
  // Block class finalization attempts when calling into the library
  // tag handler.
  isolate()->BlockClassFinalization();
  Dart_Handle result = handler(tag,
                               Api::NewHandle(isolate(), library_.raw()),
                               Api::NewHandle(isolate(), url.raw()));
  isolate()->UnblockClassFinalization();
  if (Dart_IsError(result)) {
    // In case of an error we append an explanatory error message to the
    // error obtained from the library tag handler.
    Error& prev_error = Error::Handle();
    prev_error ^= Api::UnwrapHandle(result);
    AppendErrorMsg(prev_error, token_pos, "library handler failed");
  }
  if (tag == Dart_kCanonicalizeUrl) {
    if (!Dart_IsString(result)) {
      ErrorMsg(token_pos, "library handler failed URI canonicalization");
    }
  }
  return Api::UnwrapHandle(result);
}


void Parser::ParseLibraryName() {
  ASSERT(CurrentToken() == Token::kLIBRARY);
  ConsumeToken();
  String& lib_name = *ExpectIdentifier("library name expected");
  if (CurrentToken() == Token::kPERIOD) {
    while (CurrentToken() == Token::kPERIOD) {
      ConsumeToken();
      lib_name = String::Concat(lib_name, Symbols::Dot());
      lib_name = String::Concat(lib_name,
          *ExpectIdentifier("malformed library name"));
    }
    lib_name = Symbols::New(lib_name);
  }
  library_.SetName(lib_name);
  ExpectSemicolon();
}


void Parser::ParseIdentList(GrowableObjectArray* names) {
  if (!IsIdentifier()) {
    ErrorMsg("identifier expected");
  }
  while (IsIdentifier()) {
    names->Add(*CurrentLiteral());
    ConsumeToken();  // Identifier.
    if (CurrentToken() != Token::kCOMMA) {
      return;
    }
    ConsumeToken();  // Comma.
  }
}


void Parser::ParseLibraryImportExport() {
  bool is_import = (CurrentToken() == Token::kIMPORT);
  bool is_export = (CurrentToken() == Token::kEXPORT);
  ASSERT(is_import || is_export);
  const intptr_t import_pos = TokenPos();
  ConsumeToken();
  if (CurrentToken() != Token::kSTRING) {
    ErrorMsg("library url expected");
  }
  const String& url = *CurrentLiteral();
  if (url.Length() == 0) {
    ErrorMsg("library url expected");
  }
  ConsumeToken();
  String& prefix = String::Handle();
  if (is_import && IsLiteral("as")) {
    ConsumeToken();
    prefix = ExpectIdentifier("prefix identifier expected")->raw();
  }

  Array& show_names = Array::Handle();
  Array& hide_names = Array::Handle();
  if (IsLiteral("show") || IsLiteral("hide")) {
    GrowableObjectArray& show_list =
        GrowableObjectArray::Handle(GrowableObjectArray::New());
    GrowableObjectArray& hide_list =
        GrowableObjectArray::Handle(GrowableObjectArray::New());
    for (;;) {
      if (IsLiteral("show")) {
        ConsumeToken();
        ParseIdentList(&show_list);
      } else if (IsLiteral("hide")) {
        ConsumeToken();
        ParseIdentList(&hide_list);
      } else {
        break;
      }
    }
    if (show_list.Length() > 0) {
      show_names = Array::MakeArray(show_list);
    }
    if (hide_list.Length() > 0) {
      hide_names = Array::MakeArray(hide_list);
    }
  }
  ExpectSemicolon();

  // Canonicalize library URL.
  const String& canon_url = String::CheckedHandle(
      CallLibraryTagHandler(Dart_kCanonicalizeUrl, import_pos, url));
  // Lookup the library URL.
  Library& library = Library::Handle(Library::LookupLibrary(canon_url));
  if (library.IsNull()) {
    // Call the library tag handler to load the library.
    CallLibraryTagHandler(Dart_kImportTag, import_pos, canon_url);
    // If the library tag handler succeded without registering the
    // library we create an empty library to import.
    library = Library::LookupLibrary(canon_url);
    if (library.IsNull()) {
      library = Library::New(canon_url);
      library.Register();
    }
  }
  const Namespace& ns =
    Namespace::Handle(Namespace::New(library, show_names, hide_names));
  if (is_import) {
    // Ensure that private dart:_ libraries are only imported into dart:
    // libraries.
    const String& lib_url = String::Handle(library_.url());
    if (canon_url.StartsWith(Symbols::DartSchemePrivate()) &&
        !lib_url.StartsWith(Symbols::DartScheme())) {
      ErrorMsg(import_pos, "private library is not accessible");
    }
    if (prefix.IsNull() || (prefix.Length() == 0)) {
      library_.AddImport(ns);
    } else {
      LibraryPrefix& library_prefix = LibraryPrefix::Handle();
      library_prefix = library_.LookupLocalLibraryPrefix(prefix);
      if (!library_prefix.IsNull()) {
        library_prefix.AddImport(ns);
      } else {
        library_prefix = LibraryPrefix::New(prefix, ns);
        library_.AddObject(library_prefix, prefix);
      }
    }
  } else {
    ASSERT(is_export);
    library_.AddExport(ns);
  }
}


void Parser::ParseLibraryPart() {
  const intptr_t source_pos = TokenPos();
  ConsumeToken();  // Consume "part".
  if (CurrentToken() != Token::kSTRING) {
    ErrorMsg("url expected");
  }
  const String& url = *CurrentLiteral();
  ConsumeToken();
  ExpectSemicolon();
  const String& canon_url = String::CheckedHandle(
      CallLibraryTagHandler(Dart_kCanonicalizeUrl, source_pos, url));
  CallLibraryTagHandler(Dart_kSourceTag, source_pos, canon_url);
}


void Parser::ParseLibraryDefinition() {
  TRACE_PARSER("ParseLibraryDefinition");

  // Handle the script tag.
  if (CurrentToken() == Token::kSCRIPTTAG) {
    // Nothing to do for script tags except to skip them.
    ConsumeToken();
  }

  ASSERT(script_.kind() != RawScript::kSourceTag);

  // We may read metadata tokens that are part of the toplevel
  // declaration that follows the library definitions. Therefore, we
  // need to remember the position of the last token that was
  // successfully consumed.
  intptr_t rewind_pos = TokenPos();
  intptr_t metadata_pos = SkipMetadata();
  if (CurrentToken() == Token::kLIBRARY) {
    if (is_patch_source()) {
      ErrorMsg("patch cannot override library name");
    }
    ParseLibraryName();
    if (metadata_pos >= 0) {
      library_.AddLibraryMetadata(current_class(), metadata_pos);
    }
    rewind_pos = TokenPos();
    metadata_pos = SkipMetadata();
  }
  while ((CurrentToken() == Token::kIMPORT) ||
      (CurrentToken() == Token::kEXPORT)) {
    ParseLibraryImportExport();
    rewind_pos = TokenPos();
    metadata_pos = SkipMetadata();
  }
  // Core lib has not been explicitly imported, so we implicitly
  // import it here.
  if (!library_.ImportsCorelib()) {
    Library& core_lib = Library::Handle(Library::CoreLibrary());
    ASSERT(!core_lib.IsNull());
    const Namespace& core_ns = Namespace::Handle(
        Namespace::New(core_lib, Object::null_array(), Object::null_array()));
    library_.AddImport(core_ns);
  }
  while (CurrentToken() == Token::kPART) {
    ParseLibraryPart();
    rewind_pos = TokenPos();
    metadata_pos = SkipMetadata();
  }
  SetPosition(rewind_pos);
}


void Parser::ParsePartHeader() {
  SkipMetadata();
  if (CurrentToken() != Token::kPART) {
    ErrorMsg("'part of' expected");
  }
  ConsumeToken();
  if (!IsLiteral("of")) {
    ErrorMsg("'part of' expected");
  }
  ConsumeToken();
  // The VM is not required to check that the library name matches the
  // name of the current library, so we ignore it.
  ExpectIdentifier("library name expected");
  while (CurrentToken() == Token::kPERIOD) {
    ConsumeToken();
    ExpectIdentifier("malformed library name");
  }
  ExpectSemicolon();
}


void Parser::ParseTopLevel() {
  TRACE_PARSER("ParseTopLevel");
  // Collect the classes found at the top level in this growable array.
  // They need to be registered with class finalization after parsing
  // has been completed.
  ObjectStore* object_store = isolate()->object_store();
  const GrowableObjectArray& pending_classes =
      GrowableObjectArray::Handle(isolate(), object_store->pending_classes());
  SetPosition(0);
  is_top_level_ = true;
  TopLevel top_level;
  Class& toplevel_class = Class::Handle(
      Class::New(Symbols::TopLevel(), script_, TokenPos()));
  toplevel_class.set_library(library_);

  if (is_library_source() || is_patch_source()) {
    set_current_class(toplevel_class);
    ParseLibraryDefinition();
  } else if (is_part_source()) {
    ParsePartHeader();
  }

  const Class& cls = Class::Handle(isolate());
  while (true) {
    set_current_class(cls);  // No current class.
    intptr_t metadata_pos = SkipMetadata();
    if (CurrentToken() == Token::kCLASS) {
      ParseClassDeclaration(pending_classes, metadata_pos);
    } else if ((CurrentToken() == Token::kTYPEDEF) &&
               (LookaheadToken(1) != Token::kLPAREN)) {
      set_current_class(toplevel_class);
      ParseTypedef(pending_classes, metadata_pos);
    } else if ((CurrentToken() == Token::kABSTRACT) &&
        (LookaheadToken(1) == Token::kCLASS)) {
      ParseClassDeclaration(pending_classes, metadata_pos);
    } else if (is_patch_source() && IsLiteral("patch") &&
               (LookaheadToken(1) == Token::kCLASS)) {
      ParseClassDeclaration(pending_classes, metadata_pos);
    } else {
      set_current_class(toplevel_class);
      if (IsVariableDeclaration()) {
        ParseTopLevelVariable(&top_level, metadata_pos);
      } else if (IsFunctionDeclaration()) {
        ParseTopLevelFunction(&top_level, metadata_pos);
      } else if (IsTopLevelAccessor()) {
        ParseTopLevelAccessor(&top_level, metadata_pos);
      } else if (CurrentToken() == Token::kEOS) {
        break;
      } else {
        UnexpectedToken();
      }
    }
  }
  if ((top_level.fields.Length() > 0) || (top_level.functions.Length() > 0)) {
    Array& array = Array::Handle();

    array = Array::MakeArray(top_level.fields);
    toplevel_class.SetFields(array);

    array = Array::MakeArray(top_level.functions);
    toplevel_class.SetFunctions(array);

    library_.AddAnonymousClass(toplevel_class);
    pending_classes.Add(toplevel_class, Heap::kOld);
  }
}


void Parser::ChainNewBlock(LocalScope* outer_scope) {
  Block* block = new Block(current_block_,
                           outer_scope,
                           new SequenceNode(TokenPos(), outer_scope));
  current_block_ = block;
}


void Parser::OpenBlock() {
  ASSERT(current_block_ != NULL);
  LocalScope* outer_scope = current_block_->scope;
  ChainNewBlock(new LocalScope(outer_scope,
                               outer_scope->function_level(),
                               outer_scope->loop_level()));
}


void Parser::OpenLoopBlock() {
  ASSERT(current_block_ != NULL);
  LocalScope* outer_scope = current_block_->scope;
  ChainNewBlock(new LocalScope(outer_scope,
                               outer_scope->function_level(),
                               outer_scope->loop_level() + 1));
}


void Parser::OpenFunctionBlock(const Function& func) {
  LocalScope* outer_scope;
  if (current_block_ == NULL) {
    if (!func.IsLocalFunction()) {
      // We are compiling a non-nested function.
      outer_scope = new LocalScope(NULL, 0, 0);
    } else {
      // We are compiling the function of an invoked closure.
      // Restore the outer scope containing all captured variables.
      const ContextScope& context_scope =
          ContextScope::Handle(func.context_scope());
      ASSERT(!context_scope.IsNull());
      outer_scope =
          new LocalScope(LocalScope::RestoreOuterScope(context_scope), 0, 0);
    }
  } else {
    // We are parsing a nested function while compiling the enclosing function.
    outer_scope = new LocalScope(current_block_->scope,
                                 current_block_->scope->function_level() + 1,
                                 0);
  }
  ChainNewBlock(outer_scope);
}


SequenceNode* Parser::CloseBlock() {
  SequenceNode* statements = current_block_->statements;
  if (current_block_->scope != NULL) {
    // Record the begin and end token index of the scope.
    ASSERT(statements != NULL);
    current_block_->scope->set_begin_token_pos(statements->token_pos());
    current_block_->scope->set_end_token_pos(TokenPos());
  }
  current_block_ = current_block_->parent;
  return statements;
}


// Set up default values for all optional parameters to the function.
void Parser::SetupDefaultsForOptionalParams(const ParamList* params,
                                            Array& default_values) {
  if (params->num_optional_parameters > 0) {
    // Build array of default parameter values.
    ParamDesc* param =
      params->parameters->data() + params->num_fixed_parameters;
    default_values = Array::New(params->num_optional_parameters);
    for (int i = 0; i < params->num_optional_parameters; i++) {
      ASSERT(param->default_value != NULL);
      default_values.SetAt(i, *param->default_value);
      param++;
    }
  }
}


// Populate the parameter type array and parameter name array of the function
// with the formal parameter types and names.
void Parser::AddFormalParamsToFunction(const ParamList* params,
                                       const Function& func) {
  ASSERT((params != NULL) && (params->parameters != NULL));
  ASSERT((params->num_optional_parameters > 0) ==
         (params->has_optional_positional_parameters ||
          params->has_optional_named_parameters));
  if (!Utils::IsInt(16, params->num_fixed_parameters) ||
      !Utils::IsInt(16, params->num_optional_parameters)) {
    const Script& script = Script::Handle(Class::Handle(func.Owner()).script());
    const Error& error = Error::Handle(FormatErrorMsg(
        script, func.token_pos(), "Error", "too many formal parameters"));
    ErrorMsg(error);
  }
  func.set_num_fixed_parameters(params->num_fixed_parameters);
  func.SetNumOptionalParameters(params->num_optional_parameters,
                                params->has_optional_positional_parameters);
  const int num_parameters = params->parameters->length();
  ASSERT(num_parameters == func.NumParameters());
  func.set_parameter_types(Array::Handle(Array::New(num_parameters,
                                                    Heap::kOld)));
  func.set_parameter_names(Array::Handle(Array::New(num_parameters,
                                                    Heap::kOld)));
  for (int i = 0; i < num_parameters; i++) {
    ParamDesc& param_desc = (*params->parameters)[i];
    func.SetParameterTypeAt(i, *param_desc.type);
    func.SetParameterNameAt(i, *param_desc.name);
  }
}


// Populate local scope with the formal parameters.
void Parser::AddFormalParamsToScope(const ParamList* params,
                                    LocalScope* scope) {
  ASSERT((params != NULL) && (params->parameters != NULL));
  ASSERT(scope != NULL);
  const int num_parameters = params->parameters->length();
  for (int i = 0; i < num_parameters; i++) {
    ParamDesc& param_desc = (*params->parameters)[i];
    ASSERT(!is_top_level_ || param_desc.type->IsResolved());
    const String* name = param_desc.name;
    LocalVariable* parameter = new LocalVariable(
        param_desc.name_pos, *name, *param_desc.type);
    if (!scope->AddVariable(parameter)) {
      ErrorMsg(param_desc.name_pos,
               "name '%s' already exists in scope",
               param_desc.name->ToCString());
    }
    if (param_desc.is_final) {
      parameter->set_is_final();
    }
  }
}


// Builds ReturnNode/NativeBodyNode for a native function.
void Parser::ParseNativeFunctionBlock(const ParamList* params,
                                      const Function& func) {
  func.set_is_native(true);
  TRACE_PARSER("ParseNativeFunctionBlock");
  const Class& cls = Class::Handle(func.Owner());
  const Library& library = Library::Handle(cls.library());
  ASSERT(func.NumParameters() == params->parameters->length());

  // Parse the function name out.
  const intptr_t native_pos = TokenPos();
  const String& native_name = ParseNativeDeclaration();

  // Now resolve the native function to the corresponding native entrypoint.
  const int num_params = NativeArguments::ParameterCountForResolution(func);
  NativeFunction native_function = NativeEntry::ResolveNative(
      library, native_name, num_params);
  if (native_function == NULL) {
    ErrorMsg(native_pos, "native function '%s' cannot be found",
        native_name.ToCString());
  }

  // Now add the NativeBodyNode and return statement.
  Dart_NativeEntryResolver resolver = library.native_entry_resolver();
  bool is_bootstrap_native = Bootstrap::IsBootstapResolver(resolver);
  current_block_->statements->Add(
      new ReturnNode(TokenPos(),
                     new NativeBodyNode(TokenPos(),
                                        Function::ZoneHandle(func.raw()),
                                        native_name,
                                        native_function,
                                        current_block_->scope,
                                        is_bootstrap_native)));
}


LocalVariable* Parser::LookupReceiver(LocalScope* from_scope, bool test_only) {
  ASSERT(!current_function().is_static());
  return from_scope->LookupVariable(Symbols::This(), test_only);
}


LocalVariable* Parser::LookupTypeArgumentsParameter(LocalScope* from_scope,
                                                    bool test_only) {
  ASSERT(current_function().IsInFactoryScope());
  return from_scope->LookupVariable(Symbols::TypeArgumentsParameter(),
                                    test_only);
}


LocalVariable* Parser::LookupPhaseParameter() {
  const bool kTestOnly = false;
  return current_block_->scope->LookupVariable(Symbols::PhaseParameter(),
                                               kTestOnly);
}


void Parser::CaptureInstantiator() {
  ASSERT(current_block_->scope->function_level() > 0);
  bool found = false;
  if (current_function().IsInFactoryScope()) {
    found = current_block_->scope->CaptureVariable(
        Symbols::TypeArgumentsParameter());
  } else {
    found = current_block_->scope->CaptureVariable(Symbols::This());
  }
  ASSERT(found);
}


AstNode* Parser::LoadReceiver(intptr_t token_pos) {
  // A nested function may access 'this', referring to the receiver of the
  // outermost enclosing function.
  const bool kTestOnly = false;
  LocalVariable* receiver = LookupReceiver(current_block_->scope, kTestOnly);
  if (receiver == NULL) {
    ErrorMsg(token_pos, "illegal implicit access to receiver 'this'");
  }
  return new LoadLocalNode(TokenPos(), receiver);
}


AstNode* Parser::LoadTypeArgumentsParameter(intptr_t token_pos) {
  // A nested function may access ':type_arguments' to use as instantiator,
  // referring to the implicit first parameter of the outermost enclosing
  // factory function.
  const bool kTestOnly = false;
  LocalVariable* param = LookupTypeArgumentsParameter(current_block_->scope,
                                                      kTestOnly);
  ASSERT(param != NULL);
  return new LoadLocalNode(TokenPos(), param);
}


AstNode* Parser::CallGetter(intptr_t token_pos,
                            AstNode* object,
                            const String& name) {
  return new InstanceGetterNode(token_pos, object, name);
}


// Returns ast nodes of the variable initialization.
AstNode* Parser::ParseVariableDeclaration(const AbstractType& type,
                                          bool is_final,
                                          bool is_const) {
  TRACE_PARSER("ParseVariableDeclaration");
  ASSERT(IsIdentifier());
  const intptr_t ident_pos = TokenPos();
  LocalVariable* variable =
      new LocalVariable(ident_pos, *CurrentLiteral(), type);
  ASSERT(current_block_ != NULL);
  ASSERT(current_block_->scope != NULL);
  ConsumeToken();  // Variable identifier.
  AstNode* initialization = NULL;
  if (CurrentToken() == Token::kASSIGN) {
    // Variable initialization.
    const intptr_t assign_pos = TokenPos();
    ConsumeToken();
    AstNode* expr = ParseExpr(is_const, kConsumeCascades);
    initialization = new StoreLocalNode(assign_pos, variable, expr);
    if (is_const) {
      ASSERT(expr->IsLiteralNode());
      variable->SetConstValue(expr->AsLiteralNode()->literal());
    }
  } else if (is_final || is_const) {
    ErrorMsg(ident_pos,
    "missing initialization of 'final' or 'const' variable");
  } else {
    // Initialize variable with null.
    AstNode* null_expr = new LiteralNode(ident_pos, Instance::ZoneHandle());
    initialization = new StoreLocalNode(ident_pos, variable, null_expr);
  }
  // Add variable to scope after parsing the initalizer expression.
  // The expression must not be able to refer to the variable.
  if (!current_block_->scope->AddVariable(variable)) {
    LocalVariable* existing_var =
        current_block_->scope->LookupVariable(variable->name(), true);
    ASSERT(existing_var != NULL);
    if (existing_var->owner() == current_block_->scope) {
      ErrorMsg(ident_pos, "identifier '%s' already defined",
               variable->name().ToCString());
    } else {
      ErrorMsg(ident_pos,
               "'%s' from outer scope has already been used, cannot redefine",
               variable->name().ToCString());
    }
  }
  if (is_final || is_const) {
    variable->set_is_final();
  }
  return initialization;
}


// Parses ('var' | 'final' [type] | 'const' [type] | type).
// The presence of 'final' or 'const' must be detected and remembered
// before the call. If a type is parsed, it may be resolved and finalized
// according to the given type finalization mode.
RawAbstractType* Parser::ParseConstFinalVarOrType(
    ClassFinalizer::FinalizationKind finalization) {
  TRACE_PARSER("ParseConstFinalVarOrType");
  if (CurrentToken() == Token::kVAR) {
    ConsumeToken();
    return Type::DynamicType();
  }
  bool type_is_optional = false;
  if ((CurrentToken() == Token::kFINAL) || (CurrentToken() == Token::kCONST)) {
    ConsumeToken();
    type_is_optional = true;
  }
  if (CurrentToken() != Token::kIDENT) {
    if (type_is_optional) {
      return Type::DynamicType();
    } else {
      ErrorMsg("type name expected");
    }
  }
  if (type_is_optional) {
    Token::Kind follower = LookaheadToken(1);
    // We have an identifier followed by a 'follower' token.
    // We either parse a type or return now.
    if ((follower != Token::kLT) &&  // Parameterized type.
        (follower != Token::kPERIOD) &&  // Qualified class name of type.
        !Token::IsIdentifier(follower) &&  // Variable name following a type.
        (follower != Token::kTHIS)) {  // Field parameter following a type.
      return Type::DynamicType();
    }
  }
  return ParseType(finalization);
}


// Returns ast nodes of the variable initialization. Variables without an
// explicit initializer are initialized to null. If several variables are
// declared, the individual initializers are collected in a sequence node.
AstNode* Parser::ParseVariableDeclarationList() {
  TRACE_PARSER("ParseVariableDeclarationList");
  SkipMetadata();
  bool is_final = (CurrentToken() == Token::kFINAL);
  bool is_const = (CurrentToken() == Token::kCONST);
  const AbstractType& type = AbstractType::ZoneHandle(ParseConstFinalVarOrType(
      FLAG_enable_type_checks ? ClassFinalizer::kCanonicalize :
                                ClassFinalizer::kIgnore));
  if (!IsIdentifier()) {
    ErrorMsg("identifier expected");
  }

  AstNode* initializers = ParseVariableDeclaration(type, is_final, is_const);
  ASSERT(initializers != NULL);
  while (CurrentToken() == Token::kCOMMA) {
    ConsumeToken();
    if (!IsIdentifier()) {
      ErrorMsg("identifier expected after comma");
    }
    // We have a second initializer. Allocate a sequence node now.
    // The sequence does not own the current scope. Set its own scope to NULL.
    SequenceNode* sequence = NodeAsSequenceNode(initializers->token_pos(),
                                                initializers,
                                                NULL);
    sequence->Add(ParseVariableDeclaration(type, is_final, is_const));
    initializers = sequence;
  }
  return initializers;
}


AstNode* Parser::ParseFunctionStatement(bool is_literal) {
  TRACE_PARSER("ParseFunctionStatement");
  AbstractType& result_type = AbstractType::Handle();
  const String* variable_name = NULL;
  const String* function_name = NULL;

  result_type = Type::DynamicType();

  intptr_t ident_pos = TokenPos();
  if (is_literal) {
    ASSERT(CurrentToken() == Token::kLPAREN);
    function_name = &Symbols::AnonymousClosure();
  } else {
    if (CurrentToken() == Token::kVOID) {
      ConsumeToken();
      result_type = Type::VoidType();
    } else if ((CurrentToken() == Token::kIDENT) &&
               (LookaheadToken(1) != Token::kLPAREN)) {
      result_type = ParseType(ClassFinalizer::kCanonicalize);
    }
    variable_name = ExpectIdentifier("function name expected");
    function_name = variable_name;
  }

  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg("'(' expected");
  }
  intptr_t function_pos = TokenPos();

  // Check whether we have parsed this closure function before, in a previous
  // compilation. If so, reuse the function object, else create a new one
  // and register it in the current class.
  // Note that we cannot share the same closure function between the closurized
  // and non-closurized versions of the same parent function.
  Function& function = Function::ZoneHandle();
  bool is_new_closure = false;
  // TODO(hausner): There could be two different closures at the given
  // function_pos, one enclosed in a closurized function and one enclosed in the
  // non-closurized version of this same function.
  function = current_class().LookupClosureFunction(function_pos);
  if (function.IsNull() || (function.token_pos() != function_pos) ||
      (function.parent_function() != innermost_function().raw())) {
    is_new_closure = true;
    function = Function::NewClosureFunction(*function_name,
                                            innermost_function(),
                                            ident_pos);
    function.set_result_type(result_type);
    current_class().AddClosureFunction(function);
  }

  // The function type needs to be finalized at compile time, since the closure
  // may be type checked at run time when assigned to a function variable,
  // passed as a function argument, or returned as a function result.

  LocalVariable* function_variable = NULL;
  Type& function_type = Type::ZoneHandle();
  if (variable_name != NULL) {
    // Since the function type depends on the signature of the closure function,
    // it cannot be determined before the formal parameter list of the closure
    // function is parsed. Therefore, we set the function type to a new
    // parameterized type to be patched after the actual type is known.
    // We temporarily use the class of the Function interface.
    const Class& unknown_signature_class = Class::Handle(
        Type::Handle(Type::Function()).type_class());
    function_type = Type::New(
        unknown_signature_class, TypeArguments::Handle(), ident_pos);
    function_type.SetIsFinalized();  // No finalization needed.

    // Add the function variable to the scope before parsing the function in
    // order to allow self reference from inside the function.
    function_variable = new LocalVariable(ident_pos,
                                          *variable_name,
                                          function_type);
    function_variable->set_is_final();
    ASSERT(current_block_ != NULL);
    ASSERT(current_block_->scope != NULL);
    if (!current_block_->scope->AddVariable(function_variable)) {
      LocalVariable* existing_var =
          current_block_->scope->LookupVariable(function_variable->name(),
                                                true);
      ASSERT(existing_var != NULL);
      if (existing_var->owner() == current_block_->scope) {
        ErrorMsg(ident_pos, "identifier '%s' already defined",
                 function_variable->name().ToCString());
      } else {
        ErrorMsg(ident_pos,
                 "'%s' from outer scope has already been used, cannot redefine",
                 function_variable->name().ToCString());
      }
    }
  }

  // Parse the local function.
  Array& default_parameter_values = Array::Handle();
  SequenceNode* statements = Parser::ParseFunc(function,
                                               default_parameter_values);

  // Now that the local function has formal parameters, lookup the signature
  // class in the current library (but not in its imports) and only create a new
  // canonical signature class if it does not exist yet.
  const String& signature = String::Handle(function.Signature());
  Class& signature_class = Class::ZoneHandle();
  if (!is_new_closure) {
    signature_class = function.signature_class();
  }
  if (signature_class.IsNull()) {
    signature_class = library_.LookupLocalClass(signature);
  }
  if (signature_class.IsNull()) {
    // If we don't have a signature class yet, this must be a closure we
    // have not parsed before.
    ASSERT(is_new_closure);
    signature_class = Class::NewSignatureClass(signature,
                                               function,
                                               script_,
                                               function.token_pos());
    // Record the function signature class in the current library.
    library_.AddClass(signature_class);
  } else if (is_new_closure) {
    function.set_signature_class(signature_class);
  }
  ASSERT(function.signature_class() == signature_class.raw());

  // Local functions are registered in the enclosing class, but
  // ignored during class finalization. The enclosing class has
  // already been finalized.
  ASSERT(current_class().is_finalized());

  // Make sure that the instantiator is captured.
  if ((signature_class.NumTypeParameters() > 0) &&
      (current_block_->scope->function_level() > 0)) {
    CaptureInstantiator();
  }

  // Since the signature type is cached by the signature class, it may have
  // been finalized already.
  Type& signature_type = Type::Handle(signature_class.SignatureType());
  AbstractTypeArguments& signature_type_arguments =
      AbstractTypeArguments::Handle(signature_type.arguments());

  if (!signature_type.IsFinalized()) {
    signature_type ^= ClassFinalizer::FinalizeType(
        signature_class, signature_type, ClassFinalizer::kCanonicalize);

    // The call to ClassFinalizer::FinalizeType may have
    // extended the vector of type arguments.
    signature_type_arguments = signature_type.arguments();
    ASSERT(signature_type.IsMalformed() ||
           signature_type_arguments.IsNull() ||
           (signature_type_arguments.Length() ==
            signature_class.NumTypeArguments()));

    // The signature_class should not have changed.
    ASSERT(signature_type.IsMalformed() ||
           (signature_type.type_class() == signature_class.raw()));
  }

  if (variable_name != NULL) {
    // Patch the function type of the variable now that the signature is known.
    function_type.set_type_class(signature_class);
    function_type.set_arguments(signature_type_arguments);

    // Mark the function type as malformed if the signature type is malformed.
    if (signature_type.IsMalformed()) {
      const Error& error = Error::Handle(signature_type.malformed_error());
      function_type.set_malformed_error(error);
    }
    // TODO(regis): What if the signature is malbounded?

    // The function type was initially marked as instantiated, but it may
    // actually be uninstantiated.
    function_type.ResetIsFinalized();

    // The function variable type should have been patched above.
    ASSERT((function_variable == NULL) ||
           (function_variable->type().raw() == function_type.raw()));
  }

  // The code generator does not compile the closure function when visiting
  // a ClosureNode. The generated code allocates a new Closure object containing
  // the current context. The type of the Closure object refers to the closure
  // function, which will be compiled on first invocation of the closure object.
  // Therefore, we ignore the parsed default_parameter_values and the
  // node_sequence representing the body of the closure function, which will be
  // parsed again when compiled later.
  // The only purpose of parsing the function now (besides reporting obvious
  // errors) is to mark referenced variables of the enclosing scopes as
  // captured. The captured variables will be recorded along with their
  // allocation information in a Scope object stored in the function object.
  // This Scope object is then provided to the compiler when compiling the local
  // function. It would be too early to record the captured variables here,
  // since further closure functions may capture more variables.
  // This Scope object is constructed after all variables have been allocated.
  // The local scope of the parsed function can be pruned, since contained
  // variables are not relevant for the compilation of the enclosing function.
  // This pruning is done by omitting to hook the local scope in its parent
  // scope in the constructor of LocalScope.
  AstNode* closure =
      new ClosureNode(ident_pos, function, NULL, statements->scope());

  if (function_variable == NULL) {
    ASSERT(is_literal);
    return closure;
  } else {
    AstNode* initialization =
        new StoreLocalNode(ident_pos, function_variable, closure);
    return initialization;
  }
}


// Returns true if the current and next tokens can be parsed as type
// parameters. Current token position is not saved and restored.
bool Parser::TryParseTypeParameter() {
  if (CurrentToken() == Token::kLT) {
    // We are possibly looking at type parameters. Find closing ">".
    int nesting_level = 0;
    do {
      if (CurrentToken() == Token::kLT) {
        nesting_level++;
      } else if (CurrentToken() == Token::kGT) {
        nesting_level--;
      } else if (CurrentToken() == Token::kSHR) {
        nesting_level -= 2;
      } else if (CurrentToken() == Token::kIDENT) {
        // Check to see if it is a qualified identifier.
        if (LookaheadToken(1) == Token::kPERIOD) {
          // Consume the identifier, the period will be consumed below.
          ConsumeToken();
        }
      } else if (CurrentToken() != Token::kCOMMA &&
                 CurrentToken() != Token::kEXTENDS) {
        // We are looking at something other than type parameters.
        return false;
      }
      ConsumeToken();
    } while (nesting_level > 0);
    if (nesting_level < 0) {
      return false;
    }
  }
  return true;
}


bool Parser::IsSimpleLiteral(const AbstractType& type, Instance* value) {
  // Assigning null never causes a type error.
  if (CurrentToken() == Token::kNULL) {
    *value = Instance::null();
    return true;
  }
  // If the type of the const field is guaranteed to be instantiated once
  // resolved at class finalization time, and if the type of the literal is one
  // of int, double, String, or bool, then preset the field with the value and
  // perform the type check (in checked mode only) at finalization time.
  if (type.IsTypeParameter() ||
      (type.arguments() != AbstractTypeArguments::null())) {
    // Type parameters are always resolved eagerly by the parser and never
    // resolved later by the class finalizer. Therefore, we know here that if
    // 'type' is not a type parameter (an unresolved type will not get resolved
    // to a type parameter later) and if 'type' has no type arguments, then it
    // will be instantiated at class finalization time. Otherwise, we return
    // false, since the type test would not be possible at finalization time for
    // an uninstantiated type.
    return false;
  }
  if (CurrentToken() == Token::kINTEGER) {
    *value = CurrentIntegerLiteral();
    return true;
  } else if (CurrentToken() == Token::kDOUBLE) {
    *value = CurrentDoubleLiteral();
    return true;
  } else if (CurrentToken() == Token::kSTRING) {
    *value = CurrentLiteral()->raw();
    return true;
  } else if (CurrentToken() == Token::kTRUE) {
    *value = Bool::True().raw();
    return true;
  } else if (CurrentToken() == Token::kFALSE) {
    *value = Bool::False().raw();
    return true;
  }
  return false;
}


// Returns true if the current token is kIDENT or a pseudo-keyword.
bool Parser::IsIdentifier() {
  return Token::IsIdentifier(CurrentToken());
}


// Returns true if the next tokens can be parsed as a type with optional
// type parameters. Current token position is not restored.
bool Parser::TryParseOptionalType() {
  if (CurrentToken() == Token::kIDENT) {
    QualIdent type_name;
    ParseQualIdent(&type_name);
    if ((CurrentToken() == Token::kLT) && !TryParseTypeParameter()) {
      return false;
    }
  }
  return true;
}


// Returns true if the next tokens can be parsed as a type with optional
// type parameters, or keyword "void".
// Current token position is not restored.
bool Parser::TryParseReturnType() {
  if (CurrentToken() == Token::kVOID) {
    ConsumeToken();
    return true;
  } else if (CurrentToken() == Token::kIDENT) {
    return TryParseOptionalType();
  }
  return false;
}


// Look ahead to detect whether the next tokens should be parsed as
// a variable declaration. Ignores optional metadata.
// Returns true if we detect the token pattern:
//     'var'
//   | 'final'
//   | const [type] ident (';' | '=' | ',')
//   | type ident (';' | '=' | ',')
// Token position remains unchanged.
bool Parser::IsVariableDeclaration() {
  if ((CurrentToken() == Token::kVAR) ||
      (CurrentToken() == Token::kFINAL)) {
    return true;
  }
  // Skip optional metadata.
  if (CurrentToken() == Token::kAT) {
    const intptr_t saved_pos = TokenPos();
    SkipMetadata();
    const bool is_var_decl = IsVariableDeclaration();
    SetPosition(saved_pos);
    return is_var_decl;
  }
  if ((CurrentToken() != Token::kIDENT) && (CurrentToken() != Token::kCONST)) {
    // Not a legal type identifier or const keyword or metadata.
    return false;
  }
  const intptr_t saved_pos = TokenPos();
  bool is_var_decl = false;
  bool have_type = false;
  if (CurrentToken() == Token::kCONST) {
    ConsumeToken();
    have_type = true;  // Type is dynamic.
  }
  if (IsIdentifier()) {  // Type or variable name.
    Token::Kind follower = LookaheadToken(1);
    if ((follower == Token::kLT) ||  // Parameterized type.
        (follower == Token::kPERIOD) ||  // Qualified class name of type.
        Token::IsIdentifier(follower)) {  // Variable name following a type.
      // We see the beginning of something that could be a type.
      const intptr_t type_pos = TokenPos();
      if (TryParseOptionalType()) {
        have_type = true;
      } else {
        SetPosition(type_pos);
      }
    }
    if (have_type && IsIdentifier()) {
      ConsumeToken();
      if ((CurrentToken() == Token::kSEMICOLON) ||
          (CurrentToken() == Token::kCOMMA) ||
          (CurrentToken() == Token::kASSIGN)) {
        is_var_decl = true;
      }
    }
  }
  SetPosition(saved_pos);
  return is_var_decl;
}


// Look ahead to detect whether the next tokens should be parsed as
// a function declaration. Token position remains unchanged.
bool Parser::IsFunctionDeclaration() {
  const intptr_t saved_pos = TokenPos();
  bool is_external = false;
  if (is_top_level_) {
    if (is_patch_source() &&
        (CurrentToken() == Token::kIDENT) &&
        CurrentLiteral()->Equals("patch") &&
        (LookaheadToken(1) != Token::kLPAREN)) {
      // Skip over 'patch' for top-level function declarations in patch sources.
      ConsumeToken();
    } else if (CurrentToken() == Token::kEXTERNAL) {
      // Skip over 'external' for top-level function declarations.
      is_external = true;
      ConsumeToken();
    }
  }
  if (IsIdentifier() && (LookaheadToken(1) == Token::kLPAREN)) {
    // Possibly a function without explicit return type.
    ConsumeToken();  // Consume function identifier.
  } else if (TryParseReturnType()) {
    if (!IsIdentifier()) {
      SetPosition(saved_pos);
      return false;
    }
    ConsumeToken();  // Consume function identifier.
  } else {
    SetPosition(saved_pos);
    return false;
  }
  // Check parameter list and the following token.
  if (CurrentToken() == Token::kLPAREN) {
    SkipToMatchingParenthesis();
    if ((CurrentToken() == Token::kLBRACE) ||
        (CurrentToken() == Token::kARROW) ||
        (is_top_level_ && IsLiteral("native")) ||
        is_external) {
      SetPosition(saved_pos);
      return true;
    }
  }
  SetPosition(saved_pos);
  return false;
}


bool Parser::IsTopLevelAccessor() {
  const intptr_t saved_pos = TokenPos();
  if (is_patch_source() &&
      (CurrentToken() == Token::kIDENT) &&
      (CurrentLiteral()->Equals("patch"))) {
    ConsumeToken();
  } else if (CurrentToken() == Token::kEXTERNAL) {
    ConsumeToken();
  }
  if ((CurrentToken() == Token::kGET) || (CurrentToken() == Token::kSET)) {
    SetPosition(saved_pos);
    return true;
  }
  if (TryParseReturnType()) {
    if ((CurrentToken() == Token::kGET) || (CurrentToken() == Token::kSET)) {
      if (Token::IsIdentifier(LookaheadToken(1))) {  // Accessor name.
        SetPosition(saved_pos);
        return true;
      }
    }
  }
  SetPosition(saved_pos);
  return false;
}


bool Parser::IsFunctionLiteral() {
  if (CurrentToken() != Token::kLPAREN || !allow_function_literals_) {
    return false;
  }
  const intptr_t saved_pos = TokenPos();
  bool is_function_literal = false;
  SkipToMatchingParenthesis();
  if ((CurrentToken() == Token::kLBRACE) ||
      (CurrentToken() == Token::kARROW)) {
    is_function_literal = true;
  }
  SetPosition(saved_pos);
  return is_function_literal;
}


// Current token position is the token after the opening ( of the for
// statement. Returns true if we recognize a for ( .. in expr)
// statement.
bool Parser::IsForInStatement() {
  const intptr_t saved_pos = TokenPos();
  bool result = false;
  // Allow const modifier as well when recognizing a for-in statement
  // pattern. We will get an error later if the loop variable is
  // declared with const.
  if (CurrentToken() == Token::kVAR ||
      CurrentToken() == Token::kFINAL ||
      CurrentToken() == Token::kCONST) {
    ConsumeToken();
  }
  if (IsIdentifier()) {
    if (LookaheadToken(1) == Token::kIN) {
      result = true;
    } else if (TryParseOptionalType()) {
      if (IsIdentifier()) {
        ConsumeToken();
      }
      result = (CurrentToken() == Token::kIN);
    }
  }
  SetPosition(saved_pos);
  return result;
}


static bool ContainsAbruptCompletingStatement(SequenceNode* seq);

static bool IsAbruptCompleting(AstNode* statement) {
  return statement->IsReturnNode() ||
         statement->IsJumpNode()   ||
         statement->IsThrowNode()  ||
         (statement->IsSequenceNode() &&
             ContainsAbruptCompletingStatement(statement->AsSequenceNode()));
}


static bool ContainsAbruptCompletingStatement(SequenceNode* seq) {
  for (int i = 0; i < seq->length(); i++) {
    if (IsAbruptCompleting(seq->NodeAt(i))) {
      return true;
    }
  }
  return false;
}


void Parser::ParseStatementSequence() {
  TRACE_PARSER("ParseStatementSequence");
  const bool dead_code_allowed = true;
  bool abrupt_completing_seen = false;
  while (CurrentToken() != Token::kRBRACE) {
    const intptr_t statement_pos = TokenPos();
    AstNode* statement = ParseStatement();
    // Do not add statements with no effect (e.g., LoadLocalNode).
    if ((statement != NULL) && statement->IsLoadLocalNode()) {
      // Skip load local.
      continue;
    }
    if (statement != NULL) {
      if (!dead_code_allowed && abrupt_completing_seen) {
        ErrorMsg(statement_pos, "dead code after abrupt completing statement");
      }
      current_block_->statements->Add(statement);
      abrupt_completing_seen |= IsAbruptCompleting(statement);
    }
  }
}


// Parse nested statement of if, while, for, etc. We automatically generate
// a sequence of one statement if there are no curly braces.
// The argument 'parsing_loop_body' indicates the parsing of a loop statement.
SequenceNode* Parser::ParseNestedStatement(bool parsing_loop_body,
                                           SourceLabel* label) {
  TRACE_PARSER("ParseNestedStatement");
  if (parsing_loop_body) {
    OpenLoopBlock();
  } else {
    OpenBlock();
  }
  if (label != NULL) {
    current_block_->scope->AddLabel(label);
  }
  if (CurrentToken() == Token::kLBRACE) {
    ConsumeToken();
    ParseStatementSequence();
    ExpectToken(Token::kRBRACE);
  } else {
    AstNode* statement = ParseStatement();
    if (statement != NULL) {
      current_block_->statements->Add(statement);
    }
  }
  SequenceNode* sequence = CloseBlock();
  return sequence;
}


AstNode* Parser::ParseIfStatement(String* label_name) {
  TRACE_PARSER("ParseIfStatement");
  ASSERT(CurrentToken() == Token::kIF);
  const intptr_t if_pos = TokenPos();
  SourceLabel* label = NULL;
  if (label_name != NULL) {
    label = SourceLabel::New(if_pos, label_name, SourceLabel::kStatement);
    OpenBlock();
    current_block_->scope->AddLabel(label);
  }
  ConsumeToken();
  ExpectToken(Token::kLPAREN);
  AstNode* cond_expr = ParseExpr(kAllowConst, kConsumeCascades);
  ExpectToken(Token::kRPAREN);
  const bool parsing_loop_body = false;
  SequenceNode* true_branch = ParseNestedStatement(parsing_loop_body, NULL);
  SequenceNode* false_branch = NULL;
  if (CurrentToken() == Token::kELSE) {
    ConsumeToken();
    false_branch = ParseNestedStatement(parsing_loop_body, NULL);
  }
  AstNode* if_node = new IfNode(if_pos, cond_expr, true_branch, false_branch);
  if (label != NULL) {
    current_block_->statements->Add(if_node);
    SequenceNode* sequence = CloseBlock();
    sequence->set_label(label);
    if_node = sequence;
  }
  return if_node;
}


CaseNode* Parser::ParseCaseClause(LocalVariable* switch_expr_value,
                                  SourceLabel* case_label) {
  TRACE_PARSER("ParseCaseClause");
  bool default_seen = false;
  const intptr_t case_pos = TokenPos();
  // The case expressions node sequence does not own the enclosing scope.
  SequenceNode* case_expressions = new SequenceNode(case_pos, NULL);
  while (CurrentToken() == Token::kCASE || CurrentToken() == Token::kDEFAULT) {
    if (CurrentToken() == Token::kCASE) {
      if (default_seen) {
        ErrorMsg("default clause must be last case");
      }
      ConsumeToken();  // Keyword case.
      const intptr_t expr_pos = TokenPos();
      AstNode* expr = ParseExpr(kRequireConst, kConsumeCascades);
      AstNode* switch_expr_load = new LoadLocalNode(case_pos,
                                                    switch_expr_value);
      AstNode* case_comparison = new ComparisonNode(expr_pos,
                                                    Token::kEQ,
                                                    expr,
                                                    switch_expr_load);
      case_expressions->Add(case_comparison);
    } else {
      if (default_seen) {
        ErrorMsg("only one default clause is allowed");
      }
      ConsumeToken();  // Keyword default.
      default_seen = true;
      // The default case always succeeds.
    }
    ExpectToken(Token::kCOLON);
  }

  OpenBlock();
  bool abrupt_completing_seen = false;
  while (true) {
    // Check whether the next statement still belongs to the current case
    // clause. If we see 'case' or 'default', optionally preceeded by
    // a label, or closing brace, we stop parsing statements.
    Token::Kind next_token;
    if (IsIdentifier() && LookaheadToken(1) == Token::kCOLON) {
      next_token = LookaheadToken(2);
    } else {
      next_token = CurrentToken();
    }
    if (next_token == Token::kRBRACE) {
      // End of switch statement.
      break;
    }
    if ((next_token == Token::kCASE) || (next_token == Token::kDEFAULT)) {
      // End of this case clause. If there is a possible fall-through to
      // the next case clause, throw an implicit FallThroughError.
      if (!abrupt_completing_seen) {
        ArgumentListNode* arguments = new ArgumentListNode(TokenPos());
        arguments->Add(new LiteralNode(
            TokenPos(), Integer::ZoneHandle(Integer::New(TokenPos()))));
        current_block_->statements->Add(
            MakeStaticCall(Symbols::FallThroughError(),
                           PrivateCoreLibName(Symbols::ThrowNew()),
                           arguments));
      }
      break;
    }
    // The next statement still belongs to this case.
    AstNode* statement = ParseStatement();
    if (statement != NULL) {
      current_block_->statements->Add(statement);
      abrupt_completing_seen |= IsAbruptCompleting(statement);
    }
  }
  SequenceNode* statements = CloseBlock();
  return new CaseNode(case_pos, case_label,
      case_expressions, default_seen, switch_expr_value, statements);
}


AstNode* Parser::ParseSwitchStatement(String* label_name) {
  TRACE_PARSER("ParseSwitchStatement");
  ASSERT(CurrentToken() == Token::kSWITCH);
  const intptr_t switch_pos = TokenPos();
  SourceLabel* label =
      SourceLabel::New(switch_pos, label_name, SourceLabel::kSwitch);
  ConsumeToken();
  ExpectToken(Token::kLPAREN);
  const intptr_t expr_pos = TokenPos();
  AstNode* switch_expr = ParseExpr(kAllowConst, kConsumeCascades);
  ExpectToken(Token::kRPAREN);
  ExpectToken(Token::kLBRACE);
  OpenBlock();
  current_block_->scope->AddLabel(label);

  // Store switch expression in temporary local variable.
  LocalVariable* temp_variable =
      new LocalVariable(expr_pos,
                        Symbols::SwitchExpr(),
                        Type::ZoneHandle(Type::DynamicType()));
  current_block_->scope->AddVariable(temp_variable);
  AstNode* save_switch_expr =
      new StoreLocalNode(expr_pos, temp_variable, switch_expr);
  current_block_->statements->Add(save_switch_expr);

  // Parse case clauses
  bool default_seen = false;
  while (true) {
    // Check for statement label
    SourceLabel* case_label = NULL;
    if (IsIdentifier() && LookaheadToken(1) == Token::kCOLON) {
      // Case statements start with a label.
      String* label_name = CurrentLiteral();
      const intptr_t label_pos = TokenPos();
      ConsumeToken();  // Consume label identifier.
      ConsumeToken();  // Consume colon.
      case_label = current_block_->scope->LocalLookupLabel(*label_name);
      if (case_label == NULL) {
        // Label does not exist yet. Add it to scope of switch statement.
        case_label =
            new SourceLabel(label_pos, *label_name, SourceLabel::kCase);
        current_block_->scope->AddLabel(case_label);
      } else if (case_label->kind() == SourceLabel::kForward) {
        // We have seen a 'continue' with this label name. Resolve
        // the forward reference.
        case_label->ResolveForwardReference();
      } else {
        ErrorMsg(label_pos, "label '%s' already exists in scope",
                 label_name->ToCString());
      }
      ASSERT(case_label->kind() == SourceLabel::kCase);
    }
    if (CurrentToken() == Token::kCASE ||
        CurrentToken() == Token::kDEFAULT) {
      if (default_seen) {
        ErrorMsg("no case clauses allowed after default clause");
      }
      CaseNode* case_clause = ParseCaseClause(temp_variable, case_label);
      default_seen = case_clause->contains_default();
      current_block_->statements->Add(case_clause);
    } else if (CurrentToken() != Token::kRBRACE) {
      ErrorMsg("'case' or '}' expected");
    } else if (case_label != NULL) {
      ErrorMsg("expecting at least one case clause after label");
    } else {
      break;
    }
  }

  // TODO(hausner): Check that all expressions in case clauses are
  // of the same class, or implement int or String (issue 7307).

  // Check for unresolved label references.
  SourceLabel* unresolved_label =
      current_block_->scope->CheckUnresolvedLabels();
  if (unresolved_label != NULL) {
    ErrorMsg("unresolved reference to label '%s'",
             unresolved_label->name().ToCString());
  }

  SequenceNode* switch_body = CloseBlock();
  ExpectToken(Token::kRBRACE);
  return new SwitchNode(switch_pos, label, switch_body);
}


AstNode* Parser::ParseWhileStatement(String* label_name) {
  TRACE_PARSER("ParseWhileStatement");
  const intptr_t while_pos = TokenPos();
  SourceLabel* label =
      SourceLabel::New(while_pos, label_name, SourceLabel::kWhile);
  ConsumeToken();
  ExpectToken(Token::kLPAREN);
  AstNode* cond_expr = ParseExpr(kAllowConst, kConsumeCascades);
  ExpectToken(Token::kRPAREN);
  const bool parsing_loop_body =  true;
  SequenceNode* while_body = ParseNestedStatement(parsing_loop_body, label);
  return new WhileNode(while_pos, label, cond_expr, while_body);
}


AstNode* Parser::ParseDoWhileStatement(String* label_name) {
  TRACE_PARSER("ParseDoWhileStatement");
  const intptr_t do_pos = TokenPos();
  SourceLabel* label =
      SourceLabel::New(do_pos, label_name, SourceLabel::kDoWhile);
  ConsumeToken();
  const bool parsing_loop_body =  true;
  SequenceNode* dowhile_body = ParseNestedStatement(parsing_loop_body, label);
  ExpectToken(Token::kWHILE);
  ExpectToken(Token::kLPAREN);
  AstNode* cond_expr = ParseExpr(kAllowConst, kConsumeCascades);
  ExpectToken(Token::kRPAREN);
  ExpectSemicolon();
  return new DoWhileNode(do_pos, label, cond_expr, dowhile_body);
}


AstNode* Parser::ParseForInStatement(intptr_t forin_pos,
                                     SourceLabel* label) {
  TRACE_PARSER("ParseForInStatement");
  bool is_final = (CurrentToken() == Token::kFINAL);
  if (CurrentToken() == Token::kCONST) {
    ErrorMsg("Loop variable cannot be 'const'");
  }
  const String* loop_var_name = NULL;
  LocalVariable* loop_var = NULL;
  intptr_t loop_var_pos = 0;
  if (LookaheadToken(1) == Token::kIN) {
    loop_var_pos = TokenPos();
    loop_var_name = ExpectIdentifier("variable name expected");
  } else {
    // The case without a type is handled above, so require a type here.
    const AbstractType& type =
        AbstractType::ZoneHandle(ParseConstFinalVarOrType(
            FLAG_enable_type_checks ? ClassFinalizer::kCanonicalize :
                                      ClassFinalizer::kIgnore));
    loop_var_pos = TokenPos();
    loop_var_name = ExpectIdentifier("variable name expected");
    loop_var = new LocalVariable(loop_var_pos, *loop_var_name, type);
    if (is_final) {
      loop_var->set_is_final();
    }
  }
  ExpectToken(Token::kIN);
  const intptr_t collection_pos = TokenPos();
  AstNode* collection_expr = ParseExpr(kAllowConst, kConsumeCascades);
  ExpectToken(Token::kRPAREN);

  OpenBlock();  // Implicit block around while loop.

  // Generate implicit iterator variable and add to scope.
  // We could set the type of the implicit iterator variable to Iterator<T>
  // where T is the type of the for loop variable. However, the type error
  // would refer to the compiler generated iterator and could confuse the user.
  // It is better to leave the iterator untyped and postpone the type error
  // until the loop variable is assigned to.
  const AbstractType& iterator_type = Type::ZoneHandle(Type::DynamicType());
  LocalVariable* iterator_var =
      new LocalVariable(collection_pos, Symbols::ForInIter(), iterator_type);
  current_block_->scope->AddVariable(iterator_var);

  // Generate initialization of iterator variable.
  ArgumentListNode* no_args = new ArgumentListNode(collection_pos);
  AstNode* get_iterator = new InstanceGetterNode(
      collection_pos, collection_expr, Symbols::GetIterator());
  AstNode* iterator_init =
      new StoreLocalNode(collection_pos, iterator_var, get_iterator);
  current_block_->statements->Add(iterator_init);

  // Generate while loop condition.
  AstNode* iterator_moveNext = new InstanceCallNode(
      collection_pos,
      new LoadLocalNode(collection_pos, iterator_var),
      Symbols::MoveNext(),
      no_args);

  // Parse the for loop body. Ideally, we would use ParseNestedStatement()
  // here, but that does not work well because we have to insert an implicit
  // variable assignment and potentially a variable declaration in the
  // loop body.
  OpenLoopBlock();
  current_block_->scope->AddLabel(label);

  AstNode* iterator_current = new InstanceGetterNode(
      collection_pos,
      new LoadLocalNode(collection_pos, iterator_var),
      Symbols::Current());

  // Generate assignment of next iterator value to loop variable.
  AstNode* loop_var_assignment = NULL;
  if (loop_var != NULL) {
    // The for loop declares a new variable. Add it to the loop body scope.
    current_block_->scope->AddVariable(loop_var);
    loop_var_assignment =
        new StoreLocalNode(loop_var_pos, loop_var, iterator_current);
  } else {
    AstNode* loop_var_primary =
        ResolveIdent(loop_var_pos, *loop_var_name, false);
    ASSERT(!loop_var_primary->IsPrimaryNode());
    loop_var_assignment = CreateAssignmentNode(
        loop_var_primary, iterator_current, loop_var_name, loop_var_pos);
    ASSERT(loop_var_assignment != NULL);
  }
  current_block_->statements->Add(loop_var_assignment);

  // Now parse the for-in loop statement or block.
  if (CurrentToken() == Token::kLBRACE) {
    ConsumeToken();
    ParseStatementSequence();
    ExpectToken(Token::kRBRACE);
  } else {
    AstNode* statement = ParseStatement();
    if (statement != NULL) {
      current_block_->statements->Add(statement);
    }
  }

  SequenceNode* for_loop_statement = CloseBlock();

  AstNode* while_statement =
      new WhileNode(forin_pos, label, iterator_moveNext, for_loop_statement);
  current_block_->statements->Add(while_statement);

  return CloseBlock();  // Implicit block around while loop.
}


AstNode* Parser::ParseForStatement(String* label_name) {
  TRACE_PARSER("ParseForStatement");
  const intptr_t for_pos = TokenPos();
  ConsumeToken();
  ExpectToken(Token::kLPAREN);
  SourceLabel* label = SourceLabel::New(for_pos, label_name, SourceLabel::kFor);
  if (IsForInStatement()) {
    return ParseForInStatement(for_pos, label);
  }
  // Open a block that contains the loop variable. Make it a loop block so
  // that we allocate a new context if the loop variable is captured.
  OpenLoopBlock();
  AstNode* initializer = NULL;
  const intptr_t init_pos = TokenPos();
  LocalScope* init_scope = current_block_->scope;
  if (CurrentToken() != Token::kSEMICOLON) {
    if (IsVariableDeclaration()) {
      initializer = ParseVariableDeclarationList();
    } else {
      initializer = ParseExpr(kAllowConst, kConsumeCascades);
    }
  }
  ExpectSemicolon();
  AstNode* condition = NULL;
  if (CurrentToken() != Token::kSEMICOLON) {
    condition = ParseExpr(kAllowConst, kConsumeCascades);
  }
  ExpectSemicolon();
  AstNode* increment = NULL;
  const intptr_t incr_pos = TokenPos();
  if (CurrentToken() != Token::kRPAREN) {
    increment = ParseExprList();
  }
  ExpectToken(Token::kRPAREN);
  const bool parsing_loop_body =  true;
  SequenceNode* body = ParseNestedStatement(parsing_loop_body, label);

  // Check whether any of the variables in the initializer part of
  // the for statement are captured by a closure. If so, we insert a
  // node that creates a new Context for the loop variable before
  // the increment expression is evaluated.
  for (int i = 0; i < init_scope->num_variables(); i++) {
    if (init_scope->VariableAt(i)->is_captured() &&
        (init_scope->VariableAt(i)->owner() == init_scope)) {
      SequenceNode* incr_sequence = new SequenceNode(incr_pos, NULL);
      incr_sequence->Add(new CloneContextNode(for_pos));
      if (increment != NULL) {
        incr_sequence->Add(increment);
      }
      increment = incr_sequence;
      break;
    }
  }
  AstNode* for_node =
      new ForNode(for_pos,
                  label,
                  NodeAsSequenceNode(init_pos, initializer, NULL),
                  condition,
                  NodeAsSequenceNode(incr_pos, increment, NULL),
                  body);
  current_block_->statements->Add(for_node);
  return CloseBlock();
}


// Calling VM-internal helpers, uses implementation core library.
AstNode* Parser::MakeStaticCall(const String& cls_name,
                                const String& func_name,
                                ArgumentListNode* arguments) {
  const Class& cls = Class::Handle(LookupCoreClass(cls_name));
  ASSERT(!cls.IsNull());
  const Function& func = Function::ZoneHandle(
      Resolver::ResolveStatic(cls,
                              func_name,
                              arguments->length(),
                              arguments->names(),
                              Resolver::kIsQualified));
  ASSERT(!func.IsNull());
  return new StaticCallNode(arguments->token_pos(), func, arguments);
}


AstNode* Parser::MakeAssertCall(intptr_t begin, intptr_t end) {
  ArgumentListNode* arguments = new ArgumentListNode(begin);
  arguments->Add(new LiteralNode(begin,
      Integer::ZoneHandle(Integer::New(begin))));
  arguments->Add(new LiteralNode(end,
      Integer::ZoneHandle(Integer::New(end))));
  return MakeStaticCall(Symbols::AssertionError(),
                        PrivateCoreLibName(Symbols::ThrowNew()),
                        arguments);
}


AstNode* Parser::InsertClosureCallNodes(AstNode* condition) {
  if (condition->IsClosureNode() ||
      (condition->IsStoreLocalNode() &&
       condition->AsStoreLocalNode()->value()->IsClosureNode())) {
    EnsureSavedCurrentContext();
    // Function literal in assert implies a call.
    const intptr_t pos = condition->token_pos();
    condition = new ClosureCallNode(pos, condition, new ArgumentListNode(pos));
  } else if (condition->IsConditionalExprNode()) {
    ConditionalExprNode* cond_expr = condition->AsConditionalExprNode();
    cond_expr->set_true_expr(InsertClosureCallNodes(cond_expr->true_expr()));
    cond_expr->set_false_expr(InsertClosureCallNodes(cond_expr->false_expr()));
  }
  return condition;
}


AstNode* Parser::ParseAssertStatement() {
  TRACE_PARSER("ParseAssertStatement");
  ConsumeToken();  // Consume assert keyword.
  ExpectToken(Token::kLPAREN);
  const intptr_t condition_pos = TokenPos();
  if (!FLAG_enable_asserts && !FLAG_enable_type_checks) {
    SkipExpr();
    ExpectToken(Token::kRPAREN);
    return NULL;
  }
  AstNode* condition = ParseExpr(kAllowConst, kConsumeCascades);
  const intptr_t condition_end = TokenPos();
  ExpectToken(Token::kRPAREN);
  condition = InsertClosureCallNodes(condition);
  condition = new UnaryOpNode(condition_pos, Token::kNOT, condition);
  AstNode* assert_throw = MakeAssertCall(condition_pos, condition_end);
  return new IfNode(condition_pos,
                    condition,
                    NodeAsSequenceNode(condition_pos, assert_throw, NULL),
                    NULL);
}


struct CatchParamDesc {
  CatchParamDesc()
      : token_pos(0), type(NULL), var(NULL) { }
  intptr_t token_pos;
  const AbstractType* type;
  const String* var;
};


// Populate local scope of the catch block with the catch parameters.
void Parser::AddCatchParamsToScope(const CatchParamDesc& exception_param,
                                   const CatchParamDesc& stack_trace_param,
                                   LocalScope* scope) {
  if (exception_param.var != NULL) {
    LocalVariable* var = new LocalVariable(exception_param.token_pos,
                                           *exception_param.var,
                                           *exception_param.type);
    var->set_is_final();
    bool added_to_scope = scope->AddVariable(var);
    ASSERT(added_to_scope);
  }
  if (stack_trace_param.var != NULL) {
    LocalVariable* var = new LocalVariable(TokenPos(),
                                           *stack_trace_param.var,
                                           *stack_trace_param.type);
    var->set_is_final();
    bool added_to_scope = scope->AddVariable(var);
    if (!added_to_scope) {
      ErrorMsg(stack_trace_param.token_pos,
               "name '%s' already exists in scope",
               stack_trace_param.var->ToCString());
    }
  }
}


SequenceNode* Parser::ParseFinallyBlock() {
  TRACE_PARSER("ParseFinallyBlock");
  OpenBlock();
  ExpectToken(Token::kLBRACE);
  ParseStatementSequence();
  ExpectToken(Token::kRBRACE);
  SequenceNode* finally_block = CloseBlock();
  return finally_block;
}


void Parser::PushTryBlock(Block* try_block) {
  intptr_t try_index = AllocateTryIndex();
  TryBlocks* block = new TryBlocks(try_block, try_blocks_list_, try_index);
  try_blocks_list_ = block;
}


Parser::TryBlocks* Parser::PopTryBlock() {
  TryBlocks* innermost_try_block = try_blocks_list_;
  try_blocks_list_ = try_blocks_list_->outer_try_block();
  return innermost_try_block;
}


void Parser::AddNodeForFinallyInlining(AstNode* node) {
  if (node == NULL) {
    return;
  }
  ASSERT(node->IsReturnNode() || node->IsJumpNode());
  TryBlocks* iterator = try_blocks_list_;
  while (iterator != NULL) {
    // For continue and break node check if the target label is in scope.
    if (node->IsJumpNode()) {
      SourceLabel* label = node->AsJumpNode()->label();
      ASSERT(label != NULL);
      LocalScope* try_scope = iterator->try_block()->scope;
      // If the label is defined in a scope which is a child (nested scope)
      // of the try scope then we are not breaking out of this try block
      // so we do not need to inline the finally code. Otherwise we need
      // to inline the finally code of this try block and then move on to the
      // next outer try block.
      if (label->owner()->IsNestedWithin(try_scope)) {
        break;
      }
    }
    iterator->AddNodeForFinallyInlining(node);
    iterator = iterator->outer_try_block();
  }
}


// Add the inlined finally block to the specified node.
void Parser::AddFinallyBlockToNode(AstNode* node,
                                   InlinedFinallyNode* finally_node) {
  if (node->IsReturnNode()) {
    node->AsReturnNode()->AddInlinedFinallyNode(finally_node);
  } else {
    ASSERT(node->IsJumpNode());
    node->AsJumpNode()->AddInlinedFinallyNode(finally_node);
  }
}


AstNode* Parser::ParseTryStatement(String* label_name) {
  TRACE_PARSER("ParseTryStatement");

  // We create three stack slots for exceptions here:
  // ':saved_try_context_var' - Used to save the context before start of the try
  //                            block. The context register is restored from
  //                            this slot before processing the catch block
  //                            handler.
  // ':exception_var' - Used to save the current exception object that was
  //                    thrown.
  // ':stacktrace_var' - Used to save the current stack trace object into which
  //                     the stack trace was copied into when an exception was
  //                     thrown.
  // :exception_var and :stacktrace_var get set with the exception object
  // and the stacktrace object when an exception is thrown.
  // These three implicit variables can never be captured variables.
  LocalVariable* context_var =
      current_block_->scope->LocalLookupVariable(Symbols::SavedTryContextVar());
  if (context_var == NULL) {
    context_var = new LocalVariable(TokenPos(),
                                    Symbols::SavedTryContextVar(),
                                    Type::ZoneHandle(Type::DynamicType()));
    current_block_->scope->AddVariable(context_var);
  }
  LocalVariable* catch_excp_var =
      current_block_->scope->LocalLookupVariable(Symbols::ExceptionVar());
  if (catch_excp_var == NULL) {
    catch_excp_var = new LocalVariable(TokenPos(),
                                       Symbols::ExceptionVar(),
                                       Type::ZoneHandle(Type::DynamicType()));
    current_block_->scope->AddVariable(catch_excp_var);
  }
  LocalVariable* catch_trace_var =
      current_block_->scope->LocalLookupVariable(Symbols::StacktraceVar());
  if (catch_trace_var == NULL) {
    catch_trace_var = new LocalVariable(TokenPos(),
                                        Symbols::StacktraceVar(),
                                        Type::ZoneHandle(Type::DynamicType()));
    current_block_->scope->AddVariable(catch_trace_var);
  }

  const intptr_t try_pos = TokenPos();
  ConsumeToken();  // Consume the 'try'.

  SourceLabel* try_label = NULL;
  if (label_name != NULL) {
    try_label = SourceLabel::New(try_pos, label_name, SourceLabel::kStatement);
    OpenBlock();
    current_block_->scope->AddLabel(try_label);
  }

  // Now parse the 'try' block.
  OpenBlock();
  Block* current_try_block = current_block_;
  PushTryBlock(current_try_block);
  ExpectToken(Token::kLBRACE);
  ParseStatementSequence();
  ExpectToken(Token::kRBRACE);
  SequenceNode* try_block = CloseBlock();

  // Now create a label for the end of catch block processing so that we can
  // jump over the catch block code after executing the try block.
  SourceLabel* end_catch_label =
      SourceLabel::New(TokenPos(), NULL, SourceLabel::kCatch);

  // Now parse the 'catch' blocks if any and merge all of them into
  // an if-then sequence of the different types specified using the 'is'
  // operator.
  bool catch_seen = false;
  bool generic_catch_seen = false;
  SequenceNode* catch_handler_list = NULL;
  const intptr_t handler_pos = TokenPos();
  OpenBlock();  // Start the catch block sequence.
  current_block_->scope->AddLabel(end_catch_label);
  const GrowableObjectArray& handler_types =
      GrowableObjectArray::Handle(GrowableObjectArray::New());
  bool needs_stacktrace = false;
  while ((CurrentToken() == Token::kCATCH) || IsLiteral("on")) {
    const intptr_t catch_pos = TokenPos();
    CatchParamDesc exception_param;
    CatchParamDesc stack_trace_param;
    catch_seen = true;
    if (IsLiteral("on")) {
      ConsumeToken();
      exception_param.type = &AbstractType::ZoneHandle(
          ParseType(ClassFinalizer::kCanonicalize));
    } else {
      exception_param.type =
          &AbstractType::ZoneHandle(Type::DynamicType());
    }
    if (CurrentToken() == Token::kCATCH) {
      ConsumeToken();  // Consume the 'catch'.
      ExpectToken(Token::kLPAREN);
      exception_param.token_pos = TokenPos();
      exception_param.var = ExpectIdentifier("identifier expected");
      if (CurrentToken() == Token::kCOMMA) {
        ConsumeToken();
        // TODO(hausner): Make implicit type be StackTrace, not dynamic.
        stack_trace_param.type =
            &AbstractType::ZoneHandle(Type::DynamicType());
        stack_trace_param.token_pos = TokenPos();
        stack_trace_param.var = ExpectIdentifier("identifier expected");
      }
      ExpectToken(Token::kRPAREN);
    }

    // If a generic "catch all" statement has already been seen then all
    // subsequent catch statements are dead. We issue an error for now,
    // it might make sense to turn this into a warning.
    if (generic_catch_seen) {
      ErrorMsg("a generic 'catch all' statement already exists for this "
               "try block. All subsequent catch statements are dead code");
    }
    OpenBlock();
    AddCatchParamsToScope(exception_param,
                          stack_trace_param,
                          current_block_->scope);

    // Parse the individual catch handler code and add an unconditional
    // JUMP to the end of the try block.
    ExpectToken(Token::kLBRACE);
    OpenBlock();

    if (exception_param.var != NULL) {
      // Generate code to load the exception object (:exception_var) into
      // the exception variable specified in this block.
      LocalVariable* var = LookupLocalScope(*exception_param.var);
      ASSERT(var != NULL);
      ASSERT(catch_excp_var != NULL);
      current_block_->statements->Add(
          new StoreLocalNode(catch_pos, var,
                             new LoadLocalNode(catch_pos, catch_excp_var)));
    }
    if (stack_trace_param.var != NULL) {
      // A stack trace variable is specified in this block, so generate code
      // to load the stack trace object (:stacktrace_var) into the stack trace
      // variable specified in this block.
      needs_stacktrace = true;
      ArgumentListNode* no_args = new ArgumentListNode(catch_pos);
      LocalVariable* trace = LookupLocalScope(*stack_trace_param.var);
      ASSERT(catch_trace_var != NULL);
      current_block_->statements->Add(
          new StoreLocalNode(catch_pos, trace,
                             new LoadLocalNode(catch_pos, catch_trace_var)));
      current_block_->statements->Add(
          new InstanceCallNode(
              catch_pos,
              new LoadLocalNode(catch_pos, trace),
              PrivateCoreLibName(Symbols::_setupFullStackTrace()),
              no_args));
    }

    ParseStatementSequence();  // Parse the catch handler code.
    current_block_->statements->Add(
        new JumpNode(catch_pos, Token::kCONTINUE, end_catch_label));
    SequenceNode* catch_handler = CloseBlock();
    ExpectToken(Token::kRBRACE);

    if (!exception_param.type->IsDynamicType()) {  // Has a type specification.
      // Now form an 'if type check' as an exception type exists in
      // the catch specifier.
      if (!exception_param.type->IsInstantiated() &&
          (current_block_->scope->function_level() > 0)) {
        // Make sure that the instantiator is captured.
        CaptureInstantiator();
      }
      TypeNode* exception_type = new TypeNode(catch_pos, *exception_param.type);
      AstNode* exception_var = new LoadLocalNode(catch_pos, catch_excp_var);
      if (!exception_type->type().IsInstantiated()) {
        EnsureExpressionTemp();
      }
      AstNode* type_cond_expr = new ComparisonNode(
          catch_pos, Token::kIS, exception_var, exception_type);
      current_block_->statements->Add(
          new IfNode(catch_pos, type_cond_expr, catch_handler, NULL));

      // Do not add uninstantiated types (e.g. type parameter T or
      // generic type List<T>), since the debugger won't be able to
      // instantiate it when walking the stack.
      // This means that the debugger is not able to determine whether
      // an exception is caught if the catch clause uses generic types.
      // It will report the exception as uncaught when in fact it might
      // be caught and handled when we unwind the stack.
      if (exception_param.type->IsInstantiated()) {
        handler_types.Add(*exception_param.type);
      }
    } else {
      // No exception type exists in the catch specifier so execute the
      // catch handler code unconditionally.
      current_block_->statements->Add(catch_handler);
      generic_catch_seen = true;
      // This catch clause will handle all exceptions. We can safely forget
      // all previous catch clause types.
      handler_types.SetLength(0);
      handler_types.Add(*exception_param.type);
    }
    SequenceNode* catch_clause = CloseBlock();

    // Add this individual catch handler to the catch handlers list.
    current_block_->statements->Add(catch_clause);
  }
  catch_handler_list = CloseBlock();
  TryBlocks* inner_try_block = PopTryBlock();
  intptr_t try_index = inner_try_block->try_index();
  TryBlocks* outer_try_block = try_blocks_list_;
  intptr_t outer_try_index = (outer_try_block != NULL)
      ? outer_try_block->try_index()
      : CatchClauseNode::kInvalidTryIndex;

  // Finally parse the 'finally' block.
  SequenceNode* finally_block = NULL;
  if (CurrentToken() == Token::kFINALLY) {
    current_function().set_has_finally(true);
    ConsumeToken();  // Consume the 'finally'.
    const intptr_t finally_pos = TokenPos();
    // Add the finally block to the exit points recorded so far.
    intptr_t node_index = 0;
    AstNode* node_to_inline =
        inner_try_block->GetNodeToInlineFinally(node_index);
    while (node_to_inline != NULL) {
      finally_block = ParseFinallyBlock();
      InlinedFinallyNode* node = new InlinedFinallyNode(finally_pos,
                                                        finally_block,
                                                        context_var,
                                                        outer_try_index);
      AddFinallyBlockToNode(node_to_inline, node);
      node_index += 1;
      node_to_inline = inner_try_block->GetNodeToInlineFinally(node_index);
      tokens_iterator_.SetCurrentPosition(finally_pos);
    }
    finally_block = ParseFinallyBlock();
  } else {
    if (!catch_seen) {
      ErrorMsg("catch or finally clause expected");
    }
  }

  if (!generic_catch_seen) {
    // No generic catch handler exists so rethrow the exception so that
    // the next catch handler can deal with it.
    catch_handler_list->Add(
        new ThrowNode(handler_pos,
                      new LoadLocalNode(handler_pos, catch_excp_var),
                      new LoadLocalNode(handler_pos, catch_trace_var)));
  }
  CatchClauseNode* catch_block =
      new CatchClauseNode(handler_pos,
                          catch_handler_list,
                          Array::ZoneHandle(Array::MakeArray(handler_types)),
                          context_var,
                          catch_excp_var,
                          catch_trace_var,
                          (finally_block != NULL)
                              ? AllocateTryIndex()
                              : CatchClauseNode::kInvalidTryIndex,
                          needs_stacktrace);

  // Now create the try/catch ast node and return it. If there is a label
  // on the try/catch, close the block that's embedding the try statement
  // and attach the label to it.
  AstNode* try_catch_node =
      new TryCatchNode(try_pos, try_block, end_catch_label,
                       context_var, catch_block, finally_block, try_index);

  if (try_label != NULL) {
    current_block_->statements->Add(try_catch_node);
    SequenceNode* sequence = CloseBlock();
    sequence->set_label(try_label);
    try_catch_node = sequence;
  }
  return try_catch_node;
}


AstNode* Parser::ParseJump(String* label_name) {
  TRACE_PARSER("ParseJump");
  ASSERT(CurrentToken() == Token::kBREAK || CurrentToken() == Token::kCONTINUE);
  Token::Kind jump_kind = CurrentToken();
  const intptr_t jump_pos = TokenPos();
  SourceLabel* target = NULL;
  ConsumeToken();
  if (IsIdentifier()) {
    // Explicit label after break/continue.
    const String& target_name = *CurrentLiteral();
    ConsumeToken();
    // Handle pathological cases first.
    if (label_name != NULL && target_name.Equals(*label_name)) {
      if (jump_kind == Token::kCONTINUE) {
        ErrorMsg(jump_pos, "'continue' jump to label '%s' is illegal",
                 target_name.ToCString());
      }
      // L: break L; is a no-op.
      return NULL;
    }
    target = current_block_->scope->LookupLabel(target_name);
    if (target == NULL && jump_kind == Token::kCONTINUE) {
      // Either a reference to a non-existent label, or a forward reference
      // to a case label that we haven't seen yet. If we are inside a switch
      // statement, create a "forward reference" label in the scope of
      // the switch statement.
      LocalScope* switch_scope = current_block_->scope->LookupSwitchScope();
      if (switch_scope != NULL) {
        // We found a switch scope. Enter a forward reference to the label.
        target = new SourceLabel(
            TokenPos(), target_name, SourceLabel::kForward);
        switch_scope->AddLabel(target);
      }
    }
    if (target == NULL) {
      ErrorMsg(jump_pos, "label '%s' not found", target_name.ToCString());
    }
  } else {
    target = current_block_->scope->LookupInnermostLabel(jump_kind);
    if (target == NULL) {
      ErrorMsg(jump_pos, "'%s' is illegal here", Token::Str(jump_kind));
    }
  }
  ASSERT(target != NULL);
  if (jump_kind == Token::kCONTINUE) {
    if (target->kind() == SourceLabel::kSwitch) {
      ErrorMsg(jump_pos, "'continue' jump to switch statement is illegal");
    } else if (target->kind() == SourceLabel::kStatement) {
      ErrorMsg(jump_pos, "'continue' jump to label '%s' is illegal",
               target->name().ToCString());
    }
  }
  if (jump_kind == Token::kBREAK && target->kind() == SourceLabel::kCase) {
    ErrorMsg(jump_pos, "'break' to case clause label is illegal");
  }
  if (target->FunctionLevel() != current_block_->scope->function_level()) {
    ErrorMsg(jump_pos, "'%s' target must be in same function context",
             Token::Str(jump_kind));
  }
  return new JumpNode(jump_pos, jump_kind, target);
}


AstNode* Parser::ParseStatement() {
  TRACE_PARSER("ParseStatement");
  AstNode* statement = NULL;
  intptr_t label_pos = 0;
  String* label_name = NULL;
  if (IsIdentifier()) {
    if (LookaheadToken(1) == Token::kCOLON) {
      // Statement starts with a label.
      label_name = CurrentLiteral();
      label_pos = TokenPos();
      ASSERT(label_pos > 0);
      ConsumeToken();  // Consume identifier.
      ConsumeToken();  // Consume colon.
    }
  }
  const intptr_t statement_pos = TokenPos();

  if (CurrentToken() == Token::kWHILE) {
    statement = ParseWhileStatement(label_name);
  } else if (CurrentToken() == Token::kFOR) {
    statement = ParseForStatement(label_name);
  } else if (CurrentToken() == Token::kDO) {
    statement = ParseDoWhileStatement(label_name);
  } else if (CurrentToken() == Token::kSWITCH) {
    statement = ParseSwitchStatement(label_name);
  } else if (CurrentToken() == Token::kTRY) {
    statement = ParseTryStatement(label_name);
  } else if (CurrentToken() == Token::kRETURN) {
    const intptr_t return_pos = TokenPos();
    ConsumeToken();
    if (CurrentToken() != Token::kSEMICOLON) {
      if (current_function().IsConstructor() &&
          (current_block_->scope->function_level() == 0)) {
        ErrorMsg(return_pos, "return of a value not allowed in constructors");
      }
      AstNode* expr = ParseExpr(kAllowConst, kConsumeCascades);
      statement = new ReturnNode(statement_pos, expr);
    } else {
      statement = new ReturnNode(statement_pos);
    }
    AddNodeForFinallyInlining(statement);
    ExpectSemicolon();
  } else if (CurrentToken() == Token::kIF) {
    statement = ParseIfStatement(label_name);
  } else if (CurrentToken() == Token::kASSERT) {
    statement = ParseAssertStatement();
    ExpectSemicolon();
  } else if (IsVariableDeclaration()) {
    statement = ParseVariableDeclarationList();
    ExpectSemicolon();
  } else if (IsFunctionDeclaration()) {
    statement = ParseFunctionStatement(false);
  } else if (CurrentToken() == Token::kLBRACE) {
    SourceLabel* label = NULL;
    OpenBlock();
    if (label_name != NULL) {
      label = SourceLabel::New(label_pos, label_name, SourceLabel::kStatement);
      current_block_->scope->AddLabel(label);
    }
    ConsumeToken();
    ParseStatementSequence();
    statement = CloseBlock();
    if (label != NULL) {
      statement->AsSequenceNode()->set_label(label);
    }
    ExpectToken(Token::kRBRACE);
  } else if (CurrentToken() == Token::kBREAK) {
    statement = ParseJump(label_name);
    AddNodeForFinallyInlining(statement);
    ExpectSemicolon();
  } else if (CurrentToken() == Token::kCONTINUE) {
    statement = ParseJump(label_name);
    AddNodeForFinallyInlining(statement);
    ExpectSemicolon();
  } else if (CurrentToken() == Token::kSEMICOLON) {
    // Empty statement, nothing to do.
    ConsumeToken();
  } else if ((CurrentToken() == Token::kRETHROW) ||
            ((CurrentToken() == Token::kTHROW) &&
             (LookaheadToken(1) == Token::kSEMICOLON))) {
    // Rethrow of current exception. Throwing of an exception object
    // is an expression and is handled in ParseExpr().
    // TODO(hausner): remove support for 'throw;'.
    ConsumeToken();
    ExpectSemicolon();
    // Check if it is ok to do a rethrow.
    SourceLabel* label = current_block_->scope->LookupInnermostCatchLabel();
    if (label == NULL ||
        label->FunctionLevel() != current_block_->scope->function_level()) {
      ErrorMsg(statement_pos, "rethrow of an exception is not valid here");
    }
    ASSERT(label->owner() != NULL);
    LocalScope* scope = label->owner()->parent();
    ASSERT(scope != NULL);
    LocalVariable* excp_var =
        scope->LocalLookupVariable(Symbols::ExceptionVar());
    ASSERT(excp_var != NULL);
    LocalVariable* trace_var =
        scope->LocalLookupVariable(Symbols::StacktraceVar());
    ASSERT(trace_var != NULL);
    statement = new ThrowNode(statement_pos,
                              new LoadLocalNode(statement_pos, excp_var),
                              new LoadLocalNode(statement_pos, trace_var));
  } else {
    statement = ParseExpr(kAllowConst, kConsumeCascades);
    ExpectSemicolon();
  }
  return statement;
}


RawError* Parser::FormatErrorWithAppend(const Error& prev_error,
                                        const Script& script,
                                        intptr_t token_pos,
                                        const char* message_header,
                                        const char* format,
                                        va_list args) {
  const String& msg1 = String::Handle(String::New(prev_error.ToErrorCString()));
  const String& msg2 = String::Handle(
      FormatMessage(script, token_pos, message_header, format, args));
  return LanguageError::New(String::Handle(String::Concat(msg1, msg2)));
}


RawError* Parser::FormatError(const Script& script,
                              intptr_t token_pos,
                              const char* message_header,
                              const char* format,
                              va_list args) {
  const String& msg = String::Handle(
      FormatMessage(script, token_pos, message_header, format, args));
  return LanguageError::New(msg);
}


RawError* Parser::FormatErrorMsg(const Script& script,
                                 intptr_t token_pos,
                                 const char* message_header,
                                 const char* format, ...) {
  va_list args;
  va_start(args, format);
  const Error& error = Error::Handle(
      FormatError(script, token_pos, message_header, format, args));
  va_end(args);
  return error.raw();
}


RawString* Parser::FormatMessage(const Script& script,
                                 intptr_t token_pos,
                                 const char* message_header,
                                 const char* format, va_list args) {
  String& result = String::Handle();
  const String& msg = String::Handle(String::NewFormattedV(format, args));
  if (!script.IsNull()) {
    const String& script_url = String::Handle(script.url());
    if (token_pos >= 0) {
      intptr_t line, column;
      script.GetTokenLocation(token_pos, &line, &column);
      result = String::NewFormatted("'%s': %s: line %" Pd " pos %" Pd ": ",
                                    script_url.ToCString(),
                                    message_header,
                                    line,
                                    column);
      // Append the formatted error or warning message.
      result = String::Concat(result, msg);
      const String& new_line = String::Handle(String::New("\n"));
      // Append the source line.
      const String& script_line = String::Handle(script.GetLine(line));
      ASSERT(!script_line.IsNull());
      result = String::Concat(result, new_line);
      result = String::Concat(result, script_line);
      result = String::Concat(result, new_line);
      // Append the column marker.
      const String& column_line = String::Handle(
          String::NewFormatted("%*s\n", static_cast<int>(column), "^"));
      result = String::Concat(result, column_line);
    } else {
      // Token position is unknown.
      result = String::NewFormatted("'%s': %s: ",
                                    script_url.ToCString(),
                                    message_header);
      result = String::Concat(result, msg);
    }
  } else {
    // Script is unknown.
    // Append the formatted error or warning message.
    result = msg.raw();
  }
  return result.raw();
}


void Parser::PrintMessage(const Script& script,
                          intptr_t token_pos,
                          const char* message_header,
                          const char* format, ...) {
  va_list args;
  va_start(args, format);
  const String& buf = String::Handle(
      FormatMessage(script, token_pos, message_header, format, args));
  va_end(args);
  OS::Print("%s", buf.ToCString());
}


void Parser::ErrorMsg(intptr_t token_pos, const char* format, ...) const {
  va_list args;
  va_start(args, format);
  const Error& error = Error::Handle(
      FormatError(script_, token_pos, "Error", format, args));
  va_end(args);
  isolate()->long_jump_base()->Jump(1, error);
  UNREACHABLE();
}


void Parser::ErrorMsg(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const Error& error = Error::Handle(
      FormatError(script_, TokenPos(), "Error", format, args));
  va_end(args);
  isolate()->long_jump_base()->Jump(1, error);
  UNREACHABLE();
}


void Parser::ErrorMsg(const Error& error) {
  Isolate::Current()->long_jump_base()->Jump(1, error);
  UNREACHABLE();
}


void Parser::AppendErrorMsg(
      const Error& prev_error, intptr_t token_pos, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const Error& error = Error::Handle(FormatErrorWithAppend(
      prev_error, script_, token_pos, "Error", format, args));
  va_end(args);
  isolate()->long_jump_base()->Jump(1, error);
  UNREACHABLE();
}


void Parser::Warning(intptr_t token_pos, const char* format, ...) {
  if (FLAG_silent_warnings) return;
  va_list args;
  va_start(args, format);
  const Error& error = Error::Handle(
      FormatError(script_, token_pos, "Warning", format, args));
  va_end(args);
  if (FLAG_warning_as_error) {
    isolate()->long_jump_base()->Jump(1, error);
    UNREACHABLE();
  } else {
    OS::Print("%s", error.ToErrorCString());
  }
}


void Parser::Warning(const char* format, ...) {
  if (FLAG_silent_warnings) return;
  va_list args;
  va_start(args, format);
  const Error& error = Error::Handle(
      FormatError(script_, TokenPos(), "Warning", format, args));
  va_end(args);
  if (FLAG_warning_as_error) {
    isolate()->long_jump_base()->Jump(1, error);
    UNREACHABLE();
  } else {
    OS::Print("%s", error.ToErrorCString());
  }
}


void Parser::Unimplemented(const char* msg) {
  ErrorMsg(TokenPos(), "%s", msg);
}


void Parser::ExpectToken(Token::Kind token_expected) {
  if (CurrentToken() != token_expected) {
    ErrorMsg("'%s' expected", Token::Str(token_expected));
  }
  ConsumeToken();
}


void Parser::ExpectSemicolon() {
  if (CurrentToken() != Token::kSEMICOLON) {
    ErrorMsg("semicolon expected");
  }
  ConsumeToken();
}


void Parser::UnexpectedToken() {
  ErrorMsg("unexpected token '%s'",
           CurrentToken() == Token::kIDENT ?
               CurrentLiteral()->ToCString() : Token::Str(CurrentToken()));
}


String* Parser::ExpectUserDefinedTypeIdentifier(const char* msg) {
  if (CurrentToken() != Token::kIDENT) {
    ErrorMsg("%s", msg);
  }
  String* ident = CurrentLiteral();
  if (ident->Equals("dynamic")) {
    ErrorMsg("%s", msg);
  }
  ConsumeToken();
  return ident;
}


// Check whether current token is an identifier or a built-in identifier.
String* Parser::ExpectIdentifier(const char* msg) {
  if (!IsIdentifier()) {
    ErrorMsg("%s", msg);
  }
  String* ident = CurrentLiteral();
  ConsumeToken();
  return ident;
}


bool Parser::IsLiteral(const char* literal) {
  return IsIdentifier() && CurrentLiteral()->Equals(literal);
}


static bool IsIncrementOperator(Token::Kind token) {
  return token == Token::kINCR || token == Token::kDECR;
}


static bool IsPrefixOperator(Token::Kind token) {
  return (token == Token::kSUB) ||
         (token == Token::kNOT) ||
         (token == Token::kBIT_NOT);
}


SequenceNode* Parser::NodeAsSequenceNode(intptr_t sequence_pos,
                                         AstNode* node,
                                         LocalScope* scope) {
  if ((node == NULL) || !node->IsSequenceNode()) {
    SequenceNode* sequence = new SequenceNode(sequence_pos, scope);
    if (node != NULL) {
      sequence->Add(node);
    }
    return sequence;
  }
  return node->AsSequenceNode();
}


AstNode* Parser::ThrowTypeError(intptr_t type_pos, const AbstractType& type) {
  ArgumentListNode* arguments = new ArgumentListNode(type_pos);
  // Location argument.
  arguments->Add(new LiteralNode(
      type_pos, Integer::ZoneHandle(Integer::New(type_pos))));
  // Src value argument.
  arguments->Add(new LiteralNode(type_pos, Instance::ZoneHandle()));
  // Dst type name argument.
  arguments->Add(new LiteralNode(type_pos, Symbols::Malformed()));
  // Dst name argument.
  arguments->Add(new LiteralNode(type_pos, Symbols::Empty()));
  // Malformed type error or malbounded type error.
  Error& error = Error::Handle();
  if (type.IsMalformed()) {
    error = type.malformed_error();
  } else {
    const bool is_malbounded = type.IsMalboundedWithError(&error);
    ASSERT(is_malbounded);
  }
  arguments->Add(new LiteralNode(type_pos, String::ZoneHandle(
      Symbols::New(error.ToErrorCString()))));
  return MakeStaticCall(Symbols::TypeError(),
                        PrivateCoreLibName(Symbols::ThrowNew()),
                        arguments);
}


AstNode* Parser::ThrowNoSuchMethodError(intptr_t call_pos,
                                        const Class& cls,
                                        const String& function_name,
                                        ArgumentListNode* function_arguments,
                                        InvocationMirror::Call im_call,
                                        InvocationMirror::Type im_type) {
  ArgumentListNode* arguments = new ArgumentListNode(call_pos);
  // Object receiver.
  // TODO(regis): For now, we pass a class literal of the unresolved
  // method's owner, but this is not specified and will probably change.
  Type& type = Type::ZoneHandle(
      Type::New(cls, TypeArguments::Handle(), call_pos, Heap::kOld));
  type ^= ClassFinalizer::FinalizeType(
      current_class(), type, ClassFinalizer::kCanonicalize);
  arguments->Add(new LiteralNode(call_pos, type));
  // String memberName.
  arguments->Add(new LiteralNode(
      call_pos, String::ZoneHandle(Symbols::New(function_name))));
  // Smi invocation_type.
  if (cls.IsTopLevel()) {
    ASSERT(im_call == InvocationMirror::kStatic ||
           im_call == InvocationMirror::kTopLevel);
    im_call = InvocationMirror::kTopLevel;
  }
  arguments->Add(new LiteralNode(call_pos, Smi::ZoneHandle(
      Smi::New(InvocationMirror::EncodeType(im_call, im_type)))));
  // List arguments.
  if (function_arguments == NULL) {
    arguments->Add(new LiteralNode(call_pos, Array::ZoneHandle()));
  } else {
    ArrayNode* array = new ArrayNode(call_pos,
                                     Type::ZoneHandle(Type::ArrayType()),
                                     function_arguments->nodes());
    arguments->Add(array);
  }
  // List argumentNames.
  if (function_arguments == NULL) {
    arguments->Add(new LiteralNode(call_pos, Array::ZoneHandle()));
  } else {
    arguments->Add(new LiteralNode(call_pos, function_arguments->names()));
  }
  // List existingArgumentNames.
  // Check if there exists a function with the same name.
  Function& function =
     Function::Handle(cls.LookupStaticFunction(function_name));
  if (function.IsNull()) {
    arguments->Add(new LiteralNode(call_pos, Array::ZoneHandle()));
  } else {
    const int total_num_parameters = function.NumParameters();
    Array& array =
        Array::ZoneHandle(Array::New(total_num_parameters, Heap::kOld));
    // Skip receiver.
    for (int i = 0; i < total_num_parameters; i++) {
      array.SetAt(i, String::Handle(function.ParameterNameAt(i)));
    }
    arguments->Add(new LiteralNode(call_pos, array));
  }
  return MakeStaticCall(Symbols::NoSuchMethodError(),
                        PrivateCoreLibName(Symbols::ThrowNew()),
                        arguments);
}


AstNode* Parser::ParseBinaryExpr(int min_preced) {
  TRACE_PARSER("ParseBinaryExpr");
  ASSERT(min_preced >= 4);
  AstNode* left_operand = ParseUnaryExpr();
  if (left_operand->IsPrimaryNode() &&
      (left_operand->AsPrimaryNode()->IsSuper())) {
    ErrorMsg(left_operand->token_pos(), "illegal use of 'super'");
  }
  if (IsLiteral("as")) {  // Not a reserved word.
    token_kind_ = Token::kAS;
  }
  int current_preced = Token::Precedence(CurrentToken());
  while (current_preced >= min_preced) {
    while (Token::Precedence(CurrentToken()) == current_preced) {
      Token::Kind op_kind = CurrentToken();
      const intptr_t op_pos = TokenPos();
      ConsumeToken();
      AstNode* right_operand = NULL;
      if ((op_kind != Token::kIS) && (op_kind != Token::kAS)) {
        right_operand = ParseBinaryExpr(current_preced + 1);
      } else {
        // For 'is' and 'as' we expect the right operand to be a type.
        if ((op_kind == Token::kIS) && (CurrentToken() == Token::kNOT)) {
          ConsumeToken();
          op_kind = Token::kISNOT;
        }
        const intptr_t type_pos = TokenPos();
        const AbstractType& type = AbstractType::ZoneHandle(
            ParseType(ClassFinalizer::kCanonicalize));
        if (!type.IsInstantiated() &&
            (current_block_->scope->function_level() > 0)) {
          // Make sure that the instantiator is captured.
          CaptureInstantiator();
        }
        right_operand = new TypeNode(type_pos, type);
        // The type is never malformed (mapped to dynamic), but it can be
        // malbounded in checked mode.
        ASSERT(!type.IsMalformed());
        if (((op_kind == Token::kIS) || (op_kind == Token::kISNOT)) &&
            type.IsMalbounded()) {
          // Note that a type error is thrown even if the tested value is null
          // in a type test. However, no cast exception is thrown if the value
          // is null in a type cast.
          return ThrowTypeError(type_pos, type);
        }
      }
      if (Token::IsRelationalOperator(op_kind)
          || Token::IsTypeTestOperator(op_kind)
          || Token::IsTypeCastOperator(op_kind)
          || Token::IsEqualityOperator(op_kind)) {
        if (Token::IsTypeTestOperator(op_kind) ||
            Token::IsTypeCastOperator(op_kind)) {
          if (!right_operand->AsTypeNode()->type().IsInstantiated()) {
            EnsureExpressionTemp();
          }
        }
        left_operand = new ComparisonNode(
            op_pos, op_kind, left_operand, right_operand);
        break;  // Equality and relational operators cannot be chained.
      } else {
        left_operand = OptimizeBinaryOpNode(
            op_pos, op_kind, left_operand, right_operand);
      }
    }
    current_preced--;
  }
  return left_operand;
}


AstNode* Parser::ParseExprList() {
  TRACE_PARSER("ParseExprList");
  AstNode* expressions = ParseExpr(kAllowConst, kConsumeCascades);
  if (CurrentToken() == Token::kCOMMA) {
    // Collect comma-separated expressions in a non scope owning sequence node.
    SequenceNode* list = new SequenceNode(TokenPos(), NULL);
    list->Add(expressions);
    while (CurrentToken() == Token::kCOMMA) {
      ConsumeToken();
      AstNode* expr = ParseExpr(kAllowConst, kConsumeCascades);
      list->Add(expr);
    }
    expressions = list;
  }
  return expressions;
}


void Parser::EnsureExpressionTemp() {
  // Temporary used later by the flow_graph_builder.
  parsed_function()->EnsureExpressionTemp();
}


void Parser::EnsureSavedCurrentContext() {
  // Used later by the flow_graph_builder to save current context.
  if (!parsed_function()->has_saved_current_context_var()) {
    LocalVariable* temp =
        new LocalVariable(current_function().token_pos(),
                          Symbols::SavedCurrentContextVar(),
                          Type::ZoneHandle(Type::DynamicType()));
    ASSERT(temp != NULL);
    parsed_function()->set_saved_current_context_var(temp);
  }
}


LocalVariable* Parser::CreateTempConstVariable(intptr_t token_pos,
                                               const char* s) {
  char name[64];
  OS::SNPrint(name, 64, ":%s%" Pd, s, token_pos);
  LocalVariable* temp =
      new LocalVariable(token_pos,
                        String::ZoneHandle(Symbols::New(name)),
                        Type::ZoneHandle(Type::DynamicType()));
  temp->set_is_final();
  current_block_->scope->AddVariable(temp);
  return temp;
}


// TODO(srdjan): Implement other optimizations.
AstNode* Parser::OptimizeBinaryOpNode(intptr_t op_pos,
                                      Token::Kind binary_op,
                                      AstNode* lhs,
                                      AstNode* rhs) {
  LiteralNode* lhs_literal = lhs->AsLiteralNode();
  LiteralNode* rhs_literal = rhs->AsLiteralNode();
  if ((lhs_literal != NULL) && (rhs_literal != NULL)) {
    if (lhs_literal->literal().IsDouble() &&
        rhs_literal->literal().IsDouble()) {
      double left_double = Double::Cast(lhs_literal->literal()).value();
      double right_double = Double::Cast(rhs_literal->literal()).value();
      if (binary_op == Token::kDIV) {
        const Double& dbl_obj = Double::ZoneHandle(
            Double::NewCanonical((left_double / right_double)));
        return new LiteralNode(op_pos, dbl_obj);
      }
    }
  }
  if ((binary_op == Token::kAND) || (binary_op == Token::kOR)) {
    EnsureExpressionTemp();
  }
  if (binary_op == Token::kBIT_AND) {
    // Normalize so that rhs is a literal if any is.
    if ((rhs_literal == NULL) && (lhs_literal != NULL)) {
      // Swap.
      LiteralNode* temp = rhs_literal;
      rhs_literal = lhs_literal;
      lhs_literal = temp;
    }
    if ((rhs_literal != NULL) &&
        (rhs_literal->literal().IsSmi() || rhs_literal->literal().IsMint())) {
      const int64_t val = Integer::Cast(rhs_literal->literal()).AsInt64Value();
      if ((0 <= val) && (Utils::IsUint(32, val))) {
        if (lhs->IsBinaryOpNode() &&
            (lhs->AsBinaryOpNode()->kind() == Token::kSHL)) {
          // Merge SHL and BIT_AND into one "SHL with mask" node.
          BinaryOpNode* old = lhs->AsBinaryOpNode();
          BinaryOpWithMask32Node* binop = new BinaryOpWithMask32Node(
              old->token_pos(), old->kind(), old->left(), old->right(), val);
          return binop;
        }
      }
    }
  }
  return new BinaryOpNode(op_pos, binary_op, lhs, rhs);
}


AstNode* Parser::ExpandAssignableOp(intptr_t op_pos,
                                    Token::Kind assignment_op,
                                    AstNode* lhs,
                                    AstNode* rhs) {
  TRACE_PARSER("ExpandAssignableOp");
  switch (assignment_op) {
    case Token::kASSIGN:
      return rhs;
    case Token::kASSIGN_ADD:
      return new BinaryOpNode(op_pos, Token::kADD, lhs, rhs);
    case Token::kASSIGN_SUB:
      return new BinaryOpNode(op_pos, Token::kSUB, lhs, rhs);
    case Token::kASSIGN_MUL:
      return new BinaryOpNode(op_pos, Token::kMUL, lhs, rhs);
    case Token::kASSIGN_TRUNCDIV:
      return new BinaryOpNode(op_pos, Token::kTRUNCDIV, lhs, rhs);
    case Token::kASSIGN_DIV:
      return new BinaryOpNode(op_pos, Token::kDIV, lhs, rhs);
    case Token::kASSIGN_MOD:
      return new BinaryOpNode(op_pos, Token::kMOD, lhs, rhs);
    case Token::kASSIGN_SHR:
      return new BinaryOpNode(op_pos, Token::kSHR, lhs, rhs);
    case Token::kASSIGN_SHL:
      return new BinaryOpNode(op_pos, Token::kSHL, lhs, rhs);
    case Token::kASSIGN_OR:
      return new BinaryOpNode(op_pos, Token::kBIT_OR, lhs, rhs);
    case Token::kASSIGN_AND:
      return new BinaryOpNode(op_pos, Token::kBIT_AND, lhs, rhs);
    case Token::kASSIGN_XOR:
      return new BinaryOpNode(op_pos, Token::kBIT_XOR, lhs, rhs);
    default:
      ErrorMsg(op_pos, "internal error: ExpandAssignableOp '%s' unimplemented",
          Token::Name(assignment_op));
      UNIMPLEMENTED();
      return NULL;
  }
}


// Evaluates the value of the compile time constant expression
// and returns a literal node for the value.
AstNode* Parser::FoldConstExpr(intptr_t expr_pos, AstNode* expr) {
  if (expr->IsLiteralNode()) {
    return expr;
  }
  if (expr->EvalConstExpr() == NULL) {
    ErrorMsg(expr_pos, "expression must be a compile-time constant");
  }
  return new LiteralNode(expr_pos, EvaluateConstExpr(expr));
}


LetNode* Parser::PrepareCompoundAssignmentNodes(AstNode** expr) {
  AstNode* node = *expr;
  intptr_t token_pos = node->token_pos();
  LetNode* result = new LetNode(token_pos);
  if (node->IsLoadIndexedNode()) {
    LoadIndexedNode* load_indexed = node->AsLoadIndexedNode();
    AstNode* array = load_indexed->array();
    AstNode* index = load_indexed->index_expr();
    if (!IsSimpleLocalOrLiteralNode(load_indexed->array())) {
      LocalVariable* t0 = result->AddInitializer(load_indexed->array());
      array = new LoadLocalNode(token_pos, t0);
    }
    if (!IsSimpleLocalOrLiteralNode(load_indexed->index_expr())) {
      LocalVariable* t1 = result->AddInitializer(
          load_indexed->index_expr());
      index = new LoadLocalNode(token_pos, t1);
    }
    *expr = new LoadIndexedNode(token_pos,
                                array,
                                index,
                                load_indexed->super_class());
    return result;
  }
  if (node->IsInstanceGetterNode()) {
    InstanceGetterNode* getter = node->AsInstanceGetterNode();
    AstNode* receiver = getter->receiver();
    if (!IsSimpleLocalOrLiteralNode(getter->receiver())) {
      LocalVariable* t0 = result->AddInitializer(getter->receiver());
      receiver = new LoadLocalNode(token_pos, t0);
    }
    *expr = new InstanceGetterNode(token_pos,
                                   receiver,
                                   getter->field_name());
    return result;
  }
  return result;
}


AstNode* Parser::CreateAssignmentNode(AstNode* original,
                                      AstNode* rhs,
                                      const String* left_ident,
                                      intptr_t left_pos) {
  AstNode* result = original->MakeAssignmentNode(rhs);
  if (result == NULL) {
    String& name = String::ZoneHandle();
    if (original->IsTypeNode()) {
      name = Symbols::New(original->Name());
    } else if ((left_ident != NULL) &&
               (original->IsLiteralNode() ||
                original->IsLoadLocalNode() ||
                original->IsLoadStaticFieldNode())) {
      name = left_ident->raw();
    }
    if (name.IsNull()) {
      ErrorMsg(left_pos, "expression is not assignable");
    }
    result = ThrowNoSuchMethodError(original->token_pos(),
                                    current_class(),
                                    name,
                                    NULL,  // No arguments.
                                    InvocationMirror::kStatic,
                                    InvocationMirror::kSetter);
  } else if (result->IsStoreIndexedNode() ||
             result->IsInstanceSetterNode() ||
             result->IsStaticSetterNode() ||
             result->IsStoreStaticFieldNode() ||
             result->IsStoreLocalNode()) {
    // Ensure that the expression temp is allocated for nodes that may need it.
    EnsureExpressionTemp();
  }
  return result;
}


AstNode* Parser::ParseCascades(AstNode* expr) {
  intptr_t cascade_pos = TokenPos();
  LetNode* cascade = new LetNode(cascade_pos);
  LocalVariable* cascade_receiver_var = cascade->AddInitializer(expr);
  while (CurrentToken() == Token::kCASCADE) {
    cascade_pos = TokenPos();
    LoadLocalNode* load_cascade_receiver =
        new LoadLocalNode(cascade_pos, cascade_receiver_var);
    if (Token::IsIdentifier(LookaheadToken(1))) {
      // Replace .. with . for ParseSelectors().
      token_kind_ = Token::kPERIOD;
    } else if (LookaheadToken(1) == Token::kLBRACK) {
      ConsumeToken();
    } else {
      ErrorMsg("identifier or [ expected after ..");
    }
    String* expr_ident =
        Token::IsIdentifier(CurrentToken()) ? CurrentLiteral() : NULL;
    const intptr_t expr_pos = TokenPos();
    expr = ParseSelectors(load_cascade_receiver, true);

    // Assignments after a cascade are part of the cascade. The
    // assigned expression must not contain cascades.
    if (Token::IsAssignmentOperator(CurrentToken())) {
      Token::Kind assignment_op = CurrentToken();
      const intptr_t assignment_pos = TokenPos();
      ConsumeToken();
      AstNode* right_expr = ParseExpr(kAllowConst, kNoCascades);
      if (assignment_op != Token::kASSIGN) {
        // Compound assignment: store inputs with side effects into
        // temporary locals.
        LetNode* let_expr = PrepareCompoundAssignmentNodes(&expr);
        right_expr =
            ExpandAssignableOp(assignment_pos, assignment_op, expr, right_expr);
        AstNode* assign_expr = CreateAssignmentNode(
            expr, right_expr, expr_ident, expr_pos);
        ASSERT(assign_expr != NULL);
        let_expr->AddNode(assign_expr);
        expr = let_expr;
      } else {
        right_expr =
            ExpandAssignableOp(assignment_pos, assignment_op, expr, right_expr);
        AstNode* assign_expr = CreateAssignmentNode(
            expr, right_expr, expr_ident, expr_pos);
        ASSERT(assign_expr != NULL);
        expr = assign_expr;
      }
    }
    cascade->AddNode(expr);
  }
  // The result is an expression with the (side effects of the) cascade
  // sequence followed by the (value of the) receiver temp variable load.
  cascade->AddNode(new LoadLocalNode(cascade_pos, cascade_receiver_var));
  return cascade;
}


AstNode* Parser::ParseExpr(bool require_compiletime_const,
                           bool consume_cascades) {
  TRACE_PARSER("ParseExpr");
  String* expr_ident =
      Token::IsIdentifier(CurrentToken()) ? CurrentLiteral() : NULL;
  const intptr_t expr_pos = TokenPos();

  if (CurrentToken() == Token::kTHROW) {
    ConsumeToken();
    ASSERT(CurrentToken() != Token::kSEMICOLON);
    AstNode* expr = ParseExpr(require_compiletime_const, consume_cascades);
    return new ThrowNode(expr_pos, expr, NULL);
  }
  AstNode* expr = ParseConditionalExpr();
  if (!Token::IsAssignmentOperator(CurrentToken())) {
    if ((CurrentToken() == Token::kCASCADE) && consume_cascades) {
      return ParseCascades(expr);
    }
    if (require_compiletime_const) {
      expr = FoldConstExpr(expr_pos, expr);
    }
    return expr;
  }
  // Assignment expressions.
  const Token::Kind assignment_op = CurrentToken();
  const intptr_t assignment_pos = TokenPos();
  ConsumeToken();
  const intptr_t right_expr_pos = TokenPos();
  if (require_compiletime_const && (assignment_op != Token::kASSIGN)) {
    ErrorMsg(right_expr_pos, "expression must be a compile-time constant");
  }
  AstNode* right_expr = ParseExpr(require_compiletime_const, consume_cascades);
  if (assignment_op != Token::kASSIGN) {
    // Compound assignment: store inputs with side effects into temp. locals.
    LetNode* let_expr = PrepareCompoundAssignmentNodes(&expr);
    AstNode* assigned_value =
        ExpandAssignableOp(assignment_pos, assignment_op, expr, right_expr);
    AstNode* assign_expr = CreateAssignmentNode(
        expr, assigned_value, expr_ident, expr_pos);
    ASSERT(assign_expr != NULL);
    let_expr->AddNode(assign_expr);
    return let_expr;
  } else {
    AstNode* assigned_value =
        ExpandAssignableOp(assignment_pos, assignment_op, expr, right_expr);
    AstNode* assign_expr = CreateAssignmentNode(
        expr, assigned_value, expr_ident, expr_pos);
    ASSERT(assign_expr != NULL);
    return assign_expr;
  }
}


LiteralNode* Parser::ParseConstExpr() {
  TRACE_PARSER("ParseConstExpr");
  AstNode* expr = ParseExpr(kRequireConst, kNoCascades);
  ASSERT(expr->IsLiteralNode());
  return expr->AsLiteralNode();
}


AstNode* Parser::ParseConditionalExpr() {
  TRACE_PARSER("ParseConditionalExpr");
  const intptr_t expr_pos = TokenPos();
  AstNode* expr = ParseBinaryExpr(Token::Precedence(Token::kOR));
  if (CurrentToken() == Token::kCONDITIONAL) {
    EnsureExpressionTemp();
    ConsumeToken();
    AstNode* expr1 = ParseExpr(kAllowConst, kNoCascades);
    ExpectToken(Token::kCOLON);
    AstNode* expr2 = ParseExpr(kAllowConst, kNoCascades);
    expr = new ConditionalExprNode(expr_pos, expr, expr1, expr2);
  }
  return expr;
}


AstNode* Parser::ParseUnaryExpr() {
  TRACE_PARSER("ParseUnaryExpr");
  AstNode* expr = NULL;
  const intptr_t op_pos = TokenPos();
  if (IsPrefixOperator(CurrentToken())) {
    Token::Kind unary_op = CurrentToken();
    if (unary_op == Token::kSUB) {
      unary_op = Token::kNEGATE;
    }
    ConsumeToken();
    expr = ParseUnaryExpr();
    if (expr->IsPrimaryNode() && (expr->AsPrimaryNode()->IsSuper())) {
      expr = BuildUnarySuperOperator(unary_op, expr->AsPrimaryNode());
    } else {
      expr = UnaryOpNode::UnaryOpOrLiteral(op_pos, unary_op, expr);
    }
  } else if (IsIncrementOperator(CurrentToken())) {
    Token::Kind incr_op = CurrentToken();
    ConsumeToken();
    String* expr_ident =
        Token::IsIdentifier(CurrentToken()) ? CurrentLiteral() : NULL;
    const intptr_t expr_pos = TokenPos();
    expr = ParseUnaryExpr();
    // Is prefix.
    LetNode* let_expr = PrepareCompoundAssignmentNodes(&expr);
    Token::Kind binary_op =
        (incr_op == Token::kINCR) ? Token::kADD : Token::kSUB;
    BinaryOpNode* add = new BinaryOpNode(
        op_pos,
        binary_op,
        expr,
        new LiteralNode(op_pos, Smi::ZoneHandle(Smi::New(1))));
    AstNode* store = CreateAssignmentNode(expr, add, expr_ident, expr_pos);
    ASSERT(store != NULL);
    let_expr->AddNode(store);
    expr = let_expr;
  } else {
    expr = ParsePostfixExpr();
  }
  return expr;
}


ArgumentListNode* Parser::ParseActualParameters(
                              ArgumentListNode* implicit_arguments,
                              bool require_const) {
  TRACE_PARSER("ParseActualParameters");
  ASSERT(CurrentToken() == Token::kLPAREN);
  const bool saved_mode = SetAllowFunctionLiterals(true);
  ArgumentListNode* arguments;
  if (implicit_arguments == NULL) {
    arguments = new ArgumentListNode(TokenPos());
  } else {
    arguments = implicit_arguments;
  }
  const GrowableObjectArray& names =
      GrowableObjectArray::Handle(GrowableObjectArray::New(Heap::kOld));
  bool named_argument_seen = false;
  if (LookaheadToken(1) != Token::kRPAREN) {
    String& arg_name = String::Handle();
    do {
      ASSERT((CurrentToken() == Token::kLPAREN) ||
             (CurrentToken() == Token::kCOMMA));
      ConsumeToken();
      if (IsIdentifier() && (LookaheadToken(1) == Token::kCOLON)) {
        named_argument_seen = true;
        // The canonicalization of the arguments descriptor array built in
        // the code generator requires that the names are symbols, i.e.
        // canonicalized strings.
        ASSERT(CurrentLiteral()->IsSymbol());
        for (int i = 0; i < names.Length(); i++) {
          arg_name ^= names.At(i);
          if (CurrentLiteral()->Equals(arg_name)) {
            ErrorMsg("duplicate named argument");
          }
        }
        names.Add(*CurrentLiteral());
        ConsumeToken();  // ident.
        ConsumeToken();  // colon.
      } else if (named_argument_seen) {
        ErrorMsg("named argument expected");
      }
      arguments->Add(ParseExpr(require_const, kConsumeCascades));
    } while (CurrentToken() == Token::kCOMMA);
  } else {
    ConsumeToken();
  }
  ExpectToken(Token::kRPAREN);
  SetAllowFunctionLiterals(saved_mode);
  if (named_argument_seen) {
    arguments->set_names(Array::Handle(Array::MakeArray(names)));
  }
  return arguments;
}


AstNode* Parser::ParseStaticCall(const Class& cls,
                                 const String& func_name,
                                 intptr_t ident_pos) {
  TRACE_PARSER("ParseStaticCall");
  const intptr_t call_pos = TokenPos();
  ASSERT(CurrentToken() == Token::kLPAREN);
  ArgumentListNode* arguments = ParseActualParameters(NULL, kAllowConst);
  const int num_arguments = arguments->length();
  const Function& func = Function::ZoneHandle(
      Resolver::ResolveStatic(cls,
                              func_name,
                              num_arguments,
                              arguments->names(),
                              Resolver::kIsQualified));
  if (func.IsNull()) {
    // Check if there is a static field of the same name, it could be a closure
    // and so we try and invoke the closure.
    AstNode* closure = NULL;
    const Field& field = Field::ZoneHandle(cls.LookupStaticField(func_name));
    Function& func = Function::ZoneHandle();
    if (field.IsNull()) {
      // No field, check if we have an explicit getter function.
      const String& getter_name =
          String::ZoneHandle(Field::GetterName(func_name));
      const int kNumArguments = 0;  // no arguments.
      func = Resolver::ResolveStatic(cls,
                                     getter_name,
                                     kNumArguments,
                                     Object::empty_array(),
                                     Resolver::kIsQualified);
      if (!func.IsNull()) {
        ASSERT(func.kind() != RawFunction::kImplicitStaticFinalGetter);
        EnsureSavedCurrentContext();
        closure = new StaticGetterNode(call_pos,
                                       NULL,
                                       false,
                                       Class::ZoneHandle(cls.raw()),
                                       func_name);
        return new ClosureCallNode(call_pos, closure, arguments);
      }
    } else {
      EnsureSavedCurrentContext();
      closure = GenerateStaticFieldLookup(field, call_pos);
      return new ClosureCallNode(call_pos, closure, arguments);
    }
    // Could not resolve static method: throw a NoSuchMethodError.
    return ThrowNoSuchMethodError(ident_pos,
                                  cls,
                                  func_name,
                                  arguments,
                                  InvocationMirror::kStatic,
                                  InvocationMirror::kMethod);
  } else if (cls.IsTopLevel() &&
      (cls.library() == Library::CoreLibrary()) &&
      (func.name() == Symbols::Identical().raw())) {
    // This is the predefined toplevel function identical(a,b). Create
    // a comparison node instead.
    ASSERT(num_arguments == 2);
    return new ComparisonNode(ident_pos,
                              Token::kEQ_STRICT,
                              arguments->NodeAt(0),
                              arguments->NodeAt(1));
  }
  return new StaticCallNode(call_pos, func, arguments);
}


AstNode* Parser::ParseInstanceCall(AstNode* receiver, const String& func_name) {
  TRACE_PARSER("ParseInstanceCall");
  const intptr_t call_pos = TokenPos();
  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg(call_pos, "left parenthesis expected");
  }
  ArgumentListNode* arguments = ParseActualParameters(NULL, kAllowConst);
  return new InstanceCallNode(call_pos, receiver, func_name, arguments);
}


AstNode* Parser::ParseClosureCall(AstNode* closure) {
  TRACE_PARSER("ParseClosureCall");
  const intptr_t call_pos = TokenPos();
  ASSERT(CurrentToken() == Token::kLPAREN);
  EnsureSavedCurrentContext();
  ArgumentListNode* arguments = ParseActualParameters(NULL, kAllowConst);
  return new ClosureCallNode(call_pos, closure, arguments);
}


AstNode* Parser::GenerateStaticFieldLookup(const Field& field,
                                           intptr_t ident_pos) {
  // If the static field has an initializer, initialize the field at compile
  // time, which is only possible if the field is const.
  AstNode* initializing_getter = RunStaticFieldInitializer(field);
  if (initializing_getter != NULL) {
    // The field is not yet initialized and could not be initialized at compile
    // time. The getter will initialize the field.
    return initializing_getter;
  }
  // The field is initialized.
  if (field.is_const()) {
    ASSERT(field.value() != Object::sentinel().raw());
    ASSERT(field.value() != Object::transition_sentinel().raw());
    return new LiteralNode(ident_pos, Instance::ZoneHandle(field.value()));
  }
  ASSERT(field.is_static());
  const Class& field_owner = Class::ZoneHandle(field.owner());
  const String& field_name = String::ZoneHandle(field.name());
  const String& getter_name = String::Handle(Field::GetterName(field_name));
  const Function& getter =
      Function::Handle(field_owner.LookupStaticFunction(getter_name));
  // Never load field directly if there is a getter (deterministic AST).
  if (getter.IsNull()) {
    return new LoadStaticFieldNode(ident_pos, Field::ZoneHandle(field.raw()));
  } else {
    ASSERT(getter.kind() == RawFunction::kImplicitStaticFinalGetter);
    return new StaticGetterNode(ident_pos,
                                NULL,  // Receiver.
                                false,  // is_super_getter.
                                field_owner,
                                field_name);
  }
}


AstNode* Parser::ParseStaticFieldAccess(const Class& cls,
                                        const String& field_name,
                                        intptr_t ident_pos,
                                        bool consume_cascades) {
  TRACE_PARSER("ParseStaticFieldAccess");
  AstNode* access = NULL;
  const intptr_t call_pos = TokenPos();
  const Field& field = Field::ZoneHandle(cls.LookupStaticField(field_name));
  Function& func = Function::ZoneHandle();
  if (Token::IsAssignmentOperator(CurrentToken())) {
    // Make sure an assignment is legal.
    if (field.IsNull()) {
      // No field, check if we have an explicit setter function.
      const String& setter_name =
          String::ZoneHandle(Field::SetterName(field_name));
      const int kNumArguments = 1;  // value.
      func = Resolver::ResolveStatic(cls,
                                     setter_name,
                                     kNumArguments,
                                     Object::empty_array(),
                                     Resolver::kIsQualified);
      if (func.IsNull()) {
        // No field or explicit setter function, throw a NoSuchMethodError.
        return ThrowNoSuchMethodError(ident_pos,
                                      cls,
                                      field_name,
                                      NULL,  // No arguments.
                                      InvocationMirror::kStatic,
                                      InvocationMirror::kField);
      }

      // Explicit setter function for the field found, field does not exist.
      // Create a getter node first in case it is needed. If getter node
      // is used as part of, e.g., "+=", and the explicit getter does not
      // exist, and error will be reported by the code generator.
      access = new StaticGetterNode(call_pos,
                                    NULL,
                                    false,
                                    Class::ZoneHandle(cls.raw()),
                                    String::ZoneHandle(field_name.raw()));
    } else {
      // Field exists.
      if (field.is_final()) {
        // Field has been marked as final, report an error as the field
        // is not settable.
        ErrorMsg(ident_pos,
                 "field '%s' is const static, cannot assign to it",
                 field_name.ToCString());
      }
      access = GenerateStaticFieldLookup(field, TokenPos());
    }
  } else {  // Not Token::IsAssignmentOperator(CurrentToken()).
    if (field.IsNull()) {
      // No field, check if we have an explicit getter function.
      const String& getter_name =
          String::ZoneHandle(Field::GetterName(field_name));
      const int kNumArguments = 0;  // no arguments.
      func = Resolver::ResolveStatic(cls,
                                     getter_name,
                                     kNumArguments,
                                     Object::empty_array(),
                                     Resolver::kIsQualified);
      if (func.IsNull()) {
        // We might be referring to an implicit closure, check to see if
        // there is a function of the same name.
        func = cls.LookupStaticFunction(field_name);
        if (func.IsNull()) {
          // No field or explicit getter function, throw a NoSuchMethodError.
          return ThrowNoSuchMethodError(ident_pos,
                                        cls,
                                        field_name,
                                        NULL,  // No arguments.
                                        InvocationMirror::kStatic,
                                        InvocationMirror::kGetter);
        }
        access = CreateImplicitClosureNode(func, call_pos, NULL);
      } else {
        ASSERT(func.kind() != RawFunction::kImplicitStaticFinalGetter);
        access = new StaticGetterNode(call_pos,
                                      NULL,
                                      false,
                                      Class::ZoneHandle(cls.raw()),
                                      field_name);
      }
    } else {
      access = GenerateStaticFieldLookup(field, TokenPos());
    }
  }
  return access;
}


AstNode* Parser::LoadFieldIfUnresolved(AstNode* node) {
  if (!node->IsPrimaryNode()) {
    return node;
  }
  PrimaryNode* primary = node->AsPrimaryNode();
  if (primary->primary().IsString()) {
    if (primary->IsSuper()) {
      return primary;
    }
    // In a static method, evaluation of an unresolved identifier causes a
    // NoSuchMethodError to be thrown.
    // In an instance method, we convert this into a getter call
    // for a field (which may be defined in a subclass.)
    String& name = String::CheckedZoneHandle(primary->primary().raw());
    if (current_function().is_static() ||
        current_function().IsInFactoryScope()) {
      return ThrowNoSuchMethodError(primary->token_pos(),
                                    current_class(),
                                    name,
                                    NULL,  // No arguments.
                                    InvocationMirror::kStatic,
                                    InvocationMirror::kField);
    } else {
      AstNode* receiver = LoadReceiver(primary->token_pos());
      return CallGetter(node->token_pos(), receiver, name);
    }
  }
  return primary;
}


AstNode* Parser::LoadClosure(PrimaryNode* primary) {
  ASSERT(primary->primary().IsFunction());
  AstNode* closure = NULL;
  const Function& func =
      Function::CheckedZoneHandle(primary->primary().raw());
  const String& funcname = String::ZoneHandle(func.name());
  if (func.is_static()) {
    // Static function access.
    closure = CreateImplicitClosureNode(func, primary->token_pos(), NULL);
  } else {
    // Instance function access.
    if (current_function().is_static() ||
        current_function().IsInFactoryScope()) {
      ErrorMsg(primary->token_pos(),
               "cannot access instance method '%s' from static method",
               funcname.ToCString());
    }
    AstNode* receiver = LoadReceiver(primary->token_pos());
    closure = CallGetter(primary->token_pos(), receiver, funcname);
  }
  return closure;
}


AstNode* Parser::ParseSelectors(AstNode* primary, bool is_cascade) {
  AstNode* left = primary;
  while (true) {
    AstNode* selector = NULL;
    if (CurrentToken() == Token::kPERIOD) {
      ConsumeToken();
      if (left->IsPrimaryNode()) {
        if (left->AsPrimaryNode()->primary().IsFunction()) {
          left = LoadClosure(left->AsPrimaryNode());
        } else if (left->AsPrimaryNode()->primary().IsTypeParameter()) {
          if (current_block_->scope->function_level() > 0) {
            // Make sure that the instantiator is captured.
            CaptureInstantiator();
          }
          TypeParameter& type_parameter = TypeParameter::ZoneHandle();
          type_parameter ^= ClassFinalizer::FinalizeType(
              current_class(),
              TypeParameter::Cast(left->AsPrimaryNode()->primary()),
              ClassFinalizer::kFinalize);
          ASSERT(!type_parameter.IsMalformed());
          left = new TypeNode(primary->token_pos(), type_parameter);
        } else {
          // Super field access handled in ParseSuperFieldAccess(),
          // super calls handled in ParseSuperCall().
          ASSERT(!left->AsPrimaryNode()->IsSuper());
          left = LoadFieldIfUnresolved(left);
        }
      }
      const intptr_t ident_pos = TokenPos();
      String* ident = ExpectIdentifier("identifier expected");
      if (CurrentToken() == Token::kLPAREN) {
        // Identifier followed by a opening paren: method call.
        if (left->IsPrimaryNode() &&
            left->AsPrimaryNode()->primary().IsClass()) {
          // Static method call prefixed with class name.
          const Class& cls = Class::Cast(left->AsPrimaryNode()->primary());
          selector = ParseStaticCall(cls, *ident, ident_pos);
        } else {
          selector = ParseInstanceCall(left, *ident);
        }
      } else {
        // Field access.
        Class& cls = Class::Handle();
        if (left->IsPrimaryNode()) {
          PrimaryNode* primary_node = left->AsPrimaryNode();
          if (primary_node->primary().IsClass()) {
            // If the primary node referred to a class we are loading a
            // qualified static field.
            cls ^= primary_node->primary().raw();
          }
        }
        if (cls.IsNull()) {
          // Instance field access.
          selector = CallGetter(ident_pos, left, *ident);
        } else {
          // Static field access.
          selector =
              ParseStaticFieldAccess(cls, *ident, ident_pos, !is_cascade);
        }
      }
    } else if (CurrentToken() == Token::kLBRACK) {
      // Super index operator handled in ParseSuperOperator().
      ASSERT(!left->IsPrimaryNode() || !left->AsPrimaryNode()->IsSuper());

      const intptr_t bracket_pos = TokenPos();
      ConsumeToken();
      left = LoadFieldIfUnresolved(left);
      const bool saved_mode = SetAllowFunctionLiterals(true);
      AstNode* index = ParseExpr(kAllowConst, kConsumeCascades);
      SetAllowFunctionLiterals(saved_mode);
      ExpectToken(Token::kRBRACK);
      AstNode* array = left;
      if (left->IsPrimaryNode()) {
        PrimaryNode* primary = left->AsPrimaryNode();
        if (primary->primary().IsFunction()) {
          array = LoadClosure(primary);
        } else if (primary->primary().IsClass()) {
          const Class& type_class = Class::Cast(primary->primary());
          Type& type = Type::ZoneHandle(
              Type::New(type_class, TypeArguments::Handle(),
                        primary->token_pos(), Heap::kOld));
          type ^= ClassFinalizer::FinalizeType(
              current_class(), type, ClassFinalizer::kCanonicalize);
          ASSERT(!type.IsMalformed());
          array = new TypeNode(primary->token_pos(), type);
        } else if (primary->primary().IsTypeParameter()) {
          if (current_block_->scope->function_level() > 0) {
            // Make sure that the instantiator is captured.
            CaptureInstantiator();
          }
          TypeParameter& type_parameter = TypeParameter::ZoneHandle();
          type_parameter ^= ClassFinalizer::FinalizeType(
              current_class(),
              TypeParameter::Cast(primary->primary()),
              ClassFinalizer::kFinalize);
          ASSERT(!type_parameter.IsMalformed());
          array = new TypeNode(primary->token_pos(), type_parameter);
        } else {
          UNREACHABLE();  // Internal parser error.
        }
      }
      selector =  new LoadIndexedNode(bracket_pos,
                                      array,
                                      index,
                                      Class::ZoneHandle());
    } else if (CurrentToken() == Token::kLPAREN) {
      if (left->IsPrimaryNode()) {
        PrimaryNode* primary = left->AsPrimaryNode();
        const intptr_t primary_pos = primary->token_pos();
        if (primary->primary().IsFunction()) {
          const Function& func = Function::Cast(primary->primary());
          const String& func_name = String::ZoneHandle(func.name());
          if (func.is_static()) {
            // Parse static function call.
            Class& cls = Class::Handle(func.Owner());
            selector = ParseStaticCall(cls, func_name, primary_pos);
          } else {
            // Dynamic function call on implicit "this" parameter.
            if (current_function().is_static()) {
              ErrorMsg(primary_pos,
                       "cannot access instance method '%s' "
                       "from static function",
                       func_name.ToCString());
            }
            selector = ParseInstanceCall(LoadReceiver(primary_pos), func_name);
          }
        } else if (primary->primary().IsString()) {
          // Primary is an unresolved name.
          if (primary->IsSuper()) {
            ErrorMsg(primary->token_pos(), "illegal use of super");
          }
          String& name = String::CheckedZoneHandle(primary->primary().raw());
          if (current_function().is_static()) {
            selector = ThrowNoSuchMethodError(primary->token_pos(),
                                              current_class(),
                                              name,
                                              NULL,  // No arguments.
                                              InvocationMirror::kStatic,
                                              InvocationMirror::kMethod);
          } else {
            // Treat as call to unresolved (instance) method.
            AstNode* receiver = LoadReceiver(primary->token_pos());
            selector = ParseInstanceCall(receiver, name);
          }
        } else if (primary->primary().IsTypeParameter()) {
          const String& name = String::ZoneHandle(
              Symbols::New(primary->Name()));
          selector = ThrowNoSuchMethodError(primary->token_pos(),
                                            current_class(),
                                            name,
                                            NULL,  // No arguments.
                                            InvocationMirror::kStatic,
                                            InvocationMirror::kMethod);
        } else if (primary->primary().IsClass()) {
          const Class& type_class = Class::Cast(primary->primary());
          Type& type = Type::ZoneHandle(
              Type::New(type_class, TypeArguments::Handle(),
                        primary->token_pos(), Heap::kOld));
          type ^= ClassFinalizer::FinalizeType(
              current_class(), type, ClassFinalizer::kCanonicalize);
          ASSERT(!type.IsMalformed());
          selector = new TypeNode(primary->token_pos(), type);
        } else {
          UNREACHABLE();  // Internal parser error.
        }
      } else {
        // Left is not a primary node; this must be a closure call.
        AstNode* closure = left;
        selector = ParseClosureCall(closure);
      }
    } else {
      // No (more) selectors to parse.
      left = LoadFieldIfUnresolved(left);
      if (left->IsPrimaryNode()) {
        PrimaryNode* primary = left->AsPrimaryNode();
        if (primary->primary().IsFunction()) {
          // Treat as implicit closure.
          left = LoadClosure(primary);
        } else if (primary->primary().IsClass()) {
          const Class& type_class = Class::Cast(primary->primary());
          Type& type = Type::ZoneHandle(
              Type::New(type_class, TypeArguments::Handle(),
                        primary->token_pos(), Heap::kOld));
          type ^= ClassFinalizer::FinalizeType(
              current_class(), type, ClassFinalizer::kCanonicalize);
          ASSERT(!type.IsMalformed());
          left = new TypeNode(primary->token_pos(), type);
        } else if (primary->primary().IsTypeParameter()) {
          if (current_block_->scope->function_level() > 0) {
            // Make sure that the instantiator is captured.
            CaptureInstantiator();
          }
          TypeParameter& type_parameter = TypeParameter::ZoneHandle();
          type_parameter ^= ClassFinalizer::FinalizeType(
              current_class(),
              TypeParameter::Cast(primary->primary()),
              ClassFinalizer::kFinalize);
          ASSERT(!type_parameter.IsMalformed());
          left = new TypeNode(primary->token_pos(), type_parameter);
        } else if (primary->IsSuper()) {
          // Return "super" to handle unary super operator calls,
          // or to report illegal use of "super" otherwise.
          left = primary;
        } else {
          UNREACHABLE();  // Internal parser error.
        }
      }
      // Done parsing selectors.
      return left;
    }
    ASSERT(selector != NULL);
    left = selector;
  }
}


AstNode* Parser::ParsePostfixExpr() {
  TRACE_PARSER("ParsePostfixExpr");
  String* expr_ident =
      Token::IsIdentifier(CurrentToken()) ? CurrentLiteral() : NULL;
  const intptr_t expr_pos = TokenPos();
  AstNode* expr = ParsePrimary();
  expr = ParseSelectors(expr, false);
  if (IsIncrementOperator(CurrentToken())) {
    TRACE_PARSER("IncrementOperator");
    Token::Kind incr_op = CurrentToken();
    ConsumeToken();
    // Not prefix.
    LetNode* let_expr = PrepareCompoundAssignmentNodes(&expr);
    LocalVariable* temp = let_expr->AddInitializer(expr);
    Token::Kind binary_op =
        (incr_op == Token::kINCR) ? Token::kADD : Token::kSUB;
    BinaryOpNode* add = new BinaryOpNode(
        expr_pos,
        binary_op,
        new LoadLocalNode(expr_pos, temp),
        new LiteralNode(expr_pos, Smi::ZoneHandle(Smi::New(1))));
    AstNode* store = CreateAssignmentNode(expr, add, expr_ident, expr_pos);
    ASSERT(store != NULL);
    // The result is a pair of the (side effects of the) store followed by
    // the (value of the) initial value temp variable load.
    let_expr->AddNode(store);
    let_expr->AddNode(new LoadLocalNode(expr_pos, temp));
    return let_expr;
  }
  return expr;
}


// Resolve the given type and its type arguments from the given scope class
// according to the given type finalization mode.
// If the given scope class is null, use the current library, but do not try to
// resolve type parameters.
// Not all involved type classes may get resolved yet, but at least the type
// parameters of the given class will get resolved, thereby relieving the class
// finalizer from resolving type parameters out of context.
void Parser::ResolveTypeFromClass(const Class& scope_class,
                                  ClassFinalizer::FinalizationKind finalization,
                                  AbstractType* type) {
  ASSERT(finalization >= ClassFinalizer::kResolveTypeParameters);
  ASSERT(type != NULL);
  if (type->IsResolved()) {
    return;
  }
  // Resolve class.
  if (!type->HasResolvedTypeClass()) {
    const UnresolvedClass& unresolved_class =
        UnresolvedClass::Handle(type->unresolved_class());
    const String& unresolved_class_name =
        String::Handle(unresolved_class.ident());
    Class& resolved_type_class = Class::Handle();
    if (unresolved_class.library_prefix() == LibraryPrefix::null()) {
      if (!scope_class.IsNull()) {
        // First check if the type is a type parameter of the given scope class.
        const TypeParameter& type_parameter = TypeParameter::Handle(
            scope_class.LookupTypeParameter(unresolved_class_name));
        if (!type_parameter.IsNull()) {
          // A type parameter is considered to be a malformed type when
          // referenced by a static member.
          if (ParsingStaticMember()) {
            ASSERT(scope_class.raw() == current_class().raw());
            if ((finalization == ClassFinalizer::kCanonicalizeWellFormed) ||
                FLAG_error_on_bad_type) {
              *type = ClassFinalizer::NewFinalizedMalformedType(
                  Error::Handle(),  // No previous error.
                  scope_class,
                  type->token_pos(),
                  "type parameter '%s' cannot be referenced "
                  "from static member",
                  String::Handle(type_parameter.name()).ToCString());
            } else {
              // Map the malformed type to dynamic and ignore type arguments.
              *type = Type::DynamicType();
            }
            return;
          }
          // A type parameter cannot be parameterized, so make the type
          // malformed if type arguments have previously been parsed.
          if (!AbstractTypeArguments::Handle(type->arguments()).IsNull()) {
            if ((finalization == ClassFinalizer::kCanonicalizeWellFormed) ||
                FLAG_error_on_bad_type) {
              *type = ClassFinalizer::NewFinalizedMalformedType(
                  Error::Handle(),  // No previous error.
                  scope_class,
                  type_parameter.token_pos(),
                  "type parameter '%s' cannot be parameterized",
                  String::Handle(type_parameter.name()).ToCString());
            } else {
              // Map the malformed type to dynamic and ignore type arguments.
              *type = Type::DynamicType();
            }
            return;
          }
          *type = type_parameter.raw();
          return;
        }
      }
      // The referenced class may not have been parsed yet. It would be wrong
      // to resolve it too early to an imported class of the same name.
      if (finalization > ClassFinalizer::kResolveTypeParameters) {
        // Resolve classname in the scope of the current library.
        Error& error = Error::Handle();
        resolved_type_class = ResolveClassInCurrentLibraryScope(
            unresolved_class.token_pos(),
            unresolved_class_name,
            &error);
        if (!error.IsNull()) {
          if ((finalization == ClassFinalizer::kCanonicalizeWellFormed) ||
              FLAG_error_on_bad_type) {
            *type = ClassFinalizer::NewFinalizedMalformedType(
                error,
                scope_class,
                unresolved_class.token_pos(),
                "cannot resolve class '%s'",
                unresolved_class_name.ToCString());
          } else {
            // Map the malformed type to dynamic and ignore type arguments.
            *type = Type::DynamicType();
          }
          return;
        }
      }
    } else {
      LibraryPrefix& lib_prefix =
          LibraryPrefix::Handle(unresolved_class.library_prefix());
      // Resolve class name in the scope of the library prefix.
      Error& error = Error::Handle();
      resolved_type_class = ResolveClassInPrefixScope(
          unresolved_class.token_pos(),
          lib_prefix,
          unresolved_class_name,
          &error);
      if (!error.IsNull()) {
        if ((finalization == ClassFinalizer::kCanonicalizeWellFormed) ||
            FLAG_error_on_bad_type) {
          *type = ClassFinalizer::NewFinalizedMalformedType(
              error,
              scope_class,
              unresolved_class.token_pos(),
              "cannot resolve class '%s'",
              unresolved_class_name.ToCString());
        } else {
          // Map the malformed type to dynamic and ignore type arguments.
          *type = Type::DynamicType();
        }
        return;
      }
    }
    // At this point, we can only have a parameterized_type.
    const Type& parameterized_type = Type::Cast(*type);
    if (!resolved_type_class.IsNull()) {
      // Replace unresolved class with resolved type class.
      parameterized_type.set_type_class(resolved_type_class);
    } else if (finalization >= ClassFinalizer::kCanonicalize) {
      if ((finalization == ClassFinalizer::kCanonicalizeWellFormed) ||
          FLAG_error_on_bad_type) {
        ClassFinalizer::FinalizeMalformedType(
            Error::Handle(),  // No previous error.
            scope_class,
            parameterized_type,
            "type '%s' is not loaded",
            String::Handle(parameterized_type.UserVisibleName()).ToCString());
      } else {
        // Map the malformed type to dynamic and ignore type arguments.
        *type = Type::DynamicType();
      }
      return;
    }
  }
  // Resolve type arguments, if any.
  const AbstractTypeArguments& arguments =
      AbstractTypeArguments::Handle(type->arguments());
  if (!arguments.IsNull()) {
    const intptr_t num_arguments = arguments.Length();
    for (intptr_t i = 0; i < num_arguments; i++) {
      AbstractType& type_argument = AbstractType::Handle(arguments.TypeAt(i));
      ResolveTypeFromClass(scope_class, finalization, &type_argument);
      arguments.SetTypeAt(i, type_argument);
    }
  }
}


LocalVariable* Parser::LookupLocalScope(const String& ident) {
  if (current_block_ == NULL) {
    return NULL;
  }
  // A found name is treated as accessed and possibly marked as captured.
  const bool kTestOnly = false;
  return current_block_->scope->LookupVariable(ident, kTestOnly);
}


void Parser::CheckInstanceFieldAccess(intptr_t field_pos,
                                      const String& field_name) {
  // Fields are not accessible from a static function, except from a
  // constructor, which is considered as non-static by the compiler.
  if (current_function().is_static()) {
    ErrorMsg(field_pos,
             "cannot access instance field '%s' from a static function",
             field_name.ToCString());
  }
}


bool Parser::ParsingStaticMember() const {
  if (is_top_level_) {
    return (current_member_ != NULL) &&
           current_member_->has_static && !current_member_->has_factory;
  }
  ASSERT(!current_function().IsNull());
  return
      current_function().is_static() && !current_function().IsInFactoryScope();
}


const AbstractType* Parser::ReceiverType(const Class& cls) {
  ASSERT(!cls.IsNull());
  TypeArguments& type_arguments = TypeArguments::Handle();
  if (cls.NumTypeParameters() > 0) {
    type_arguments = cls.type_parameters();
  }
  AbstractType& type = AbstractType::ZoneHandle(
      Type::New(cls, type_arguments, cls.token_pos()));
  if (cls.is_type_finalized()) {
    type ^= ClassFinalizer::FinalizeType(
        cls, type, ClassFinalizer::kCanonicalizeWellFormed);
    // Note that the receiver type may now be a malbounded type.
  }
  return &type;
}


bool Parser::IsInstantiatorRequired() const {
  ASSERT(!current_function().IsNull());
  if (current_function().is_static() &&
      !current_function().IsInFactoryScope()) {
    return false;
  }
  return current_class().NumTypeParameters() > 0;
}


RawInstance* Parser::TryCanonicalize(const Instance& instance,
                                     intptr_t token_pos) {
  if (instance.IsNull()) {
    return instance.raw();
  }
  const char* error_str = NULL;
  Instance& result =
      Instance::Handle(instance.CheckAndCanonicalize(&error_str));
  if (result.IsNull()) {
    ErrorMsg(token_pos, "Invalid const object %s", error_str);
  }
  return result.raw();
}


// If the field is already initialized, return no ast (NULL).
// Otherwise, if the field is constant, initialize the field and return no ast.
// If the field is not initialized and not const, return the ast for the getter.
AstNode* Parser::RunStaticFieldInitializer(const Field& field) {
  ASSERT(field.is_static());
  const Class& field_owner = Class::ZoneHandle(field.owner());
  const String& field_name = String::ZoneHandle(field.name());
  const String& getter_name = String::Handle(Field::GetterName(field_name));
  const Function& getter =
      Function::Handle(field_owner.LookupStaticFunction(getter_name));
  const Instance& value = Instance::Handle(field.value());
  if (value.raw() == Object::transition_sentinel().raw()) {
    if (field.is_const()) {
      ErrorMsg("circular dependency while initializing static field '%s'",
               field_name.ToCString());
    } else {
      // The implicit static getter will throw the exception if necessary.
      return new StaticGetterNode(TokenPos(),
                                  NULL,
                                  false,
                                  field_owner,
                                  field_name);
    }
  } else if (value.raw() == Object::sentinel().raw()) {
    // This field has not been referenced yet and thus the value has
    // not been evaluated. If the field is const, call the static getter method
    // to evaluate the expression and canonicalize the value.
    if (field.is_const()) {
      field.set_value(Object::transition_sentinel());
      const int kNumArguments = 0;  // no arguments.
      const Function& func =
          Function::Handle(Resolver::ResolveStatic(field_owner,
                                                   getter_name,
                                                   kNumArguments,
                                                   Object::empty_array(),
                                                   Resolver::kIsQualified));
      ASSERT(!func.IsNull());
      ASSERT(func.kind() == RawFunction::kImplicitStaticFinalGetter);
      Object& const_value = Object::Handle(
          DartEntry::InvokeFunction(func, Object::empty_array()));
      if (const_value.IsError()) {
        const Error& error = Error::Cast(const_value);
        if (error.IsUnhandledException()) {
          // An exception may not occur in every parse attempt, i.e., the
          // generated AST is not deterministic. Therefore mark the function as
          // not optimizable.
          current_function().set_is_optimizable(false);
          field.set_value(Object::null_instance());
          // It is a compile-time error if evaluation of a compile-time constant
          // would raise an exception.
          AppendErrorMsg(error, TokenPos(),
                         "error initializing const field '%s'",
                         String::Handle(field.name()).ToCString());
        } else {
          isolate()->long_jump_base()->Jump(1, error);
        }
      }
      ASSERT(const_value.IsNull() || const_value.IsInstance());
      Instance& instance = Instance::Handle();
      instance ^= const_value.raw();
      instance = TryCanonicalize(instance, TokenPos());
      field.set_value(instance);
      return NULL;   // Constant
    } else {
      return new StaticGetterNode(TokenPos(),
                                  NULL,
                                  false,
                                  field_owner,
                                  field_name);
    }
  }
  if (getter.IsNull() ||
      (getter.kind() == RawFunction::kImplicitStaticFinalGetter)) {
    return NULL;
  }
  ASSERT(getter.kind() == RawFunction::kImplicitGetter);
  return new StaticGetterNode(TokenPos(), NULL, false, field_owner, field_name);
}


RawObject* Parser::EvaluateConstConstructorCall(
    const Class& type_class,
    const AbstractTypeArguments& type_arguments,
    const Function& constructor,
    ArgumentListNode* arguments) {
  const int kNumExtraArgs = 2;  // implicit rcvr and construction phase args.
  const int num_arguments = arguments->length() + kNumExtraArgs;
  const Array& arg_values = Array::Handle(Array::New(num_arguments));
  Instance& instance = Instance::Handle();
  ASSERT(!constructor.IsFactory());
  instance = Instance::New(type_class, Heap::kOld);
  if (!type_arguments.IsNull()) {
    if (!type_arguments.IsInstantiated()) {
      ErrorMsg("type must be constant in const constructor");
    }
    instance.SetTypeArguments(
        AbstractTypeArguments::Handle(type_arguments.Canonicalize()));
  }
  arg_values.SetAt(0, instance);
  arg_values.SetAt(1, Smi::Handle(Smi::New(Function::kCtorPhaseAll)));
  for (int i = 0; i < arguments->length(); i++) {
    AstNode* arg = arguments->NodeAt(i);
    // Arguments have been evaluated to a literal value already.
    ASSERT(arg->IsLiteralNode());
    arg_values.SetAt((i + kNumExtraArgs), arg->AsLiteralNode()->literal());
  }
  const Array& args_descriptor =
      Array::Handle(ArgumentsDescriptor::New(num_arguments,
                                             arguments->names()));
  const Object& result =
      Object::Handle(DartEntry::InvokeFunction(constructor,
                                               arg_values,
                                               args_descriptor));
  if (result.IsError()) {
      // An exception may not occur in every parse attempt, i.e., the
      // generated AST is not deterministic. Therefore mark the function as
      // not optimizable.
      current_function().set_is_optimizable(false);
      if (result.IsUnhandledException()) {
        return result.raw();
      } else {
        isolate()->long_jump_base()->Jump(1, Error::Cast(result));
        UNREACHABLE();
        return Object::null();
      }
  } else {
    return TryCanonicalize(instance, TokenPos());
  }
}


// Do a lookup for the identifier in the block scope and the class scope
// return true if the identifier is found, false otherwise.
// If node is non NULL return an AST node corresponding to the identifier.
bool Parser::ResolveIdentInLocalScope(intptr_t ident_pos,
                                      const String &ident,
                                      AstNode** node) {
  TRACE_PARSER("ResolveIdentInLocalScope");
  // First try to find the identifier in the nested local scopes.
  LocalVariable* local = LookupLocalScope(ident);
  if (local != NULL) {
    if (node != NULL) {
      if (local->IsConst()) {
        *node = new LiteralNode(ident_pos, *local->ConstValue());
      } else {
        *node = new LoadLocalNode(ident_pos, local);
      }
    }
    return true;
  }

  // Try to find the identifier in the class scope of the current class.
  // If the current class is the result of a mixin application, we must
  // use the class scope of the class from which the function originates.
  Class& cls = Class::Handle(isolate());
  if (current_class().mixin() == Type::null()) {
    cls = current_class().raw();
  } else {
    cls = parsed_function()->function().origin();
  }
  Function& func = Function::Handle(isolate(), Function::null());
  Field& field = Field::Handle(isolate(), Field::null());

  // First check if a field exists.
  field = cls.LookupField(ident);
  if (!field.IsNull()) {
    if (node != NULL) {
      if (!field.is_static()) {
        CheckInstanceFieldAccess(ident_pos, ident);
        *node = CallGetter(ident_pos, LoadReceiver(ident_pos), ident);
      } else {
        *node = GenerateStaticFieldLookup(field, ident_pos);
      }
    }
    return true;
  }

  // Check if an instance/static function exists.
  func = cls.LookupFunction(ident);
  if (!func.IsNull() &&
      (func.IsDynamicFunction() || func.IsStaticFunction())) {
    if (node != NULL) {
      *node = new PrimaryNode(ident_pos,
                              Function::ZoneHandle(isolate(), func.raw()));
    }
    return true;
  }

  // Now check if a getter/setter method exists for it in which case
  // it is still a field.
  func = cls.LookupGetterFunction(ident);
  if (!func.IsNull()) {
    if (func.IsDynamicFunction()) {
      if (node != NULL) {
        CheckInstanceFieldAccess(ident_pos, ident);
        ASSERT(AbstractType::Handle(func.result_type()).IsResolved());
        *node = CallGetter(ident_pos, LoadReceiver(ident_pos), ident);
      }
      return true;
    } else if (func.IsStaticFunction()) {
      if (node != NULL) {
        ASSERT(AbstractType::Handle(func.result_type()).IsResolved());
        // The static getter may later be changed into a dynamically
        // resolved instance setter if no static setter can
        // be found.
        AstNode* receiver = NULL;
        const bool kTestOnly = true;
        if (!current_function().is_static() &&
            (LookupReceiver(current_block_->scope, kTestOnly) != NULL)) {
          receiver = LoadReceiver(ident_pos);
        }
        *node = new StaticGetterNode(ident_pos,
                                     receiver,
                                     false,
                                     Class::ZoneHandle(isolate(), cls.raw()),
                                     ident);
      }
      return true;
    }
  }
  func = cls.LookupSetterFunction(ident);
  if (!func.IsNull()) {
    if (func.IsDynamicFunction()) {
      if (node != NULL) {
        // We create a getter node even though a getter doesn't exist as
        // it could be followed by an assignment which will convert it to
        // a setter node. If there is no assignment we will get an error
        // when we try to invoke the getter.
        CheckInstanceFieldAccess(ident_pos, ident);
        ASSERT(AbstractType::Handle(func.result_type()).IsResolved());
        *node = CallGetter(ident_pos, LoadReceiver(ident_pos), ident);
      }
      return true;
    } else if (func.IsStaticFunction()) {
      if (node != NULL) {
        // We create a getter node even though a getter doesn't exist as
        // it could be followed by an assignment which will convert it to
        // a setter node. If there is no assignment we will get an error
        // when we try to invoke the getter.
        *node = new StaticGetterNode(ident_pos,
                                     NULL,
                                     false,
                                     Class::ZoneHandle(isolate(), cls.raw()),
                                     ident);
      }
      return true;
    }
  }

  // Nothing found in scope of current class.
  if (node != NULL) {
    *node = NULL;
  }
  return false;  // Not an unqualified identifier.
}


static RawObject* LookupNameInLibrary(Isolate* isolate,
                                      const Library& lib,
                                      const String& name) {
  Object& obj = Object::Handle(isolate);
  obj = lib.LookupLocalObject(name);
  if (!obj.IsNull()) {
    return obj.raw();
  }
  String& accessor_name = String::Handle(isolate, Field::GetterName(name));
  obj = lib.LookupLocalObject(accessor_name);
  if (!obj.IsNull()) {
    return obj.raw();
  }
  accessor_name = Field::SetterName(name);
  obj = lib.LookupLocalObject(accessor_name);
  return obj.raw();
}


static RawObject* LookupNameInImport(Isolate* isolate,
                                     const Namespace& ns,
                                     const String& name) {
  // If the given name is filtered out by the import, don't look it up, nor its
  // getter and setter names.
  if (ns.HidesName(name)) {
    return Object::null();
  }
  Object& obj = Object::Handle(isolate);
  obj = ns.Lookup(name);
  if (!obj.IsNull()) {
    return obj.raw();
  }
  String& accessor_name = String::Handle(isolate, Field::GetterName(name));
  obj = ns.Lookup(accessor_name);
  if (!obj.IsNull()) {
    return obj.raw();
  }
  accessor_name = Field::SetterName(name);
  obj = ns.Lookup(accessor_name);
  return obj.raw();
}


// Resolve a name by checking the global scope of the current
// library. If not found in the current library, then look in the scopes
// of all libraries that are imported without a library prefix.
// Issue an error if the name is not found in the global scope
// of the current library, but is defined in more than one imported
// library, i.e. if the name cannot be resolved unambiguously.
RawObject* Parser::ResolveNameInCurrentLibraryScope(intptr_t ident_pos,
                                                    const String& name,
                                                    Error* error) {
  TRACE_PARSER("ResolveNameInCurrentLibraryScope");
  HANDLESCOPE(isolate());
  Object& obj = Object::Handle(isolate(),
                               LookupNameInLibrary(isolate(), library_, name));
  if (obj.IsNull()) {
    // Name is not found in current library. Check scope of all
    // imported libraries.
    String& first_lib_url = String::Handle(isolate());
    Namespace& import = Namespace::Handle(isolate());
    intptr_t num_imports = library_.num_imports();
    Object& imported_obj = Object::Handle(isolate());
    Library& lib = Library::Handle(isolate());
    for (int i = 0; i < num_imports; i++) {
      import = library_.ImportAt(i);
      imported_obj = LookupNameInImport(isolate(), import, name);
      if (!imported_obj.IsNull()) {
        lib ^= import.library();
        if (!first_lib_url.IsNull()) {
          // Found duplicate definition.
          Error& ambiguous_ref_error = Error::Handle();
          if (first_lib_url.raw() == lib.url()) {
            ambiguous_ref_error = FormatErrorMsg(
                script_, ident_pos, "Error",
                "ambiguous reference to '%s', "
                "as library '%s' is imported multiple times",
                name.ToCString(),
                first_lib_url.ToCString());
          } else {
            ambiguous_ref_error = FormatErrorMsg(
                script_, ident_pos, "Error",
                "ambiguous reference: "
                "'%s' is defined in library '%s' and also in '%s'",
                name.ToCString(),
                first_lib_url.ToCString(),
                String::Handle(lib.url()).ToCString());
          }
          if (error == NULL) {
            // Report a compile time error since the caller is not interested
            // in the error.
            ErrorMsg(ambiguous_ref_error);
          }
          *error = ambiguous_ref_error.raw();
          return Object::null();
        } else {
          first_lib_url = lib.url();
          obj = imported_obj.raw();
        }
      }
    }
  }
  return obj.raw();
}


RawClass* Parser::ResolveClassInCurrentLibraryScope(intptr_t ident_pos,
                                                    const String& name,
                                                    Error* error) {
  const Object& obj =
      Object::Handle(ResolveNameInCurrentLibraryScope(ident_pos, name, error));
  if (obj.IsClass()) {
    return Class::Cast(obj).raw();
  }
  return Class::null();
}


// Resolve an identifier by checking the global scope of the current
// library. If not found in the current library, then look in the scopes
// of all libraries that are imported without a library prefix.
// Issue an error if the identifier is not found in the global scope
// of the current library, but is defined in more than one imported
// library, i.e. if the identifier cannot be resolved unambiguously.
AstNode* Parser::ResolveIdentInCurrentLibraryScope(intptr_t ident_pos,
                                                   const String& ident) {
  TRACE_PARSER("ResolveIdentInCurrentLibraryScope");
  const Object& obj =
    Object::Handle(ResolveNameInCurrentLibraryScope(ident_pos, ident, NULL));
  if (obj.IsClass()) {
    const Class& cls = Class::Cast(obj);
    return new PrimaryNode(ident_pos, Class::ZoneHandle(cls.raw()));
  } else if (obj.IsField()) {
    const Field& field = Field::Cast(obj);
    ASSERT(field.is_static());
    return GenerateStaticFieldLookup(field, ident_pos);
  } else if (obj.IsFunction()) {
    const Function& func = Function::Cast(obj);
    ASSERT(func.is_static());
    if (func.IsGetterFunction() || func.IsSetterFunction()) {
      return new StaticGetterNode(ident_pos,
                                  /* receiver */ NULL,
                                  /* is_super_getter */ false,
                                  Class::ZoneHandle(func.Owner()),
                                  ident);

    } else {
      return new PrimaryNode(ident_pos, Function::ZoneHandle(func.raw()));
    }
  } else {
    ASSERT(obj.IsNull() || obj.IsLibraryPrefix());
  }
  // Lexically unresolved primary identifiers are referenced by their name.
  return new PrimaryNode(ident_pos, ident);
}


RawObject* Parser::ResolveNameInPrefixScope(intptr_t ident_pos,
                                            const LibraryPrefix& prefix,
                                            const String& name,
                                            Error* error) {
  TRACE_PARSER("ResolveNameInPrefixScope");
  HANDLESCOPE(isolate());
  Namespace& import = Namespace::Handle(isolate());
  String& first_lib_url = String::Handle(isolate());
  Object& obj = Object::Handle(isolate());
  Object& resolved_obj = Object::Handle(isolate());
  const Array& imports = Array::Handle(isolate(), prefix.imports());
  Library& lib = Library::Handle(isolate());
  for (intptr_t i = 0; i < prefix.num_imports(); i++) {
    import ^= imports.At(i);
    resolved_obj = LookupNameInImport(isolate(), import, name);
    if (!resolved_obj.IsNull()) {
      obj = resolved_obj.raw();
      lib = import.library();
      if (first_lib_url.IsNull()) {
        first_lib_url = lib.url();
      } else {
        // Found duplicate definition.
        Error& ambiguous_ref_error = Error::Handle();
        if (first_lib_url.raw() == lib.url()) {
          ambiguous_ref_error = FormatErrorMsg(
              script_, ident_pos, "Error",
              "ambiguous reference: '%s.%s' is imported multiple times",
              String::Handle(prefix.name()).ToCString(),
              name.ToCString());
        } else {
          ambiguous_ref_error = FormatErrorMsg(
              script_, ident_pos, "Error",
              "ambiguous reference: '%s.%s' is defined in '%s' and '%s'",
              String::Handle(prefix.name()).ToCString(),
              name.ToCString(),
              first_lib_url.ToCString(),
              String::Handle(lib.url()).ToCString());
        }
        if (error == NULL) {
          // Report a compile time error since the caller is not interested
          // in the error.
          ErrorMsg(ambiguous_ref_error);
        }
        *error = ambiguous_ref_error.raw();
        return Object::null();
      }
    }
  }
  return obj.raw();
}


RawClass* Parser::ResolveClassInPrefixScope(intptr_t ident_pos,
                                            const LibraryPrefix& prefix,
                                            const String& name,
                                            Error* error) {
  const Object& obj =
      Object::Handle(ResolveNameInPrefixScope(ident_pos, prefix, name, error));
  if (obj.IsClass()) {
    return Class::Cast(obj).raw();
  }
  return Class::null();
}


// Do a lookup for the identifier in the scope of the specified
// library prefix. This means trying to resolve it locally in all of the
// libraries present in the library prefix. If there are multiple libraries
// with the name, issue an ambiguous reference error.
AstNode* Parser::ResolveIdentInPrefixScope(intptr_t ident_pos,
                                           const LibraryPrefix& prefix,
                                           const String& ident) {
  TRACE_PARSER("ResolveIdentInPrefixScope");
  Object& obj =
      Object::Handle(ResolveNameInPrefixScope(ident_pos, prefix, ident, NULL));
  if (obj.IsNull()) {
    // Unresolved prefixed primary identifier.
    ErrorMsg(ident_pos, "identifier '%s.%s' cannot be resolved",
             String::Handle(prefix.name()).ToCString(),
             ident.ToCString());
  }
  if (obj.IsClass()) {
    const Class& cls = Class::Cast(obj);
    return new PrimaryNode(ident_pos, Class::ZoneHandle(cls.raw()));
  } else if (obj.IsField()) {
    const Field& field = Field::Cast(obj);
    ASSERT(field.is_static());
    return GenerateStaticFieldLookup(field, ident_pos);
  } else if (obj.IsFunction()) {
    const Function& func = Function::Cast(obj);
    ASSERT(func.is_static());
    if (func.IsGetterFunction() || func.IsSetterFunction()) {
      return new StaticGetterNode(ident_pos,
                                  /* receiver */ NULL,
                                  /* is_super_getter */ false,
                                  Class::ZoneHandle(func.Owner()),
                                  ident);

    } else {
      return new PrimaryNode(ident_pos, Function::ZoneHandle(func.raw()));
    }
  } else {
    // TODO(hausner): Should this be an error? It is not meaningful to
    // reference a library prefix defined in an imported library.
    ASSERT(obj.IsLibraryPrefix());
  }
  // Lexically unresolved primary identifiers are referenced by their name.
  return new PrimaryNode(ident_pos, ident);
}


// Resolve identifier. Issue an error message if
// the ident refers to a method and allow_closure_names is false.
// If the name cannot be resolved, turn it into an instance field access
// if we're compiling an instance method, or issue an error message
// if we're compiling a static method.
AstNode* Parser::ResolveIdent(intptr_t ident_pos,
                              const String& ident,
                              bool allow_closure_names) {
  TRACE_PARSER("ResolveIdent");
  // First try to find the variable in the local scope (block scope or
  // class scope).
  AstNode* resolved = NULL;
  ResolveIdentInLocalScope(ident_pos, ident, &resolved);
  if (resolved == NULL) {
    // Check whether the identifier is a type parameter.
    if (!current_class().IsNull()) {
      TypeParameter& type_parameter = TypeParameter::ZoneHandle(
          current_class().LookupTypeParameter(ident));
      if (!type_parameter.IsNull()) {
        if (current_block_->scope->function_level() > 0) {
          // Make sure that the instantiator is captured.
          CaptureInstantiator();
        }
        type_parameter ^= ClassFinalizer::FinalizeType(
            current_class(), type_parameter, ClassFinalizer::kFinalize);
        ASSERT(!type_parameter.IsMalformed());
        return new TypeNode(ident_pos, type_parameter);
      }
    }
    // Not found in the local scope, and the name is not a type parameter.
    // Try finding the variable in the library scope (current library
    // and all libraries imported by it without a library prefix).
    resolved = ResolveIdentInCurrentLibraryScope(ident_pos, ident);
  }
  if (resolved->IsPrimaryNode()) {
    PrimaryNode* primary = resolved->AsPrimaryNode();
    if (primary->primary().IsString()) {
      // We got an unresolved name. If we are compiling a static
      // method, evaluation of an unresolved identifier causes a
      // NoSuchMethodError to be thrown. In an instance method, we convert
      // the unresolved name to an instance field access, since a
      // subclass might define a field with this name.
      if (current_function().is_static()) {
        resolved = ThrowNoSuchMethodError(ident_pos,
                                          current_class(),
                                          ident,
                                          NULL,  // No arguments.
                                          InvocationMirror::kStatic,
                                          InvocationMirror::kField);
      } else {
        // Treat as call to unresolved instance field.
        resolved = CallGetter(ident_pos, LoadReceiver(ident_pos), ident);
      }
    } else if (primary->primary().IsFunction()) {
      if (allow_closure_names) {
        resolved = LoadClosure(primary);
      } else {
        ErrorMsg(ident_pos, "illegal reference to method '%s'",
                 ident.ToCString());
      }
    } else if (primary->primary().IsClass()) {
      const Class& type_class = Class::Cast(primary->primary());
      Type& type = Type::ZoneHandle(
          Type::New(type_class, TypeArguments::Handle(),
                    primary->token_pos(), Heap::kOld));
      type ^= ClassFinalizer::FinalizeType(
          current_class(), type, ClassFinalizer::kCanonicalize);
      ASSERT(!type.IsMalformed());
      resolved = new TypeNode(primary->token_pos(), type);
    }
  }
  return resolved;
}


// Parses type = [ident "."] ident ["<" type { "," type } ">"], then resolve and
// finalize it according to the given type finalization mode.
RawAbstractType* Parser::ParseType(
    ClassFinalizer::FinalizationKind finalization) {
  TRACE_PARSER("ParseType");
  if (CurrentToken() != Token::kIDENT) {
    ErrorMsg("type name expected");
  }
  QualIdent type_name;
  if (finalization == ClassFinalizer::kIgnore) {
    SkipQualIdent();
  } else {
    ParseQualIdent(&type_name);
    // An identifier cannot be resolved in a local scope when top level parsing.
    if (!is_top_level_ &&
        (type_name.lib_prefix == NULL) &&
        ResolveIdentInLocalScope(type_name.ident_pos, *type_name.ident, NULL)) {
      // The type is malformed. Skip over its type arguments.
      ParseTypeArguments(ClassFinalizer::kIgnore);
      if (finalization == ClassFinalizer::kCanonicalizeWellFormed) {
        return ClassFinalizer::NewFinalizedMalformedType(
            Error::Handle(),  // No previous error.
            current_class(),
            type_name.ident_pos,
            "using '%s' in this context is invalid",
            type_name.ident->ToCString());
      }
      return Type::DynamicType();
    }
  }
  Object& type_class = Object::Handle(isolate());
  // Leave type_class as null if type finalization mode is kIgnore.
  if (finalization != ClassFinalizer::kIgnore) {
    LibraryPrefix& lib_prefix = LibraryPrefix::Handle(isolate());
    if (type_name.lib_prefix != NULL) {
      lib_prefix = type_name.lib_prefix->raw();
    }
    type_class = UnresolvedClass::New(lib_prefix,
                                      *type_name.ident,
                                      type_name.ident_pos);
  }
  AbstractTypeArguments& type_arguments = AbstractTypeArguments::Handle(
      isolate(), ParseTypeArguments(finalization));
  if (finalization == ClassFinalizer::kIgnore) {
    return Type::DynamicType();
  }
  AbstractType& type = AbstractType::Handle(
      isolate(), Type::New(type_class, type_arguments, type_name.ident_pos));
  if (finalization >= ClassFinalizer::kResolveTypeParameters) {
    ResolveTypeFromClass(current_class(), finalization, &type);
    if (finalization >= ClassFinalizer::kCanonicalize) {
      type ^= ClassFinalizer::FinalizeType(current_class(), type, finalization);
    }
  }
  return type.raw();
}


void Parser::CheckConstructorCallTypeArguments(
    intptr_t pos, Function& constructor,
    const AbstractTypeArguments& type_arguments) {
  if (!type_arguments.IsNull()) {
    const Class& constructor_class = Class::Handle(constructor.Owner());
    ASSERT(!constructor_class.IsNull());
    ASSERT(constructor_class.is_finalized());
    // Do not report the expected vs. actual number of type arguments, because
    // the type argument vector is flattened and raw types are allowed.
    if (type_arguments.Length() != constructor_class.NumTypeArguments()) {
      ErrorMsg(pos, "wrong number of type arguments passed to constructor");
    }
  }
}


// Parse "[" [ expr { "," expr } ["," ] "]".
// Note: if the list literal is empty and the brackets have no whitespace
// between them, the scanner recognizes the opening and closing bracket
// as one token of type Token::kINDEX.
AstNode* Parser::ParseListLiteral(intptr_t type_pos,
                                  bool is_const,
                                  const AbstractTypeArguments& type_arguments) {
  TRACE_PARSER("ParseListLiteral");
  ASSERT(type_pos >= 0);
  ASSERT(CurrentToken() == Token::kLBRACK || CurrentToken() == Token::kINDEX);
  const intptr_t literal_pos = TokenPos();
  bool is_empty_literal = CurrentToken() == Token::kINDEX;
  ConsumeToken();

  AbstractType& element_type = Type::ZoneHandle(Type::DynamicType());
  AbstractTypeArguments& list_type_arguments =
      AbstractTypeArguments::ZoneHandle(type_arguments.raw());
  // If no type argument vector is provided, leave it as null, which is
  // equivalent to using dynamic as the type argument for the element type.
  if (!list_type_arguments.IsNull()) {
    ASSERT(list_type_arguments.Length() > 0);
    // List literals take a single type argument.
    if (list_type_arguments.Length() == 1) {
      element_type = list_type_arguments.TypeAt(0);
    } else {
      if (FLAG_error_on_bad_type) {
        ErrorMsg(type_pos,
                 "a list literal takes one type argument specifying "
                 "the element type");
      }
      // Ignore type arguments.
      list_type_arguments = AbstractTypeArguments::null();
    }
    if (is_const && !element_type.IsInstantiated()) {
      ErrorMsg(type_pos,
               "the type argument of a constant list literal cannot include "
               "a type variable");
    }
  }
  ASSERT((list_type_arguments.IsNull() && element_type.IsDynamicType()) ||
         ((list_type_arguments.Length() == 1) && !element_type.IsNull()));
  const Class& array_class = Class::Handle(
      isolate()->object_store()->array_class());
  Type& type = Type::ZoneHandle(
      Type::New(array_class, list_type_arguments, type_pos));
  type ^= ClassFinalizer::FinalizeType(
      current_class(), type, ClassFinalizer::kCanonicalize);
  GrowableArray<AstNode*> element_list;
  // Parse the list elements. Note: there may be an optional extra
  // comma after the last element.
  if (!is_empty_literal) {
    const bool saved_mode = SetAllowFunctionLiterals(true);
    while (CurrentToken() != Token::kRBRACK) {
      const intptr_t element_pos = TokenPos();
      AstNode* element = ParseExpr(is_const, kConsumeCascades);
      if (FLAG_enable_type_checks &&
          !is_const &&
          !element_type.IsDynamicType()) {
        element = new AssignableNode(element_pos,
                                     element,
                                     element_type,
                                     Symbols::ListLiteralElement());
      }
      element_list.Add(element);
      if (CurrentToken() == Token::kCOMMA) {
        ConsumeToken();
      } else if (CurrentToken() != Token::kRBRACK) {
        ErrorMsg("comma or ']' expected");
      }
    }
    ExpectToken(Token::kRBRACK);
    SetAllowFunctionLiterals(saved_mode);
  }

  if (is_const) {
    // Allocate and initialize the const list at compile time.
    Array& const_list =
        Array::ZoneHandle(Array::New(element_list.length(), Heap::kOld));
    const_list.SetTypeArguments(
        AbstractTypeArguments::Handle(list_type_arguments.Canonicalize()));
    Error& malformed_error = Error::Handle();
    for (int i = 0; i < element_list.length(); i++) {
      AstNode* elem = element_list[i];
      // Arguments have been evaluated to a literal value already.
      ASSERT(elem->IsLiteralNode());
      ASSERT(!is_top_level_);  // We cannot check unresolved types.
      if (FLAG_enable_type_checks &&
          !element_type.IsDynamicType() &&
          (!elem->AsLiteralNode()->literal().IsNull() &&
           !elem->AsLiteralNode()->literal().IsInstanceOf(
               element_type, TypeArguments::Handle(), &malformed_error))) {
        // If the failure is due to a malformed type error, display it instead.
        if (!malformed_error.IsNull()) {
          ErrorMsg(malformed_error);
        } else {
          ErrorMsg(elem->AsLiteralNode()->token_pos(),
                   "list literal element at index %d must be "
                   "a constant of type '%s'",
                   i,
                   String::Handle(element_type.UserVisibleName()).ToCString());
        }
      }
      const_list.SetAt(i, elem->AsLiteralNode()->literal());
    }
    const_list ^= TryCanonicalize(const_list, literal_pos);
    const_list.MakeImmutable();
    return new LiteralNode(literal_pos, const_list);
  } else {
    // Factory call at runtime.
    const Class& factory_class =
        Class::Handle(LookupCoreClass(Symbols::List()));
    ASSERT(!factory_class.IsNull());
    const Function& factory_method = Function::ZoneHandle(
        factory_class.LookupFactory(
            PrivateCoreLibName(Symbols::ListLiteralFactory())));
    ASSERT(!factory_method.IsNull());
    if (!list_type_arguments.IsNull() &&
        !list_type_arguments.IsInstantiated() &&
        (current_block_->scope->function_level() > 0)) {
      // Make sure that the instantiator is captured.
      CaptureInstantiator();
    }
    AbstractTypeArguments& factory_type_args =
        AbstractTypeArguments::ZoneHandle(list_type_arguments.raw());
    // If the factory class extends other parameterized classes, adjust the
    // type argument vector.
    if (!factory_type_args.IsNull() && (factory_class.NumTypeArguments() > 1)) {
      ASSERT(factory_type_args.Length() == 1);
      Type& factory_type = Type::Handle(Type::New(
          factory_class, factory_type_args, type_pos, Heap::kNew));
      factory_type ^= ClassFinalizer::FinalizeType(
          current_class(), factory_type, ClassFinalizer::kFinalize);
      factory_type_args = factory_type.arguments();
      ASSERT(factory_type_args.Length() == factory_class.NumTypeArguments());
    }
    factory_type_args = factory_type_args.Canonicalize();
    ArgumentListNode* factory_param = new ArgumentListNode(literal_pos);
    if (element_list.length() == 0) {
      // TODO(srdjan): Use Object::empty_array once issue 9871 has been fixed.
      Array& empty_array = Array::ZoneHandle(Object::empty_array().raw());
      LiteralNode* empty_array_literal =
          new LiteralNode(TokenPos(), empty_array);
      factory_param->Add(empty_array_literal);
    } else {
      ArrayNode* list = new ArrayNode(TokenPos(), type, element_list);
      factory_param->Add(list);
    }
    return CreateConstructorCallNode(literal_pos,
                                     factory_type_args,
                                     factory_method,
                                     factory_param);
  }
}


ConstructorCallNode* Parser::CreateConstructorCallNode(
    intptr_t token_pos,
    const AbstractTypeArguments& type_arguments,
    const Function& constructor,
    ArgumentListNode* arguments) {
  if (!type_arguments.IsNull() && !type_arguments.IsInstantiated()) {
    EnsureExpressionTemp();
  }
  return new ConstructorCallNode(token_pos,
                                 type_arguments,
                                 constructor,
                                 arguments);
}


static void AddKeyValuePair(GrowableArray<AstNode*>* pairs,
                            bool is_const,
                            AstNode* key,
                            AstNode* value) {
  if (is_const) {
    ASSERT(key->IsLiteralNode());
    const Instance& new_key = key->AsLiteralNode()->literal();
    for (int i = 0; i < pairs->length(); i += 2) {
      const Instance& key_i = (*pairs)[i]->AsLiteralNode()->literal();
      // The keys of a compile time constant map are compile time
      // constants, i.e. canonicalized values. Thus, we can compare
      // raw pointers to check for equality.
      if (new_key.raw() == key_i.raw()) {
        // Duplicate key found. The new value replaces the previously
        // defined value.
        (*pairs)[i + 1] = value;
        return;
      }
    }
  }
  pairs->Add(key);
  pairs->Add(value);
}


AstNode* Parser::ParseMapLiteral(intptr_t type_pos,
                                 bool is_const,
                                 const AbstractTypeArguments& type_arguments) {
  TRACE_PARSER("ParseMapLiteral");
  ASSERT(type_pos >= 0);
  ASSERT(CurrentToken() == Token::kLBRACE);
  const intptr_t literal_pos = TokenPos();
  ConsumeToken();

  AbstractType& key_type = Type::ZoneHandle(Type::DynamicType());
  AbstractType& value_type = Type::ZoneHandle(Type::DynamicType());
  AbstractTypeArguments& map_type_arguments =
      AbstractTypeArguments::ZoneHandle(type_arguments.raw());
  // If no type argument vector is provided, leave it as null, which is
  // equivalent to using dynamic as the type argument for the both key and value
  // types.
  if (!map_type_arguments.IsNull()) {
    ASSERT(map_type_arguments.Length() > 0);
    // Map literals take two type arguments.
    if (map_type_arguments.Length() == 2) {
      key_type = map_type_arguments.TypeAt(0);
      value_type = map_type_arguments.TypeAt(1);
      if (is_const && !type_arguments.IsInstantiated()) {
        ErrorMsg(type_pos,
                 "the type arguments of a constant map literal cannot include "
                 "a type variable");
      }
      if (key_type.IsMalformed()) {
        if (FLAG_error_on_bad_type) {
          ErrorMsg(Error::Handle(key_type.malformed_error()));
        }
        // Map malformed key type to dynamic.
        key_type = Type::DynamicType();
        map_type_arguments.SetTypeAt(0, key_type);
      }
      if (value_type.IsMalformed()) {
        if (FLAG_error_on_bad_type) {
          ErrorMsg(Error::Handle(value_type.malformed_error()));
        }
        // Map malformed value type to dynamic.
        value_type = Type::DynamicType();
        map_type_arguments.SetTypeAt(1, value_type);
      }
    } else {
      if (FLAG_error_on_bad_type) {
        ErrorMsg(type_pos,
                 "a map literal takes two type arguments specifying "
                 "the key type and the value type");
      }
      // Ignore type arguments.
      map_type_arguments = AbstractTypeArguments::null();
    }
  }
  ASSERT((map_type_arguments.IsNull() &&
          key_type.IsDynamicType() && value_type.IsDynamicType()) ||
         ((map_type_arguments.Length() == 2) &&
          !key_type.IsMalformed() && !value_type.IsMalformed()));
  map_type_arguments ^= map_type_arguments.Canonicalize();

  GrowableArray<AstNode*> kv_pairs_list;
  // Parse the map entries. Note: there may be an optional extra
  // comma after the last entry.
  while (CurrentToken() != Token::kRBRACE) {
    const bool saved_mode = SetAllowFunctionLiterals(true);
    const intptr_t key_pos = TokenPos();
    AstNode* key = ParseExpr(is_const, kConsumeCascades);
    if (FLAG_enable_type_checks &&
        !is_const &&
        !key_type.IsDynamicType()) {
      key = new AssignableNode(key_pos,
                               key,
                               key_type,
                               Symbols::ListLiteralElement());
    }
    ExpectToken(Token::kCOLON);
    const intptr_t value_pos = TokenPos();
    AstNode* value = ParseExpr(is_const, kConsumeCascades);
    SetAllowFunctionLiterals(saved_mode);
    if (FLAG_enable_type_checks &&
        !is_const &&
        !value_type.IsDynamicType()) {
      value = new AssignableNode(value_pos,
                                 value,
                                 value_type,
                                 Symbols::ListLiteralElement());
    }
    AddKeyValuePair(&kv_pairs_list, is_const, key, value);

    if (CurrentToken() == Token::kCOMMA) {
      ConsumeToken();
    } else if (CurrentToken() != Token::kRBRACE) {
      ErrorMsg("comma or '}' expected");
    }
  }
  ASSERT(kv_pairs_list.length() % 2 == 0);
  ExpectToken(Token::kRBRACE);

  if (is_const) {
    // Create the key-value pair array, canonicalize it and then create
    // the immutable map object with it. This all happens at compile time.
    // The resulting immutable map object is returned as a literal.

    // First, create the canonicalized key-value pair array.
    Array& key_value_array =
        Array::ZoneHandle(Array::New(kv_pairs_list.length(), Heap::kOld));
    AbstractType& arg_type = Type::Handle();
    Error& malformed_error = Error::Handle();
    for (int i = 0; i < kv_pairs_list.length(); i++) {
      AstNode* arg = kv_pairs_list[i];
      // Arguments have been evaluated to a literal value already.
      ASSERT(arg->IsLiteralNode());
      ASSERT(!is_top_level_);  // We cannot check unresolved types.
      if (FLAG_enable_type_checks) {
        if ((i % 2) == 0) {
          // Check key type.
          arg_type = key_type.raw();
        } else {
          // Check value type.
          arg_type = value_type.raw();
        }
        if (!arg_type.IsDynamicType() &&
            (!arg->AsLiteralNode()->literal().IsNull() &&
             !arg->AsLiteralNode()->literal().IsInstanceOf(
                 arg_type,
                 Object::null_abstract_type_arguments(),
                 &malformed_error))) {
          // If the failure is due to a malformed type error, display it.
          if (!malformed_error.IsNull()) {
            ErrorMsg(malformed_error);
          } else {
            ErrorMsg(arg->AsLiteralNode()->token_pos(),
                     "map literal %s at index %d must be "
                     "a constant of type '%s'",
                     ((i % 2) == 0) ? "key" : "value",
                     i >> 1,
                     String::Handle(arg_type.UserVisibleName()).ToCString());
          }
        }
      }
      key_value_array.SetAt(i, arg->AsLiteralNode()->literal());
    }
    key_value_array ^= TryCanonicalize(key_value_array, TokenPos());
    key_value_array.MakeImmutable();

    // Construct the map object.
    const Class& immutable_map_class =
        Class::Handle(LookupCoreClass(Symbols::ImmutableMap()));
    ASSERT(!immutable_map_class.IsNull());
    // If the immutable map class extends other parameterized classes, we need
    // to adjust the type argument vector. This is currently not the case.
    ASSERT(immutable_map_class.NumTypeArguments() == 2);
    ArgumentListNode* constr_args = new ArgumentListNode(TokenPos());
    constr_args->Add(new LiteralNode(literal_pos, key_value_array));
    const Function& map_constr =
        Function::ZoneHandle(immutable_map_class.LookupConstructor(
            PrivateCoreLibName(Symbols::ImmutableMapConstructor())));
    ASSERT(!map_constr.IsNull());
    const Object& constructor_result = Object::Handle(
        EvaluateConstConstructorCall(immutable_map_class,
                                     map_type_arguments,
                                     map_constr,
                                     constr_args));
    if (constructor_result.IsUnhandledException()) {
      return GenerateRethrow(literal_pos, constructor_result);
    } else {
      const Instance& const_instance = Instance::Cast(constructor_result);
      return new LiteralNode(literal_pos,
                             Instance::ZoneHandle(const_instance.raw()));
    }
  } else {
    // Factory call at runtime.
    const Class& factory_class =
        Class::Handle(LookupCoreClass(Symbols::Map()));
    ASSERT(!factory_class.IsNull());
    const Function& factory_method = Function::ZoneHandle(
        factory_class.LookupFactory(
            PrivateCoreLibName(Symbols::MapLiteralFactory())));
    ASSERT(!factory_method.IsNull());
    if (!map_type_arguments.IsNull() &&
        !map_type_arguments.IsInstantiated() &&
        (current_block_->scope->function_level() > 0)) {
      // Make sure that the instantiator is captured.
      CaptureInstantiator();
    }
    AbstractTypeArguments& factory_type_args =
        AbstractTypeArguments::ZoneHandle(map_type_arguments.raw());
    // If the factory class extends other parameterized classes, adjust the
    // type argument vector.
    if (!factory_type_args.IsNull() && (factory_class.NumTypeArguments() > 2)) {
      ASSERT(factory_type_args.Length() == 2);
      Type& factory_type = Type::Handle(Type::New(
          factory_class, factory_type_args, type_pos, Heap::kNew));
      factory_type ^= ClassFinalizer::FinalizeType(
          current_class(), factory_type, ClassFinalizer::kFinalize);
      factory_type_args = factory_type.arguments();
      ASSERT(factory_type_args.Length() == factory_class.NumTypeArguments());
    }
    factory_type_args = factory_type_args.Canonicalize();
    ArgumentListNode* factory_param = new ArgumentListNode(literal_pos);
    // The kv_pair array is temporary and of element type dynamic. It is passed
    // to the factory to initialize a properly typed map.
    ArrayNode* kv_pairs = new ArrayNode(
        TokenPos(),
        Type::ZoneHandle(Type::ArrayType()),
        kv_pairs_list);
    factory_param->Add(kv_pairs);
    return CreateConstructorCallNode(literal_pos,
                                     factory_type_args,
                                     factory_method,
                                     factory_param);
  }
}


AstNode* Parser::ParseCompoundLiteral() {
  TRACE_PARSER("ParseCompoundLiteral");
  bool is_const = false;
  if (CurrentToken() == Token::kCONST) {
    is_const = true;
    ConsumeToken();
  }
  const intptr_t type_pos = TokenPos();
  AbstractTypeArguments& type_arguments = AbstractTypeArguments::Handle(
      ParseTypeArguments(ClassFinalizer::kCanonicalize));
  // Map and List interfaces do not declare bounds on their type parameters, so
  // we should never see a malformed type argument mapped to dynamic here.
  AstNode* primary = NULL;
  if ((CurrentToken() == Token::kLBRACK) ||
      (CurrentToken() == Token::kINDEX)) {
    primary = ParseListLiteral(type_pos, is_const, type_arguments);
  } else if (CurrentToken() == Token::kLBRACE) {
    primary = ParseMapLiteral(type_pos, is_const, type_arguments);
  } else {
    ErrorMsg("unexpected token %s", Token::Str(CurrentToken()));
  }
  return primary;
}


static const String& BuildConstructorName(const String& type_class_name,
                                          const String* named_constructor) {
  // By convention, the static function implementing a named constructor 'C'
  // for class 'A' is labeled 'A.C', and the static function implementing the
  // unnamed constructor for class 'A' is labeled 'A.'.
  // This convention prevents users from explicitly calling constructors.
  String& constructor_name =
      String::Handle(String::Concat(type_class_name, Symbols::Dot()));
  if (named_constructor != NULL) {
    constructor_name = String::Concat(constructor_name, *named_constructor);
  }
  return constructor_name;
}


AstNode* Parser::ParseNewOperator(Token::Kind op_kind) {
  TRACE_PARSER("ParseNewOperator");
  const intptr_t new_pos = TokenPos();
  ASSERT((op_kind == Token::kNEW) || (op_kind == Token::kCONST));
  bool is_const = (op_kind == Token::kCONST);
  if (!IsIdentifier()) {
    ErrorMsg("type name expected");
  }
  intptr_t type_pos = TokenPos();
  AbstractType& type = AbstractType::Handle(
      ParseType(ClassFinalizer::kCanonicalizeWellFormed));
  // In case the type is malformed, throw a dynamic type error after finishing
  // parsing the instance creation expression.
  if (!type.IsMalformed()) {
    if (type.IsTypeParameter() || type.IsDynamicType()) {
      // Replace the type with a malformed type.
      type = ClassFinalizer::NewFinalizedMalformedType(
          Error::Handle(),  // No previous error.
          current_class(),
          type_pos,
          "%s'%s' cannot be instantiated",
          type.IsTypeParameter() ? "type parameter " : "",
          type.IsTypeParameter() ?
              String::Handle(type.UserVisibleName()).ToCString() : "dynamic");
    } else if (FLAG_enable_type_checks || FLAG_error_on_bad_type) {
      Error& bound_error = Error::Handle();
      if (type.IsMalboundedWithError(&bound_error)) {
        // Replace the type with a malformed type.
        type = ClassFinalizer::NewFinalizedMalformedType(
            bound_error,
            current_class(),
            type_pos,
            "malbounded type '%s' cannot be instantiated",
            String::Handle(type.UserVisibleName()).ToCString());
      }
    }
  }

  // The grammar allows for an optional ('.' identifier)? after the type, which
  // is a named constructor. Note that ParseType() above will not consume it as
  // part of a misinterpreted qualified identifier, because only a valid library
  // prefix is accepted as qualifier.
  String* named_constructor = NULL;
  if (CurrentToken() == Token::kPERIOD) {
    ConsumeToken();
    named_constructor = ExpectIdentifier("name of constructor expected");
  }

  // Parse constructor parameters.
  if (CurrentToken() != Token::kLPAREN) {
    ErrorMsg("'(' expected");
  }
  intptr_t call_pos = TokenPos();
  ArgumentListNode* arguments = ParseActualParameters(NULL, is_const);

  // Parsing is complete, so we can return a throw in case of a malformed type
  // or report a compile-time error if the constructor is const.
  if (type.IsMalformed()) {
    if (is_const) {
      const Error& error = Error::Handle(type.malformed_error());
      ErrorMsg(error);
    }
    return ThrowTypeError(type_pos, type);
  }

  // Resolve the type and optional identifier to a constructor or factory.
  Class& type_class = Class::Handle(type.type_class());
  const String& type_class_name = String::Handle(type_class.Name());
  AbstractTypeArguments& type_arguments =
      AbstractTypeArguments::ZoneHandle(type.arguments());

  // A constructor has an implicit 'this' parameter (instance to construct)
  // and a factory has an implicit 'this' parameter (type_arguments).
  // A constructor has a second implicit 'phase' parameter.
  intptr_t arguments_length = arguments->length() + 2;

  // An additional type check of the result of a redirecting factory may be
  // required.
  AbstractType& type_bound = AbstractType::ZoneHandle();

  // Make sure that an appropriate constructor exists.
  const String& constructor_name =
      BuildConstructorName(type_class_name, named_constructor);
  Function& constructor = Function::ZoneHandle(
      type_class.LookupConstructor(constructor_name));
  if (constructor.IsNull()) {
    constructor = type_class.LookupFactory(constructor_name);
    if (constructor.IsNull()) {
      const String& external_constructor_name =
          (named_constructor ? constructor_name : type_class_name);
      // Replace the type with a malformed type and compile a throw or report a
      // compile-time error if the constructor is const.
      if (is_const) {
        type = ClassFinalizer::NewFinalizedMalformedType(
            Error::Handle(),  // No previous error.
            current_class(),
            call_pos,
            "class '%s' has no constructor or factory named '%s'",
            String::Handle(type_class.Name()).ToCString(),
            external_constructor_name.ToCString());
        const Error& error = Error::Handle(type.malformed_error());
        ErrorMsg(error);
      }
      return ThrowNoSuchMethodError(call_pos,
                                    type_class,
                                    external_constructor_name,
                                    arguments,
                                    InvocationMirror::kConstructor,
                                    InvocationMirror::kMethod);
    } else if (constructor.IsRedirectingFactory()) {
      ClassFinalizer::ResolveRedirectingFactory(type_class, constructor);
      Type& redirect_type = Type::Handle(constructor.RedirectionType());
      if (!redirect_type.IsMalformed() && !redirect_type.IsInstantiated()) {
        // The type arguments of the redirection type are instantiated from the
        // type arguments of the parsed type of the 'new' or 'const' expression.
        Error& malformed_error = Error::Handle();
        redirect_type ^= redirect_type.InstantiateFrom(type_arguments,
                                                       &malformed_error);
        if (!malformed_error.IsNull()) {
          redirect_type.set_malformed_error(malformed_error);
        }
      }
      if (redirect_type.IsMalformed()) {
        if (is_const) {
          ErrorMsg(Error::Handle(redirect_type.malformed_error()));
        }
        return ThrowTypeError(redirect_type.token_pos(), redirect_type);
      }
      if (FLAG_enable_type_checks && !redirect_type.IsSubtypeOf(type, NULL)) {
        // Additional type checking of the result is necessary.
        type_bound = type.raw();
      }
      type = redirect_type.raw();
      type_class = type.type_class();
      type_arguments = type.arguments();
      constructor = constructor.RedirectionTarget();
      ASSERT(!constructor.IsNull());
    }
    if (constructor.IsFactory()) {
      // A factory does not have the implicit 'phase' parameter.
      arguments_length -= 1;
    }
  }

  // It is ok to call a factory method of an abstract class, but it is
  // a dynamic error to instantiate an abstract class.
  ASSERT(!constructor.IsNull());
  if (type_class.is_abstract() && !constructor.IsFactory()) {
    ArgumentListNode* arguments = new ArgumentListNode(type_pos);
    arguments->Add(new LiteralNode(
        TokenPos(), Integer::ZoneHandle(Integer::New(type_pos))));
    arguments->Add(new LiteralNode(
        TokenPos(), String::ZoneHandle(type_class_name.raw())));
    return MakeStaticCall(Symbols::AbstractClassInstantiationError(),
                          PrivateCoreLibName(Symbols::ThrowNew()),
                          arguments);
  }
  String& error_message = String::Handle();
  if (!constructor.AreValidArguments(arguments_length,
                                     arguments->names(),
                                     &error_message)) {
    const String& external_constructor_name =
        (named_constructor ? constructor_name : type_class_name);
    if (is_const) {
      ErrorMsg(call_pos,
               "invalid arguments passed to constructor '%s' "
               "for class '%s': %s",
               external_constructor_name.ToCString(),
               String::Handle(type_class.Name()).ToCString(),
               error_message.ToCString());
    }
    return ThrowNoSuchMethodError(call_pos,
                                  type_class,
                                  external_constructor_name,
                                  arguments,
                                  InvocationMirror::kConstructor,
                                  InvocationMirror::kMethod);
  }

  // Return a throw in case of a malformed type or report a compile-time error
  // if the constructor is const.
  if (type.IsMalformed()) {
    if (is_const) {
      const Error& error = Error::Handle(type.malformed_error());
      ErrorMsg(error);
    }
    return ThrowTypeError(type_pos, type);
  }
  type_arguments ^= type_arguments.Canonicalize();
  // Make the constructor call.
  AstNode* new_object = NULL;
  if (is_const) {
    if (!constructor.is_const()) {
      ErrorMsg("'const' requires const constructor: '%s'",
          String::Handle(constructor.name()).ToCString());
    }
    const Object& constructor_result = Object::Handle(
        EvaluateConstConstructorCall(type_class,
                                     type_arguments,
                                     constructor,
                                     arguments));
    if (constructor_result.IsUnhandledException()) {
      new_object = GenerateRethrow(new_pos, constructor_result);
    } else {
      const Instance& const_instance = Instance::Cast(constructor_result);
      new_object = new LiteralNode(new_pos,
                                   Instance::ZoneHandle(const_instance.raw()));
      if (!type_bound.IsNull()) {
        ASSERT(!type_bound.IsMalformed());
        Error& malformed_error = Error::Handle();
        ASSERT(!is_top_level_);  // We cannot check unresolved types.
        if (!const_instance.IsInstanceOf(type_bound,
                                         TypeArguments::Handle(),
                                         &malformed_error)) {
          type_bound = ClassFinalizer::NewFinalizedMalformedType(
              malformed_error,
              current_class(),
              new_pos,
              "const factory result is not an instance of '%s'",
              String::Handle(type_bound.UserVisibleName()).ToCString());
          new_object = ThrowTypeError(new_pos, type_bound);
        }
        type_bound = AbstractType::null();
      }
    }
  } else {
    CheckConstructorCallTypeArguments(new_pos, constructor, type_arguments);
    if (!type_arguments.IsNull() &&
        !type_arguments.IsInstantiated() &&
        (current_block_->scope->function_level() > 0)) {
      // Make sure that the instantiator is captured.
      CaptureInstantiator();
    }
    // If the type argument vector is not instantiated, we verify in checked
    // mode at runtime that it is within its declared bounds.
    new_object = CreateConstructorCallNode(
        new_pos, type_arguments, constructor, arguments);
  }
  if (!type_bound.IsNull()) {
    new_object = new AssignableNode(new_pos,
                                    new_object,
                                    type_bound,
                                    Symbols::FactoryResult());
  }
  return new_object;
}


String& Parser::Interpolate(const GrowableArray<AstNode*>& values) {
  const Class& cls = Class::Handle(LookupCoreClass(Symbols::StringBase()));
  ASSERT(!cls.IsNull());
  const Function& func =
      Function::Handle(cls.LookupStaticFunction(
          PrivateCoreLibName(Symbols::Interpolate())));
  ASSERT(!func.IsNull());

  // Build the array of literal values to interpolate.
  const Array& value_arr = Array::Handle(Array::New(values.length()));
  for (int i = 0; i < values.length(); i++) {
    ASSERT(values[i]->IsLiteralNode());
    value_arr.SetAt(i, values[i]->AsLiteralNode()->literal());
  }

  // Build argument array to pass to the interpolation function.
  const Array& interpolate_arg = Array::Handle(Array::New(1));
  interpolate_arg.SetAt(0, value_arr);

  // Call interpolation function.
  String& concatenated = String::ZoneHandle();
  concatenated ^= DartEntry::InvokeFunction(func, interpolate_arg);
  if (concatenated.IsUnhandledException()) {
    ErrorMsg("Exception thrown in Parser::Interpolate");
  }
  concatenated = Symbols::New(concatenated);
  return concatenated;
}


// A string literal consists of the concatenation of the next n tokens
// that satisfy the EBNF grammar:
// literal = kSTRING {{ interpol } kSTRING }
// interpol = kINTERPOL_VAR | (kINTERPOL_START expression kINTERPOL_END)
// In other words, the scanner breaks down interpolated strings so that
// a string literal always begins and ends with a kSTRING token.
AstNode* Parser::ParseStringLiteral() {
  TRACE_PARSER("ParseStringLiteral");
  AstNode* primary = NULL;
  const intptr_t literal_start = TokenPos();
  ASSERT(CurrentToken() == Token::kSTRING);
  Token::Kind l1_token = LookaheadToken(1);
  if ((l1_token != Token::kSTRING) &&
      (l1_token != Token::kINTERPOL_VAR) &&
      (l1_token != Token::kINTERPOL_START)) {
    // Common case: no interpolation.
    primary = new LiteralNode(literal_start, *CurrentLiteral());
    ConsumeToken();
    return primary;
  }
  // String interpolation needed.
  bool is_compiletime_const = true;
  GrowableArray<AstNode*> values_list;
  while (CurrentToken() == Token::kSTRING) {
    values_list.Add(new LiteralNode(TokenPos(), *CurrentLiteral()));
    ConsumeToken();
    while ((CurrentToken() == Token::kINTERPOL_VAR) ||
        (CurrentToken() == Token::kINTERPOL_START)) {
      AstNode* expr = NULL;
      const intptr_t expr_pos = TokenPos();
      if (CurrentToken() == Token::kINTERPOL_VAR) {
        expr = ResolveIdent(TokenPos(), *CurrentLiteral(), true);
        ConsumeToken();
      } else {
        ASSERT(CurrentToken() == Token::kINTERPOL_START);
        ConsumeToken();
        expr = ParseExpr(kAllowConst, kConsumeCascades);
        ExpectToken(Token::kINTERPOL_END);
      }
      // Check if this interpolated string is still considered a compile time
      // constant. If it is we need to evaluate if the current string part is
      // a constant or not. Only stings, numbers, booleans and null values
      // are allowed in compile time const interpolations.
      if (is_compiletime_const) {
        const Object* const_expr = expr->EvalConstExpr();
        if ((const_expr != NULL) &&
            (const_expr->IsNumber() ||
            const_expr->IsString() ||
            const_expr->IsBool() ||
            const_expr->IsNull())) {
          // Change expr into a literal.
          expr = new LiteralNode(expr_pos, EvaluateConstExpr(expr));
        } else {
          is_compiletime_const = false;
        }
      }
      values_list.Add(expr);
    }
  }
  if (is_compiletime_const) {
    primary = new LiteralNode(literal_start, Interpolate(values_list));
  } else {
    ArgumentListNode* interpolate_arg = new ArgumentListNode(TokenPos());
    ArrayNode* values = new ArrayNode(
        TokenPos(),
        Type::ZoneHandle(Type::ArrayType()),
        values_list);
    interpolate_arg->Add(values);
    primary = MakeStaticCall(Symbols::StringBase(),
                             PrivateCoreLibName(Symbols::Interpolate()),
                             interpolate_arg);
  }
  return primary;
}


AstNode* Parser::ParsePrimary() {
  TRACE_PARSER("ParsePrimary");
  ASSERT(!is_top_level_);
  AstNode* primary = NULL;
  if (IsFunctionLiteral()) {
    // The name of a literal function is visible from inside the function, but
    // must not collide with names in the scope declaring the literal.
    OpenBlock();
    primary = ParseFunctionStatement(true);
    CloseBlock();
  } else if (IsIdentifier()) {
    QualIdent qual_ident;
    ParseQualIdent(&qual_ident);
    if (qual_ident.lib_prefix == NULL) {
      if (!ResolveIdentInLocalScope(qual_ident.ident_pos,
                                    *qual_ident.ident,
                                    &primary)) {
        // Check whether the identifier is a type parameter.
        if (!current_class().IsNull()) {
          TypeParameter& type_param = TypeParameter::ZoneHandle(
              current_class().LookupTypeParameter(*(qual_ident.ident)));
          if (!type_param.IsNull()) {
            return new PrimaryNode(qual_ident.ident_pos, type_param);
          }
        }
        // This is a non-local unqualified identifier so resolve the
        // identifier locally in the main app library and all libraries
        // imported by it.
        primary = ResolveIdentInCurrentLibraryScope(qual_ident.ident_pos,
                                                    *qual_ident.ident);
      }
    } else {
      // This is a qualified identifier with a library prefix so resolve
      // the identifier locally in that library (we do not include the
      // libraries imported by that library).
      primary = ResolveIdentInPrefixScope(qual_ident.ident_pos,
                                          *qual_ident.lib_prefix,
                                          *qual_ident.ident);
    }
    ASSERT(primary != NULL);
  } else if (CurrentToken() == Token::kTHIS) {
    LocalVariable* local = LookupLocalScope(Symbols::This());
    if (local == NULL) {
      ErrorMsg("receiver 'this' is not in scope");
    }
    primary = new LoadLocalNode(TokenPos(), local);
    ConsumeToken();
  } else if (CurrentToken() == Token::kINTEGER) {
    const Integer& literal = Integer::ZoneHandle(CurrentIntegerLiteral());
    primary = new LiteralNode(TokenPos(), literal);
    ConsumeToken();
  } else if (CurrentToken() == Token::kTRUE) {
    primary = new LiteralNode(TokenPos(), Bool::True());
    ConsumeToken();
  } else if (CurrentToken() == Token::kFALSE) {
    primary = new LiteralNode(TokenPos(), Bool::False());
    ConsumeToken();
  } else if (CurrentToken() == Token::kNULL) {
    primary = new LiteralNode(TokenPos(), Instance::ZoneHandle());
    ConsumeToken();
  } else if (CurrentToken() == Token::kLPAREN) {
    ConsumeToken();
    const bool saved_mode = SetAllowFunctionLiterals(true);
    primary = ParseExpr(kAllowConst, kConsumeCascades);
    SetAllowFunctionLiterals(saved_mode);
    ExpectToken(Token::kRPAREN);
  } else if (CurrentToken() == Token::kDOUBLE) {
    Double& double_value = Double::ZoneHandle(CurrentDoubleLiteral());
    if (double_value.IsNull()) {
      ErrorMsg("invalid double literal");
    }
    primary = new LiteralNode(TokenPos(), double_value);
    ConsumeToken();
  } else if (CurrentToken() == Token::kSTRING) {
    primary = ParseStringLiteral();
  } else if (CurrentToken() == Token::kNEW) {
    ConsumeToken();
    primary = ParseNewOperator(Token::kNEW);
  } else if (CurrentToken() == Token::kCONST) {
    if ((LookaheadToken(1) == Token::kLT) ||
        (LookaheadToken(1) == Token::kLBRACK) ||
        (LookaheadToken(1) == Token::kINDEX) ||
        (LookaheadToken(1) == Token::kLBRACE)) {
      primary = ParseCompoundLiteral();
    } else {
      ConsumeToken();
      primary = ParseNewOperator(Token::kCONST);
    }
  } else if (CurrentToken() == Token::kLT ||
             CurrentToken() == Token::kLBRACK ||
             CurrentToken() == Token::kINDEX ||
             CurrentToken() == Token::kLBRACE) {
    primary = ParseCompoundLiteral();
  } else if (CurrentToken() == Token::kSUPER) {
    if (current_function().is_static()) {
      ErrorMsg("cannot access superclass from static method");
    }
    if (current_class().SuperClass() == Class::null()) {
      ErrorMsg("class '%s' does not have a superclass",
               String::Handle(current_class().Name()).ToCString());
    }
    if (current_class().mixin() != Type::null()) {
      const Type& mixin_type = Type::Handle(current_class().mixin());
      if (mixin_type.type_class() == current_function().origin()) {
        ErrorMsg("class '%s' may not use super "
                 "because it is used as mixin class",
                 String::Handle(current_class().Name()).ToCString());
      }
    }
    ConsumeToken();
    if (CurrentToken() == Token::kPERIOD) {
      ConsumeToken();
      const String& ident = *ExpectIdentifier("identifier expected");
      if (CurrentToken() == Token::kLPAREN) {
        primary = ParseSuperCall(ident);
      } else {
        primary = ParseSuperFieldAccess(ident);
      }
    } else if ((CurrentToken() == Token::kLBRACK) ||
        Token::CanBeOverloaded(CurrentToken()) ||
        (CurrentToken() == Token::kNE)) {
      primary = ParseSuperOperator();
    } else {
      primary = new PrimaryNode(TokenPos(), Symbols::Super());
    }
  } else {
    UnexpectedToken();
  }
  return primary;
}


// Evaluate expression in expr and return the value. The expression must
// be a compile time constant.
const Instance& Parser::EvaluateConstExpr(AstNode* expr) {
  if (expr->IsLiteralNode()) {
    return expr->AsLiteralNode()->literal();
  } else if (expr->IsLoadLocalNode() &&
      expr->AsLoadLocalNode()->local().IsConst()) {
    return *expr->AsLoadLocalNode()->local().ConstValue();
  } else {
    ASSERT(expr->EvalConstExpr() != NULL);
    ReturnNode* ret = new ReturnNode(expr->token_pos(), expr);
    // Compile time constant expressions cannot reference anything from a
    // local scope.
    LocalScope* empty_scope = new LocalScope(NULL, 0, 0);
    SequenceNode* seq = new SequenceNode(expr->token_pos(), empty_scope);
    seq->Add(ret);

    Object& result = Object::Handle(Compiler::ExecuteOnce(seq));
    if (result.IsError()) {
      // Propagate the compilation error.
      isolate()->long_jump_base()->Jump(1, Error::Cast(result));
      UNREACHABLE();
    }
    ASSERT(result.IsInstance());
    Instance& value = Instance::ZoneHandle();
    value ^= result.raw();
    value = TryCanonicalize(value, TokenPos());
    return value;
  }
}


void Parser::SkipFunctionLiteral() {
  if (IsIdentifier()) {
    if (LookaheadToken(1) != Token::kLPAREN) {
      SkipType(true);
    }
    ExpectIdentifier("function name expected");
  }
  if (CurrentToken() == Token::kLPAREN) {
    const bool allow_explicit_default_values = true;
    ParamList params;
    params.skipped = true;
    ParseFormalParameterList(allow_explicit_default_values, &params);
  }
  if (CurrentToken() == Token::kLBRACE) {
    SkipBlock();
  } else if (CurrentToken() == Token::kARROW) {
    ConsumeToken();
    SkipExpr();
  }
}


// Skips function/method/constructor/getter/setter preambles until the formal
// parameter list. It is enough to skip the tokens, since we have already
// previously parsed the function.
void Parser::SkipFunctionPreamble() {
  while (true) {
    if (CurrentToken() == Token::kLPAREN ||
        CurrentToken() == Token::kARROW ||
        CurrentToken() == Token::kSEMICOLON ||
        CurrentToken() == Token::kLBRACE) {
      return;
    }
    // Case handles "native" keyword, but also return types of form
    // native.SomeType where native is the name of a library.
    if (CurrentToken() == Token::kIDENT &&
        LookaheadToken(1) != Token::kPERIOD) {
      if (CurrentLiteral()->raw() == Symbols::Native().raw()) {
        return;
      }
    }
    ConsumeToken();
  }
}


void Parser::SkipListLiteral() {
  if (CurrentToken() == Token::kINDEX) {
    // Empty list literal.
    ConsumeToken();
    return;
  }
  ExpectToken(Token::kLBRACK);
  while (CurrentToken() != Token::kRBRACK) {
    SkipNestedExpr();
    if (CurrentToken() == Token::kCOMMA) {
      ConsumeToken();
    } else {
      break;
    }
  }
  ExpectToken(Token::kRBRACK);
}


void Parser::SkipMapLiteral() {
  ExpectToken(Token::kLBRACE);
  while (CurrentToken() != Token::kRBRACE) {
    SkipNestedExpr();
    ExpectToken(Token::kCOLON);
    SkipNestedExpr();
    if (CurrentToken() == Token::kCOMMA) {
      ConsumeToken();
    } else {
      break;
    }
  }
  ExpectToken(Token::kRBRACE);
}


void Parser::SkipActualParameters() {
  ExpectToken(Token::kLPAREN);
  while (CurrentToken() != Token::kRPAREN) {
    if (IsIdentifier() && (LookaheadToken(1) == Token::kCOLON)) {
      // Named actual parameter.
      ConsumeToken();
      ConsumeToken();
    }
    SkipNestedExpr();
    if (CurrentToken() == Token::kCOMMA) {
      ConsumeToken();
    }
  }
  ExpectToken(Token::kRPAREN);
}


void Parser::SkipCompoundLiteral() {
  if (CurrentToken() == Token::kLT) {
    SkipTypeArguments();
  }
  if ((CurrentToken() == Token::kLBRACK) ||
      (CurrentToken() == Token::kINDEX)) {
    SkipListLiteral();
  } else if (CurrentToken() == Token::kLBRACE) {
    SkipMapLiteral();
  }
}


void Parser::SkipNewOperator() {
  ConsumeToken();  // Skip new or const keyword.
  if (IsIdentifier()) {
    SkipType(false);
    if (CurrentToken() == Token::kLPAREN) {
      SkipActualParameters();
      return;
    }
  }
}


void Parser::SkipStringLiteral() {
  ASSERT(CurrentToken() == Token::kSTRING);
  while (CurrentToken() == Token::kSTRING) {
    ConsumeToken();
    while (true) {
      if (CurrentToken() == Token::kINTERPOL_VAR) {
        ConsumeToken();
      } else if (CurrentToken() == Token::kINTERPOL_START) {
        ConsumeToken();
        SkipExpr();
        ExpectToken(Token::kINTERPOL_END);
      } else {
        break;
      }
    }
  }
}


void Parser::SkipPrimary() {
  if (IsFunctionLiteral()) {
    SkipFunctionLiteral();
    return;
  }
  switch (CurrentToken()) {
    case Token::kTHIS:
    case Token::kSUPER:
    case Token::kNULL:
    case Token::kTRUE:
    case Token::kFALSE:
    case Token::kINTEGER:
    case Token::kDOUBLE:
      ConsumeToken();
      break;
    case Token::kIDENT:
      ConsumeToken();
      break;
    case Token::kSTRING:
      SkipStringLiteral();
      break;
    case Token::kLPAREN:
      ConsumeToken();
      SkipNestedExpr();
      ExpectToken(Token::kRPAREN);
      break;
    case Token::kNEW:
      SkipNewOperator();
      break;
    case Token::kCONST:
      if ((LookaheadToken(1) == Token::kLT) ||
          (LookaheadToken(1) == Token::kLBRACE) ||
          (LookaheadToken(1) == Token::kLBRACK) ||
          (LookaheadToken(1) == Token::kINDEX)) {
        ConsumeToken();
        SkipCompoundLiteral();
      } else {
        SkipNewOperator();
      }
      break;
    case Token::kLT:
    case Token::kLBRACE:
    case Token::kLBRACK:
    case Token::kINDEX:
      SkipCompoundLiteral();
      break;
    default:
      if (IsIdentifier()) {
        ConsumeToken();  // Handle pseudo-keyword identifiers.
      } else {
        UnexpectedToken();
        UNREACHABLE();
      }
      break;
  }
}


void Parser::SkipSelectors() {
  while (true) {
    if (CurrentToken() == Token::kCASCADE) {
      ConsumeToken();
      if (CurrentToken() == Token::kLBRACK) {
        continue;  // Consume [ in next loop iteration.
      } else {
        ExpectIdentifier("identifier or [ expected after ..");
      }
    } else if (CurrentToken() == Token::kPERIOD) {
      ConsumeToken();
      ExpectIdentifier("identifier expected");
    } else if (CurrentToken() == Token::kLBRACK) {
      ConsumeToken();
      SkipNestedExpr();
      ExpectToken(Token::kRBRACK);
    } else if (CurrentToken() == Token::kLPAREN) {
      SkipActualParameters();
    } else {
      break;
    }
  }
}

void Parser::SkipPostfixExpr() {
  SkipPrimary();
  SkipSelectors();
  if (IsIncrementOperator(CurrentToken())) {
    ConsumeToken();
  }
}


void Parser::SkipUnaryExpr() {
  if (IsPrefixOperator(CurrentToken()) ||
      IsIncrementOperator(CurrentToken())) {
    ConsumeToken();
    SkipUnaryExpr();
  } else {
    SkipPostfixExpr();
  }
}


void Parser::SkipBinaryExpr() {
  SkipUnaryExpr();
  const int min_prec = Token::Precedence(Token::kOR);
  const int max_prec = Token::Precedence(Token::kMUL);
  while (((min_prec <= Token::Precedence(CurrentToken())) &&
      (Token::Precedence(CurrentToken()) <= max_prec)) ||
      IsLiteral("as")) {
    Token::Kind last_token = IsLiteral("as") ? Token::kAS : CurrentToken();
    ConsumeToken();
    if (last_token == Token::kIS) {
      if (CurrentToken() == Token::kNOT) {
        ConsumeToken();
      }
      SkipType(false);
    } else if (last_token == Token::kAS) {
      SkipType(false);
    } else {
      SkipUnaryExpr();
    }
  }
}


void Parser::SkipConditionalExpr() {
  SkipBinaryExpr();
  if (CurrentToken() == Token::kCONDITIONAL) {
    ConsumeToken();
    SkipExpr();
    ExpectToken(Token::kCOLON);
    SkipExpr();
  }
}


void Parser::SkipExpr() {
  while (CurrentToken() == Token::kTHROW) {
    ConsumeToken();
  }
  SkipConditionalExpr();
  if (CurrentToken() == Token::kCASCADE) {
    SkipSelectors();
  }
  if (Token::IsAssignmentOperator(CurrentToken())) {
    ConsumeToken();
    SkipExpr();
  }
}


void Parser::SkipNestedExpr() {
  const bool saved_mode = SetAllowFunctionLiterals(true);
  SkipExpr();
  SetAllowFunctionLiterals(saved_mode);
}


void Parser::SkipQualIdent() {
  ASSERT(IsIdentifier());
  ConsumeToken();
  if (CurrentToken() == Token::kPERIOD) {
    ConsumeToken();  // Consume the kPERIOD token.
    ExpectIdentifier("identifier expected after '.'");
  }
}

}  // namespace dart
