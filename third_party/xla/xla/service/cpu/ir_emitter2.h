/* Copyright 2024 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef XLA_SERVICE_CPU_IR_EMITTER2_H_
#define XLA_SERVICE_CPU_IR_EMITTER2_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/service/llvm_ir/ir_array.h"
#include "xla/shape.h"

namespace xla::cpu {

// IrEmitter emits host kernels form HLO instructions into the LLVM module(s).
//
// Host kernel is simply a function that implements StreamExecutor HostKernel
// interface (defined as C API for ABI stability), and XLA:CPU runtime is
// responsible for launching host kernels on the host as a part of the Thunk
// sequence execution.
//
// In addition to a host kernel function itself, host kernel defines how much
// concurrency it can support by picking the right thread and block sizes.
// Runtime might launch host kernel blocks and threads on a thread pool, with an
// assumption that threads and blocks that are close to each other in three
// dimensional space are likely to touch the same memory, and thus should be
// executed on the same thread (or same NUMA node).
//
// At run time thunks resolve kernel functions by name in the compiled LLVM
// module.
//
// WARNING: This is under construction and will eventually replace IrEmitter.
class IrEmitter2 {
 public:
  IrEmitter2(const HloModule& hlo_module, llvm::Module* module);

  // Thread dimensions of the kernel invocation.
  struct KernelThreadDims {
    llvm::Value* x;
    llvm::Value* y;
    llvm::Value* z;
  };

  // Thread coordinates of the kernel invocation.
  struct KernelThread {
    llvm::Value* x;
    llvm::Value* y;
    llvm::Value* z;
  };

  // A kernel function prototype with all the LLVM values that might be needed
  // to emit the actual kernel body.
  struct KernelPrototype {
    llvm::Function* function;

    // LLVM values identifying kernel invocation thread coordinates.
    KernelThreadDims thread_dims;
    KernelThread thread;

    // LLVM values corresponding to the kernel arguments and results arrays. All
    // tuples are flattened as we do not have any tuples at run time and only
    // read and write data from/to leaf arrays.
    std::vector<llvm_ir::IrArray> arguments;
    std::vector<llvm_ir::IrArray> results;
  };

  // A symbol name in the LLVM module that defines a host kernel.
  //
  // TODO(ezhulenev): In addition to a symbol name we also need to know the
  // block and thread sizes.
  struct KernelInfo {
    explicit KernelInfo(std::string name) : name(std::move(name)) {}
    std::string name;
  };

  // Returns all the kernels emitted so far via this emitter.
  absl::Span<const KernelInfo> kernels() const { return kernels_; }

  // Emits an elemental host kernel for the given HLO instruction.
  absl::StatusOr<KernelInfo> EmitElementalHostKernel(
      const HloInstruction* instr);

  // Emits a host kernel prototype and prepares function for emitting kernel
  // body into it.
  KernelPrototype EmitKernelPrototype(std::string_view name,
                                      absl::Span<const Shape> arguments,
                                      absl::Span<const Shape> results);

 private:
  class ElementalIrEmitter;

  KernelThreadDims EmitKernelThreadDims(llvm::IRBuilder<>& b,
                                        llvm::Value* call_frame);

  KernelThread EmitKernelThread(llvm::IRBuilder<>& b, llvm::Value* call_frame);

  llvm_ir::IrArray EmitKernelArgument(llvm::IRBuilder<>& b,
                                      llvm::Value* call_frame, int64_t index,
                                      const Shape& shape);

  const HloModule& hlo_module_;
  llvm::Module* module_;

  // LLVM types defining HostKernel API (see host_kernel_c_api.h).
  llvm::StructType* call_frame_ty_;
  llvm::StructType* thread_dims_ty_;
  llvm::StructType* thread_ty_;
  llvm::StructType* arg_ty_;

  // Keeps track of all the kernels emitted so far.
  std::vector<KernelInfo> kernels_;
};

}  // namespace xla::cpu

#endif  // XLA_SERVICE_CPU_IR_EMITTER2_H_
