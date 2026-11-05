//==-- LowerFunctionInfo.h - Represents of function argument/return types --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/inlcude/CodeGen/LowerFunctionInfo.h. The
// queries are adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERFUNCTIONINFO_H
#define LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERFUNCTIONINFO_H

#include "mlir/IR/Types.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/TrailingObjects.h"

namespace cir {

/// A class for recording the number of arguments that a function
/// signature requires.
class RequiredArgs {
  /// The number of required arguments, or ~0 if the signature does
  /// not permit optional arguments.
  unsigned NumRequired;

public:
  enum All_t { All };

  RequiredArgs(All_t _) : NumRequired(~0U) {}
  explicit RequiredArgs(unsigned n) : NumRequired(n) {
    cir_cconv_assert(n != ~0U);
  }

  /// Compute the arguments required by the given formal prototype,
  /// given that there may be some additional, non-formal arguments
  /// in play.
  ///
  /// If FD is not null, this will consider pass_object_size params in FD.
  static RequiredArgs forPrototypePlus(const FuncType prototype,
                                       unsigned additional) {
    if (!prototype.isVarArg())
      return All;

    return RequiredArgs(prototype.getNumInputs() + additional);
  }

  bool allowsOptionalArgs() const { return NumRequired != ~0U; }

  unsigned getNumRequiredArgs() const {
    assert(allowsOptionalArgs());
    return NumRequired;
  }
};

// Implementation detail of LowerFunctionInfo, factored out so it can be
// named in the TrailingObjects base class of CGFunctionInfo.
struct LowerFunctionInfoArgInfo {
  enum class TypeProvenance : uint8_t {
    None = 0,
    BitInt = 1u << 0,
    Matrix = 1u << 1,
  };

  mlir::Type type;        // Original ABI-agnostic type.
  cir::ABIArgInfo info;   // ABI-specific information.
  TypeProvenance provenance = TypeProvenance::None;

  static bool hasProvenance(TypeProvenance value, TypeProvenance flag) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
  }

  bool isBitIntType() const {
    return hasProvenance(provenance, TypeProvenance::BitInt);
  }
  bool isMatrixType() const {
    return hasProvenance(provenance, TypeProvenance::Matrix);
  }
};

inline constexpr LowerFunctionInfoArgInfo::TypeProvenance
operator|(LowerFunctionInfoArgInfo::TypeProvenance lhs,
          LowerFunctionInfoArgInfo::TypeProvenance rhs) {
  return static_cast<LowerFunctionInfoArgInfo::TypeProvenance>(
      static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline LowerFunctionInfoArgInfo::TypeProvenance &
operator|=(LowerFunctionInfoArgInfo::TypeProvenance &lhs,
           LowerFunctionInfoArgInfo::TypeProvenance rhs) {
  lhs = lhs | rhs;
  return lhs;
}

// FIXME(cir): We could likely encode this information within CIR/MLIR, allowing
// us to eliminate this class.
class LowerFunctionInfo final
    : private llvm::TrailingObjects<LowerFunctionInfo,
                                    LowerFunctionInfoArgInfo> {
  typedef LowerFunctionInfoArgInfo ArgInfo;

  /// The LLVM::CallingConv to use for this function (as specified by the
  /// user).
  unsigned CallingConvention : 8;

  /// The LLVM::CallingConv to actually use for this function, which may
  /// depend on the ABI.
  unsigned EffectiveCallingConvention : 8;

  /// Whether this is an instance method.
  unsigned InstanceMethod : 1;

  /// Whether this is a chain call.
  unsigned ChainCall : 1;

  /// Whether this function is called by forwarding arguments.
  /// This doesn't support inalloca or varargs.
  unsigned DelegateCall : 1;

  /// Whether this function has a __attribute__((regparm(N))) attribute. Kept
  /// distinct from RegParm==0 so that regparm(0) (an explicit "0 register
  /// parameters" request) can be told apart from no attribute at all, matching
  /// clang::FunctionType::ExtInfo's +1 encoding.
  unsigned HasRegParm : 1;

  /// The number of integer/pointer parameters to pass in registers, as
  /// specified by __attribute__((regparm(N))). Only meaningful when
  /// HasRegParm is set. N is in [0, 3].
  unsigned RegParm : 3;

  RequiredArgs Required;

  /// The record representing all arguments passed in memory.  Only used when
  /// passing non-trivial types with inalloca.  Not part of the profile.
  RecordType ArgRecord;

  unsigned NumArgs;

  const ArgInfo *getArgsBuffer() const { return getTrailingObjects(); }
  ArgInfo *getArgsBuffer() { return getTrailingObjects(); }

  LowerFunctionInfo() : Required(RequiredArgs::All) {}

public:
  // Override new/delete to bypass C++14 sized deallocation. LowerFunctionInfo
  // uses TrailingObjects and over-allocates via operator new(size), but
  // unique_ptr's default deleter calls operator delete(ptr, sizeof(T)) with
  // the static type size, causing an ASAN new-delete-type-mismatch.
  static void *operator new(size_t size) { return ::operator new(size); }
  static void *operator new(size_t /*size*/, void *ptr) { return ptr; }
  void operator delete(void *ptr) { ::operator delete(ptr); }
  void operator delete(void * /*ptr*/, void * /*place*/) {}

  static LowerFunctionInfo *create(unsigned llvmCC, bool instanceMethod,
                                   bool chainCall, bool delegateCall,
                                   mlir::Type resultType,
                                   llvm::ArrayRef<mlir::Type> argTypes,
                                   llvm::ArrayRef<ArgInfo::TypeProvenance>
                                       argProvenances,
                                   RequiredArgs required, bool hasRegParm = false,
                                   unsigned regParm = 0) {
    cir_cconv_assert(argProvenances.empty() ||
                     argProvenances.size() == argTypes.size());
    // CIR does not co-allocate ExtParameterInfo trailing objects because
    // extended parameter attributes (noescape, Swift ABI, etc.) are
    // propagated via per-argument attributes on FuncOp instead.
    void *buffer = operator new(totalSizeToAlloc<ArgInfo>(argTypes.size() + 1));

    LowerFunctionInfo *FI = new (buffer) LowerFunctionInfo();
    FI->CallingConvention = llvmCC;
    FI->EffectiveCallingConvention = llvmCC;
    FI->InstanceMethod = instanceMethod;
    FI->ChainCall = chainCall;
    FI->DelegateCall = delegateCall;
    FI->HasRegParm = hasRegParm;
    FI->RegParm = regParm;
    FI->Required = required;
    FI->ArgRecord = nullptr;
    FI->NumArgs = argTypes.size();

    // Trailing ArgInfo storage comes from raw operator new, so value-initialize
    // each element before ABI classification reads default provenance.
    for (unsigned i = 0, e = argTypes.size() + 1; i != e; ++i)
      new (&FI->getArgsBuffer()[i]) ArgInfo();

    FI->getArgsBuffer()[0].type = resultType;
    for (unsigned i = 0, e = argTypes.size(); i != e; ++i) {
      FI->getArgsBuffer()[i + 1].type = argTypes[i];
      if (!argProvenances.empty())
        FI->getArgsBuffer()[i + 1].provenance = argProvenances[i];
    }

    return FI;
  };

  // Friending class TrailingObjects is apparently not good enough for MSVC,
  // so these have to be public.
  friend class TrailingObjects;
  size_t numTrailingObjects(OverloadToken<ArgInfo>) const {
    return NumArgs + 1;
  }

  typedef const ArgInfo *const_arg_iterator;
  typedef ArgInfo *arg_iterator;

  llvm::MutableArrayRef<ArgInfo> arguments() {
    return llvm::MutableArrayRef<ArgInfo>(arg_begin(), NumArgs);
  }

  const_arg_iterator arg_begin() const { return getArgsBuffer() + 1; }
  const_arg_iterator arg_end() const { return getArgsBuffer() + 1 + NumArgs; }
  arg_iterator arg_begin() { return getArgsBuffer() + 1; }
  arg_iterator arg_end() { return getArgsBuffer() + 1 + NumArgs; }

  unsigned arg_size() const { return NumArgs; }

  bool isVariadic() const { return Required.allowsOptionalArgs(); }
  bool isInstanceMethod() const { return InstanceMethod; }
  bool isChainCall() const { return ChainCall; }
  bool getHasRegParm() const { return HasRegParm; }
  unsigned getRegParm() const { return RegParm; }
  unsigned getNumRequiredArgs() const {
    return isVariadic() ? Required.getNumRequiredArgs() : arg_size();
  }

  mlir::Type getReturnType() const { return getArgsBuffer()[0].type; }
  ArgInfo &getReturnArgInfo() { return getArgsBuffer()[0]; }
  const ArgInfo &getReturnArgInfo() const { return getArgsBuffer()[0]; }

  cir::ABIArgInfo &getReturnInfo() { return getArgsBuffer()[0].info; }
  const cir::ABIArgInfo &getReturnInfo() const {
    return getArgsBuffer()[0].info;
  }

  /// Return the user specified callingconvention, which has been translated
  /// into an LLVM CC.
  unsigned getCallingConvention() const { return CallingConvention; }

  /// Get the record type used to represent all the arguments in memory.
  RecordType getArgRecord() const { return ArgRecord; }
};

} // namespace cir

#endif // LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERFUNCTIONINFO_H
