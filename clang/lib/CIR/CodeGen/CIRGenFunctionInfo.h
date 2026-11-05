//==-- CIRGenFunctionInfo.h - Representation of fn argument/return types ---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines CIRGenFunctionInfo and associated types used in representing the
// CIR source types and ABI-coerced types for function arguments and
// return values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_CIRGENFUNCTIONINFO_H
#define LLVM_CLANG_CIR_CIRGENFUNCTIONINFO_H

#include "clang/AST/CanonicalType.h"
#include "clang/AST/Type.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/TrailingObjects.h"

namespace clang::CIRGen {

/// Bitflags for function info arrangement, matching CodeGen's FnInfoOpts.
enum class FnInfoOpts {
  None = 0,
  IsInstanceMethod = 1 << 0,
  IsChainCall = 1 << 1,
  IsDelegateCall = 1 << 2,
};

inline FnInfoOpts operator|(FnInfoOpts a, FnInfoOpts b) {
  return static_cast<FnInfoOpts>(llvm::to_underlying(a) |
                                 llvm::to_underlying(b));
}

inline FnInfoOpts operator&(FnInfoOpts a, FnInfoOpts b) {
  return static_cast<FnInfoOpts>(llvm::to_underlying(a) &
                                 llvm::to_underlying(b));
}

inline FnInfoOpts &operator|=(FnInfoOpts &a, FnInfoOpts b) {
  a = a | b;
  return a;
}

/// A class for recording the number of arguments that a function signature
/// requires.
class RequiredArgs {
  /// The number of required arguments, or ~0 if the signature does not permit
  /// optional arguments.
  unsigned numRequired;

public:
  enum All_t { All };

  RequiredArgs(All_t _) : numRequired(~0U) {}
  explicit RequiredArgs(unsigned n) : numRequired(n) { assert(n != ~0U); }

  unsigned getOpaqueData() const { return numRequired; }

  bool allowsOptionalArgs() const { return numRequired != ~0U; }

  /// Compute the arguments required by the given formal prototype, given that
  /// there may be some additional, non-formal arguments in play.
  ///
  /// Accounts for pass_object_size parameters in the prototype's extended
  /// parameter info.
  static RequiredArgs
  getFromProtoWithExtraSlots(const clang::FunctionProtoType *prototype,
                             unsigned additional) {
    if (!prototype->isVariadic())
      return All;

    if (prototype->hasExtParameterInfos())
      additional += llvm::count_if(
          prototype->getExtParameterInfos(),
          [](const FunctionProtoType::ExtParameterInfo &extInfo) {
            return extInfo.hasPassObjectSize();
          });

    return RequiredArgs(prototype->getNumParams() + additional);
  }

  static RequiredArgs
  getFromProtoWithExtraSlots(clang::CanQual<clang::FunctionProtoType> prototype,
                             unsigned additional) {
    return getFromProtoWithExtraSlots(prototype.getTypePtr(), additional);
  }

  unsigned getNumRequiredArgs() const {
    assert(allowsOptionalArgs());
    return numRequired;
  }
};

// The TrailingObjects for this class contain the function return type in the
// first CanQualType slot, followed by the argument types.
class CIRGenFunctionInfo final
    : public llvm::FoldingSetNode,
      private llvm::TrailingObjects<CIRGenFunctionInfo, CanQualType> {
  RequiredArgs required;

  unsigned numArgs;

  /// The clang::FunctionType::ExtInfo for this function. Stores calling
  /// convention, noreturn, produces_result, regparm, etc. Required for
  /// correct FoldingSet deduplication — functions with the same signature
  /// but different calling conventions must not share a CIRGenFunctionInfo.
  FunctionType::ExtInfo extInfo;

  /// Whether this is an instance method.
  LLVM_PREFERRED_TYPE(bool)
  unsigned instanceMethod : 1;

  /// Whether this is a chain call.
  LLVM_PREFERRED_TYPE(bool)
  unsigned chainCall : 1;

  /// Whether this function is called by forwarding arguments.
  LLVM_PREFERRED_TYPE(bool)
  unsigned delegateCall : 1;

  CanQualType *getArgTypes() { return getTrailingObjects(); }
  const CanQualType *getArgTypes() const { return getTrailingObjects(); }

  CIRGenFunctionInfo()
      : required(RequiredArgs::All), instanceMethod(false), chainCall(false),
        delegateCall(false) {}

public:
  static CIRGenFunctionInfo *create(CanQualType resultType,
                                    llvm::ArrayRef<CanQualType> argTypes,
                                    RequiredArgs required,
                                    FunctionType::ExtInfo extInfo,
                                    bool instanceMethod, bool chainCall,
                                    bool delegateCall);

  void operator delete(void *p) { ::operator delete(p); }

  // Friending class TrailingObjects is apparantly not good enough for MSVC, so
  // these have to be public.
  friend class TrailingObjects;

  using const_arg_iterator = const CanQualType *;
  using arg_iterator = CanQualType *;

  /// Whether this function is noreturn.
  bool isNoReturn() const { return extInfo.getNoReturn(); }

  /// Whether this is a CMSE non-secure call.
  bool isCmseNSCall() const { return extInfo.getCmseNSCall(); }

  /// Whether this is an instance method.
  bool isInstanceMethod() const { return instanceMethod; }

  /// Whether this is a chain call.
  bool isChainCall() const { return chainCall; }

  /// Whether this function is called by forwarding arguments.
  bool isDelegateCall() const { return delegateCall; }

  /// Get the AST calling convention.
  CallingConv getASTCallingConvention() const { return extInfo.getCC(); }

  /// Get the raw ExtInfo.
  FunctionType::ExtInfo getExtInfo() const { return extInfo; }

  // This function has to be CamelCase because llvm::FoldingSet requires so.
  // NOLINTNEXTLINE(readability-identifier-naming)
  static void Profile(llvm::FoldingSetNodeID &id, bool instanceMethod,
                      bool chainCall, bool delegateCall,
                      RequiredArgs required, CanQualType resultType,
                      llvm::ArrayRef<CanQualType> argTypes,
                      FunctionType::ExtInfo extInfo) {
    id.AddBoolean(instanceMethod);
    id.AddBoolean(chainCall);
    id.AddBoolean(delegateCall);
    id.AddInteger(extInfo.getCC());
    id.AddBoolean(extInfo.getNoReturn());
    id.AddBoolean(extInfo.getProducesResult());
    id.AddBoolean(extInfo.getNoCallerSavedRegs());
    id.AddBoolean(extInfo.getHasRegParm());
    id.AddInteger(extInfo.getRegParm());
    id.AddBoolean(extInfo.getNoCfCheck());
    id.AddBoolean(extInfo.getCmseNSCall());
    id.AddInteger(required.getOpaqueData());
    resultType.Profile(id);
    for (const CanQualType &arg : argTypes)
      arg.Profile(id);
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void Profile(llvm::FoldingSetNodeID &id) {
    // If the Profile functions get out of sync, we can end up with incorrect
    // function signatures, so we call the static Profile function here rather
    // than duplicating the logic.
    Profile(id, instanceMethod, chainCall, delegateCall, required,
            getReturnType(), arguments(), extInfo);
  }

  llvm::ArrayRef<CanQualType> arguments() const {
    return llvm::ArrayRef<CanQualType>(argTypesBegin(), numArgs);
  }

  llvm::ArrayRef<CanQualType> requiredArguments() const {
    return llvm::ArrayRef<CanQualType>(argTypesBegin(), getNumRequiredArgs());
  }

  CanQualType getReturnType() const { return getArgTypes()[0]; }

  const_arg_iterator argTypesBegin() const { return getArgTypes() + 1; }
  const_arg_iterator argTypesEnd() const { return getArgTypes() + 1 + numArgs; }
  arg_iterator argTypesBegin() { return getArgTypes() + 1; }
  arg_iterator argTypesEnd() { return getArgTypes() + 1 + numArgs; }

  unsigned argTypeSize() const { return numArgs; }

  llvm::MutableArrayRef<CanQualType> argTypes() {
    return llvm::MutableArrayRef<CanQualType>(argTypesBegin(), numArgs);
  }
  llvm::ArrayRef<CanQualType> argTypes() const {
    return llvm::ArrayRef<CanQualType>(argTypesBegin(), numArgs);
  }

  bool isVariadic() const { return required.allowsOptionalArgs(); }
  RequiredArgs getRequiredArgs() const { return required; }
  unsigned getNumRequiredArgs() const {
    return isVariadic() ? getRequiredArgs().getNumRequiredArgs()
                        : argTypeSize();
  }
};

} // namespace clang::CIRGen

#endif
