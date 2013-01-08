// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_FLOW_GRAPH_COMPILER_ARM_H_
#define VM_FLOW_GRAPH_COMPILER_ARM_H_

#ifndef VM_FLOW_GRAPH_COMPILER_H_
#error Include flow_graph_compiler.h instead of flow_graph_compiler_arm.h.
#endif

#include "vm/flow_graph.h"
#include "vm/intermediate_language.h"

namespace dart {

class Assembler;
class Code;
template <typename T> class GrowableArray;
class ParsedFunction;

// Stubbed out implementation of graph compiler, bails out immediately if
// CompileGraph is called. The rest of the public API is UNIMPLEMENTED.
class FlowGraphCompiler : public FlowGraphVisitor {
 public:
  FlowGraphCompiler(Assembler* assembler,
                    const FlowGraph& flow_graph,
                    bool is_optimizing)
      : FlowGraphVisitor(flow_graph.reverse_postorder()),
        parsed_function_(flow_graph.parsed_function()),
        is_optimizing_(is_optimizing) {
  }

  virtual ~FlowGraphCompiler() { }

  static bool CanOptimize();
  bool CanOptimizeFunction() const;

  void CompileGraph();

  void FinalizePcDescriptors(const Code& code);
  void FinalizeStackmaps(const Code& code);
  void FinalizeVarDescriptors(const Code& code);
  void FinalizeExceptionHandlers(const Code& code);
  void FinalizeComments(const Code& code);

 private:
  // Bail out of the flow graph compiler.  Does not return to the caller.
  void Bailout(const char* reason);

  const ParsedFunction& parsed_function_;
  const bool is_optimizing_;

  DISALLOW_COPY_AND_ASSIGN(FlowGraphCompiler);
};

}  // namespace dart

#endif  // VM_FLOW_GRAPH_COMPILER_ARM_H_
