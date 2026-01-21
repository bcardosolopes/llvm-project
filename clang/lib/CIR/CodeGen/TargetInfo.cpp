#include "TargetInfo.h"
#include "ABIInfo.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "clang/AST/Decl.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"

using namespace clang;
using namespace clang::CIRGen;

bool clang::CIRGen::isEmptyRecordForLayout(const ASTContext &context,
                                           QualType t) {
  const auto *rd = t->getAsRecordDecl();
  if (!rd)
    return false;

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *cxxrd = dyn_cast<CXXRecordDecl>(rd)) {
    if (cxxrd->isDynamicClass())
      return false;

    for (const auto &i : cxxrd->bases())
      if (!isEmptyRecordForLayout(context, i.getType()))
        return false;
  }

  for (const auto *i : rd->fields())
    if (!isEmptyFieldForLayout(context, i))
      return false;

  return true;
}

bool clang::CIRGen::isEmptyFieldForLayout(const ASTContext &context,
                                          const FieldDecl *fd) {
  if (fd->isZeroLengthBitField())
    return true;

  if (fd->isUnnamedBitField())
    return false;

  return isEmptyRecordForLayout(context, fd->getType());
}

namespace {

class X8664ABIInfo : public ABIInfo {
public:
  X8664ABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class X8664TargetCIRGenInfo : public TargetCIRGenInfo {
public:
  X8664TargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<X8664ABIInfo>(cgt)) {}
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createX8664TargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<X8664TargetCIRGenInfo>(cgt);
}

//===----------------------------------------------------------------------===//
// NVPTX ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class NVPTXABIInfo : public ABIInfo {
public:
  NVPTXABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class NVPTXTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  NVPTXTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<NVPTXABIInfo>(cgt)) {}

  void setCUDAKernelCallingConvention(const FunctionType *&ft) const override {
    ft = getABIInfo().cgt.getASTContext().adjustFunctionType(
        ft, ft->getExtInfo().withCallingConv(CC_DeviceKernel));
  }

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    if (const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl)) {
      cir::FuncOp func = mlir::dyn_cast<cir::FuncOp>(global);
      if (!func || func.isDeclaration())
        return;
      if (cgm.getLangOpts().CUDA) {
        if (fd->hasAttr<CUDAGlobalAttr>())
          func.setCallingConv(cir::CallingConv::PTXKernel);
      }
    }
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createNVPTXTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<NVPTXTargetCIRGenInfo>(cgt);
}

//===----------------------------------------------------------------------===//
// AMDGPU ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AMDGPUABIInfo : public ABIInfo {
public:
  AMDGPUABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class AMDGPUTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  AMDGPUTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<AMDGPUABIInfo>(cgt)) {}

  void setCUDAKernelCallingConvention(const FunctionType *&ft) const override {
    ft = getABIInfo().cgt.getASTContext().adjustFunctionType(
        ft, ft->getExtInfo().withCallingConv(CC_DeviceKernel));
  }

  mlir::ptr::MemorySpaceAttrInterface
  getCIRAllocaAddressSpace() const override {
    return cir::LangAddressSpaceAttr::get(
        &getABIInfo().cgt.getMLIRContext(),
        cir::LangAddressSpace::OffloadPrivate);
  }

  LangAS getGlobalVarAddressSpace(CIRGenModule &cgm,
                                  const VarDecl *d) const override {
    assert(!cgm.getLangOpts().OpenCL &&
           !(cgm.getLangOpts().CUDA && cgm.getLangOpts().CUDAIsDevice) &&
           "Address space agnostic languages only");
    LangAS defaultGlobalAS = getLangASFromTargetAS(
        cgm.getASTContext().getTargetAddressSpace(LangAS::opencl_global));
    if (!d)
      return defaultGlobalAS;
    LangAS addrSpace = d->getType().getAddressSpace();
    if (addrSpace != LangAS::Default)
      return addrSpace;
    if (d->getType().isConstantStorage(cgm.getASTContext(), false, false) &&
        d->hasConstantInitialization()) {
      if (auto constAS = cgm.getTarget().getConstantAddressSpace())
        return *constAS;
    }
    return defaultGlobalAS;
  }

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    if (const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl)) {
      cir::FuncOp func = mlir::dyn_cast<cir::FuncOp>(global);
      if (!func || func.isDeclaration())
        return;
      if (cgm.getLangOpts().CUDA || cgm.getLangOpts().HIP) {
        if (fd->hasAttr<CUDAGlobalAttr>())
          func.setCallingConv(cir::CallingConv::AMDGPUKernel);
      }
      if (cgm.getLangOpts().OpenCL) {
        if (fd->hasAttr<DeviceKernelAttr>())
          func.setCallingConv(cir::CallingConv::AMDGPUKernel);
      }

      // AMDGPU kernels with hidden visibility should use protected visibility
      // instead, so they can be referenced by the host.
      if (func.getGlobalVisibility() == cir::VisibilityKind::Hidden) {
        if (decl->hasAttr<CUDAGlobalAttr>() ||
            decl->hasAttr<DeviceKernelAttr>())
          func.setGlobalVisibility(cir::VisibilityKind::Protected);
      }
    }
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createAMDGPUTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<AMDGPUTargetCIRGenInfo>(cgt);
}

ABIInfo::~ABIInfo() noexcept = default;

bool TargetCIRGenInfo::isNoProtoCallVariadic(
    const FunctionNoProtoType *fnType) const {
  // The following conventions are known to require this to be false:
  //   x86_stdcall
  //   MIPS
  // For everything else, we just prefer false unless we opt out.
  return false;
}

mlir::Value TargetCIRGenInfo::performAddrSpaceCast(
    CIRGenFunction &cgf, mlir::Value v,
    mlir::ptr::MemorySpaceAttrInterface srcAddr, mlir::Type destTy,
    bool isNonNull) const {
  // Since target may map different address spaces in AST to the same address
  // space, an address space conversion may end up as a bitcast.
  if (cir::GlobalOp globalOp = v.getDefiningOp<cir::GlobalOp>())
    cgf.cgm.errorNYI("Global op addrspace cast");
  // Try to preserve the source's name to make IR more readable.
  return cgf.getBuilder().createAddrSpaceCast(v, destTy);
}

namespace {

class SPIRVABIInfo : public ABIInfo {
public:
  SPIRVABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class SPIRVTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  SPIRVTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<SPIRVABIInfo>(cgt)) {}

  mlir::ptr::MemorySpaceAttrInterface
  getCIRAllocaAddressSpace() const override {
    // In OpenCL/SPIR-V, alloca is in the private address space.
    return cir::LangAddressSpaceAttr::get(
        &getABIInfo().cgt.getMLIRContext(),
        cir::LangAddressSpace::OffloadPrivate);
  }

  cir::CallingConv getOpenCLKernelCallingConv() const override {
    return cir::CallingConv::SpirKernel;
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createSPIRVTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<SPIRVTargetCIRGenInfo>(cgt);
}

LangAS TargetCIRGenInfo::getGlobalVarAddressSpace(CIRGenModule &cgm,
                                                  const VarDecl *d) const {
  assert(!cgm.getLangOpts().OpenCL &&
         !(cgm.getLangOpts().CUDA && cgm.getLangOpts().CUDAIsDevice) &&
         "Address space agnostic languages only");
  return d ? d->getType().getAddressSpace() : LangAS::Default;
}
