//===---- TargetInfo.h - Encapsulate target details -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function definition used
// to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_TARGETINFO_H
#define LLVM_CLANG_LIB_CIR_TARGETINFO_H

#include "ABIInfo.h"
#include "CIRGenTypes.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"

#include <optional>
#include <memory>
#include <utility>

namespace clang {
class FunctionDecl;
class TargetInfo;
class VarDecl;
} // namespace clang

namespace clang::CIRGen {

/// isEmptyFieldForLayout - Return true if the field is "empty", that is,
/// either a zero-width bit-field or an isEmptyRecordForLayout.
bool isEmptyFieldForLayout(const ASTContext &context, const FieldDecl *fd);

/// isEmptyRecordForLayout - Return true if a structure contains only empty
/// base classes (per  isEmptyRecordForLayout) and fields (per
/// isEmptyFieldForLayout). Note, C++ record fields are considered empty
/// if the [[no_unique_address]] attribute would have made them empty.
bool isEmptyRecordForLayout(const ASTContext &context, QualType t);

/// Return true when OGCG's AArch64 va_arg lowering ignores \p t entirely
/// instead of touching the va_list.
bool shouldIgnoreAArch64VAArg(const ASTContext &context,
                              const TargetInfo &targetInfo, QualType t);

class CIRGenFunction;
class CIRGenModule;
class CallArgList;

class TargetCIRGenInfo {
  std::unique_ptr<ABIInfo> info;

public:
  TargetCIRGenInfo(std::unique_ptr<ABIInfo> info) : info(std::move(info)) {}

  virtual ~TargetCIRGenInfo() = default;

  /// Returns ABI info helper for the target.
  const ABIInfo &getABIInfo() const { return *info; }

  /// Get the address space for alloca.
  virtual mlir::ptr::MemorySpaceAttrInterface getCIRAllocaAddressSpace() const {
    return {};
  }
  /// Perform address space cast of an expression of pointer type.
  /// \param V is the value to be casted to another address space.
  /// \param DestTy is the destination pointer type.
  /// \param srcAS is theaddress space of \p V.
  /// \param IsNonNull is the flag indicating \p V is known to be non null.
  virtual mlir::Value
  performAddrSpaceCast(CIRGenFunction &cgf, mlir::Value v,
                       mlir::ptr::MemorySpaceAttrInterface srcAddr,
                       mlir::Type destTy, bool isNonNull = false) const;

  /// Determine whether a call to an unprototyped functions under
  /// the given calling convention should use the variadic
  /// convention or the non-variadic convention.
  ///
  /// There's a good reason to make a platform's variadic calling
  /// convention be different from its non-variadic calling
  /// convention: the non-variadic arguments can be passed in
  /// registers (better for performance), and the variadic arguments
  /// can be passed on the stack (also better for performance).  If
  /// this is done, however, unprototyped functions *must* use the
  /// non-variadic convention, because C99 states that a call
  /// through an unprototyped function type must succeed if the
  /// function was defined with a non-variadic prototype with
  /// compatible parameters.  Therefore, splitting the conventions
  /// makes it impossible to call a variadic function through an
  /// unprototyped type.  Since function prototypes came out in the
  /// late 1970s, this is probably an acceptable trade-off.
  /// Nonetheless, not all platforms are willing to make it, and in
  /// particularly x86-64 bends over backwards to make the
  /// conventions compatible.
  ///
  /// The default is false.  This is correct whenever:
  ///   - the conventions are exactly the same, because it does not
  ///     matter and the resulting IR will be somewhat prettier in
  ///     certain cases; or
  ///   - the conventions are substantively different in how they pass
  ///     arguments, because in this case using the variadic convention
  ///     will lead to C99 violations.
  ///
  /// However, some platforms make the conventions identical except
  /// for passing additional out-of-band information to a variadic
  /// function: for example, x86-64 passes the number of SSE
  /// arguments in %al.  On these platforms, it is desirable to
  /// call unprototyped functions using the variadic convention so
  /// that unprototyped calls to varargs functions still succeed.
  ///
  /// Relatedly, platforms which pass the fixed arguments to this:
  ///   A foo(B, C, D);
  /// differently than they would pass them to this:
  ///   A foo(B, C, D, ...);
  /// may need to adjust the debugger-support code in Sema to do the
  /// right thing when calling a function with no know signature.
  virtual bool isNoProtoCallVariadic(const FunctionNoProtoType *fnType) const;

  virtual bool isScalarizableAsmOperand(CIRGenFunction &cgf,
                                        mlir::Type ty) const {
    return false;
  }

  /// Corrects the MLIR type for a given constraint and "usual"
  /// type.
  ///
  /// \returns A new MLIR type, possibly the same as the original
  /// on success
  virtual mlir::Type adjustInlineAsmType(CIRGenFunction &cgf,
                                         llvm::StringRef constraint,
                                         mlir::Type ty) const {
    return ty;
  }

  virtual void setCUDAKernelCallingConvention(const FunctionType *&ft) const {}

  /// Return the device-side type for the CUDA device builtin surface type.
  virtual mlir::Type getCUDADeviceBuiltinSurfaceDeviceType() const {
    // By default, no change from the original one.
    return {};
  }

  /// Return the device-side type for the CUDA device builtin texture type.
  virtual mlir::Type getCUDADeviceBuiltinTextureDeviceType() const {
    // By default, no change from the original one.
    return {};
  }

  /// Return the calling convention used for synthesized runtime helpers.
  /// Mirrors OGCG's RuntimeCC handling.
  virtual cir::CallingConv
  getRuntimeCallingConv(CIRGenModule &cgm) const {
    return cir::CallingConv::C;
  }

  virtual cir::CallingConv getOpenCLKernelCallingConv() const {
    // Device kernels use SPIR_KERNEL by default (matching OGCG), ensuring each
    // kernel argument gets one matching argument in the produced function to
    // enable feasible implementation of clSetKernelArg().
    return cir::CallingConv::OpenCLKernel;
  }

  /// Adjust the calling convention of an OpenCL kernel stub function type.
  /// The default uses CC_C; targets like SPIR override to CC_SpirFunction.
  virtual void
  setOCLKernelStubCallingConvention(const FunctionType *&ft) const {
    ft = getABIInfo().cgt.getASTContext().adjustFunctionType(
        ft, ft->getExtInfo().withCallingConv(CC_C));
  }

  virtual void setTargetAttributes(const clang::Decl *decl,
                                   mlir::Operation *global,
                                   CIRGenModule &cgm) const {}

  /// Retrieve the target-specific inline-assembly marker used by ARC
  /// retainAutoreleasedReturnValue optimizations. Mirrors OGCG's
  /// TargetCodeGenInfo::getARCRetainAutoreleasedReturnValueMarker.
  virtual llvm::StringRef
  getARCRetainAutoreleasedReturnValueMarker() const {
    return "";
  }

  /// Return the UBSan function-signature magic used for -fsanitize=function
  /// prologues, or std::nullopt when the target does not support them.
  virtual std::optional<uint32_t> getUBSanFunctionSignature() const {
    return 0xc105cafeu;
  }

  /// Return true if the target's indirect return slot consumes an argument
  /// position, matching OGCG's ReturnSlotInterferesWithArgs logic.
  virtual bool doesReturnSlotInterfereWithArgs() const { return true; }

  /// Diagnose target-specific ABI issues for a function definition. Mirrors
  /// OGCG's TargetCodeGenInfo::checkFunctionABI hook.
  virtual void checkFunctionABI(CIRGenModule &CGM,
                                const FunctionDecl *FD) const {}

  /// Diagnose target-specific ABI issues at a call site. Mirrors OGCG's
  /// TargetCodeGenInfo::checkFunctionCallABI hook.
  virtual void checkFunctionCallABI(CIRGenModule &CGM,
                                    SourceLocation CallSiteLoc,
                                    const FunctionDecl *Caller,
                                    const FunctionDecl *Callee,
                                    const CallArgList &Args,
                                    QualType ReturnType) const {}

  /// Return true if forcing this call to inline would violate the target ABI.
  /// Mirrors OGCG's TargetCodeGenInfo::wouldInliningViolateFunctionCallABI and
  /// lets CIR avoid stamping call-site alwaysinline for target-incompatible
  /// flatten / [[clang::always_inline]] cases.
  virtual bool
  wouldInliningViolateFunctionCallABI(const FunctionDecl *Caller,
                                      const FunctionDecl *Callee) const {
    return false;
  }

  /// Determines the address space for a global variable.
  virtual LangAS getGlobalVarAddressSpace(CIRGenModule &cgm,
                                          const VarDecl *d) const;
};

std::unique_ptr<TargetCIRGenInfo> createX8632TargetCIRGenInfo(CIRGenTypes &cgt);
std::unique_ptr<TargetCIRGenInfo> createX8664TargetCIRGenInfo(CIRGenTypes &cgt);
std::unique_ptr<TargetCIRGenInfo> createARMTargetCIRGenInfo(CIRGenTypes &cgt);
std::unique_ptr<TargetCIRGenInfo>
createAArch64TargetCIRGenInfo(CIRGenTypes &cgt);
std::unique_ptr<TargetCIRGenInfo> createNVPTXTargetCIRGenInfo(CIRGenTypes &cgt);
std::unique_ptr<TargetCIRGenInfo>
createAMDGPUTargetCIRGenInfo(CIRGenTypes &cgt);
std::unique_ptr<TargetCIRGenInfo> createSPIRVTargetCIRGenInfo(CIRGenTypes &cgt);

} // namespace clang::CIRGen

#endif // LLVM_CLANG_LIB_CIR_TARGETINFO_H
