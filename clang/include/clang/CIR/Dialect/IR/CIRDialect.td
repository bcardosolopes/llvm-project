//===- CIRDialect.td - CIR dialect -------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CIR dialect.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_IR_CIRDIALECT_TD
#define CLANG_CIR_DIALECT_IR_CIRDIALECT_TD

include "mlir/IR/OpBase.td"

def CIR_Dialect : Dialect {
  let name = "cir";

  // A short one-line summary of our dialect.
  let summary = "A high-level dialect for analyzing and optimizing Clang "
                "supported languages";

  let cppNamespace = "::cir";

  let useDefaultAttributePrinterParser = 0;

  // Enable constant materialization for the CIR dialect. This generates a
  // declaration for the cir::CIRDialect::materializeConstant function. This
  // hook is necessary for canonicalization to properly handle attributes
  // returned by fold methods, allowing them to be materialized as constant
  // operations in the IR.
  let hasConstantMaterializer = 1;

  // Checks the discardable cir.* attributes that have a fixed meaning; see
  // CIRDialect::verifyOperationAttribute.
  let hasOperationAttrVerify = 1;

  let extraClassDeclaration = [{
    static llvm::StringRef getSourceLanguageAttrName() { return "cir.lang"; }
    static llvm::StringRef getTripleAttrName() { return "cir.triple"; }
    static llvm::StringRef getTargetABIAttrName() { return "cir.target_abi"; }
    static llvm::StringRef getOptInfoAttrName() { return "cir.opt_info"; }
    /// Record name -> what C++ decides about passing it (see RecordABIFacts in
    /// TargetLowering), for a module serialized without its clang AST.
    static llvm::StringRef getRecordABIFactsAttrName() { return "cir.record_abi"; }
    /// Record name -> where it is defined, then where each of its members is,
    /// for a module serialized without its clang AST: what a link that finds
    /// two definitions of one record points at.
    static llvm::StringRef getRecordLocationsAttrName() {
      return "cir.record_locations";
    }
    /// A C++ module built without exceptions, whose inline functions may have
    /// bodies that differ from the same functions built with them.
    static llvm::StringRef getNoCXXExceptionsAttrName() {
      return "cir.no_cxx_exceptions";
    }
    /// A C++ module built without RTTI data, whose linkonce vtables and
    /// libc++ internals leave out type_info pointers other TUs' copies have.
    static llvm::StringRef getNoRTTIAttrName() { return "cir.no_rtti"; }
    /// How far the module is through the CIR pipeline; see CIRStage.h.
    static llvm::StringRef getStageAttrName() { return "cir.stage"; }
    static llvm::StringRef getSOBAttrName() { return "cir.sob"; }
    static llvm::StringRef getNewStructPathTBAAAttrName() {
      return "cir.new_struct_path_tbaa";
    }
    static llvm::StringRef getTypeSizeInfoAttrName() { return "cir.type_size_info"; }
    static llvm::StringRef getCalleeAttrName() { return "callee"; }
    static llvm::StringRef getNoThrowAttrName() { return "nothrow"; }
    static llvm::StringRef getSideEffectAttrName() { return "side_effect"; }
    static llvm::StringRef getModuleLevelAsmAttrName() { return "cir.module_asm"; }
    static llvm::StringRef getGlobalCtorsAttrName() { return "cir.global_ctors"; }
    static llvm::StringRef getGlobalDtorsAttrName() { return "cir.global_dtors"; }
    static llvm::StringRef getGlobalAnnotationsAttrName() { return "cir.global_annotations"; }
    static llvm::StringRef getLLVMUsedAttrName() { return "cir.llvm_used"; }
    static llvm::StringRef getLLVMCompilerUsedAttrName() { return "cir.llvm_compiler_used"; }
    static llvm::StringRef getCUDABinaryHandleAttrName() { return "cir.cu.binary_handle"; }
    static llvm::StringRef getUWTableAttrName() { return "cir.uwtable"; }
    static llvm::StringRef getOperandSegmentSizesAttrName() { return "operandSegmentSizes"; }

    void registerAttributes();
    void registerTypes();

    mlir::Type parseType(mlir::DialectAsmParser &parser) const override;
    void printType(mlir::Type type,
                   mlir::DialectAsmPrinter &printer) const override;

    mlir::Attribute parseAttribute(mlir::DialectAsmParser &parser,
                                   mlir::Type type) const override;
    void printAttribute(mlir::Attribute attr,
                        mlir::DialectAsmPrinter &os) const override;
  }];
}

#endif // CLANG_CIR_DIALECT_IR_CIRDIALECT_TD
