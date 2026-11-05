#ifndef LLVM_INTRINSIC_OPS
#define LLVM_INTRINSIC_OPS

include "mlir/IR/OpBase.td"
include "mlir/Dialect/LLVMIR/LLVMAttrDefs.td"
include "mlir/Dialect/LLVMIR/LLVMEnums.td"
include "mlir/Dialect/LLVMIR/LLVMOpBase.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/Interfaces/MemorySlotInterfaces.td"

// Operations that correspond to LLVM intrinsics. With MLIR operation set being
// extendable, there is no reason to introduce a hard boundary between "core"
// operations and intrinsics. However, we systematically prefix them with
// "intr." to avoid potential name clashes.

class LLVM_UnaryIntrOpBase<string func, Type element,
                           list<Trait> traits = [], bit requiresFastmath = 0> :
    LLVM_OneResultIntrOp<func, [], [0],
           !listconcat([Pure, SameOperandsAndResultType], traits),
           requiresFastmath> {
  dag commonArgs = (ins LLVM_ScalarOrVectorOf<element>:$in);
  let assemblyFormat = "`(` operands `)` attr-dict `:` "
      "functional-type(operands, results)";
}

class LLVM_UnaryIntrOpI<string func, list<Trait> traits = []> :
    LLVM_UnaryIntrOpBase<func, AnySignlessInteger, traits> {
  let arguments = commonArgs;
}

class LLVM_UnaryIntrOpF<string func, list<Trait> traits = []> :
    LLVM_UnaryIntrOpBase<func, LLVM_AnyFloat, traits, /*requiresFastmath=*/1> {
  dag fmfArg = (
    ins DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
  let arguments = !con(commonArgs, fmfArg);
}

class LLVM_BinarySameArgsIntrOpBase<string func, Type element,
              list<Trait> traits = [], bit requiresFastmath = 0> :
    LLVM_OneResultIntrOp<func, [], [0],
           !listconcat([Pure, SameOperandsAndResultType], traits),
           requiresFastmath> {
  dag commonArgs = (ins LLVM_ScalarOrVectorOf<element>:$a,
                        LLVM_ScalarOrVectorOf<element>:$b);
  let assemblyFormat = "`(` operands `)` attr-dict `:` "
      "functional-type(operands, results)";
}

class LLVM_BinarySameArgsIntrOpI<string func, list<Trait> traits = []> :
    LLVM_BinarySameArgsIntrOpBase<func, AnySignlessInteger, traits> {
  let arguments = commonArgs;
}

class LLVM_BinarySameArgsIntrOpF<string func, list<Trait> traits = []> :
    LLVM_BinarySameArgsIntrOpBase<func, LLVM_AnyFloat, traits,
                                  /*requiresFastmath=*/1> {
  dag fmfArg = (
    ins DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
  let arguments = !con(commonArgs, fmfArg);
}

class LLVM_TernarySameArgsIntrOpBase<string func, Type element,
              list<Trait> traits = [], bit requiresFastmath = 0> :
    LLVM_OneResultIntrOp<func, [], [0],
           !listconcat([Pure, SameOperandsAndResultType], traits),
           requiresFastmath> {
  dag commonArgs = (ins LLVM_ScalarOrVectorOf<element>:$a,
                       LLVM_ScalarOrVectorOf<element>:$b,
                       LLVM_ScalarOrVectorOf<element>:$c);
  let assemblyFormat = "`(` operands `)` attr-dict `:` "
      "functional-type(operands, results)";
}

class LLVM_TernarySameArgsIntrOpI<string func, list<Trait> traits = []> :
    LLVM_TernarySameArgsIntrOpBase<func, AnySignlessInteger, traits> {
  let arguments = commonArgs;
}

class LLVM_TernarySameArgsIntrOpF<string func, list<Trait> traits = []> :
    LLVM_TernarySameArgsIntrOpBase<func, LLVM_AnyFloat, traits,
                                  /*requiresFastmath=*/1> {
  dag fmfArg = (
    ins DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
  let arguments = !con(commonArgs, fmfArg);
}

class LLVM_CountZerosIntrOp<string func, list<Trait> traits = []> :
    LLVM_OneResultIntrOp<func, [], [0],
           !listconcat([Pure, SameOperandsAndResultType], traits),
            /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
            /*immArgPositions=*/[1], /*immArgAttrNames=*/["is_zero_poison"]> {
  let arguments = (ins LLVM_ScalarOrVectorOf<AnySignlessInteger>:$in,
                   I1Attr:$is_zero_poison);
}

def LLVM_AbsOp : LLVM_OneResultIntrOp<"abs", [], [0], [Pure],
    /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
    /*immArgPositions=*/[1], /*immArgAttrNames=*/["is_int_min_poison"]> {
  let arguments = (ins LLVM_ScalarOrVectorOf<AnySignlessInteger>:$in,
                   I1Attr:$is_int_min_poison);
}

def LLVM_IsFPClass : LLVM_OneResultIntrOp<"is.fpclass", [], [0], [Pure],
  /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
  /*immArgPositions=*/[1], /*immArgAttrNames=*/["bit"]> {
  let arguments = (ins LLVM_ScalarOrVectorOf<LLVM_AnyFloat>:$in, I32Attr:$bit);
}

class LLVM_PowFI<string func> :
    LLVM_OneResultIntrOp<func, [], [0,1],
        [Pure], /*requiresFastmath=*/1> {
    let arguments =
        (ins LLVM_ScalarOrVectorOf<LLVM_AnyFloat>:$val,
            AnySignlessInteger:$power,
            DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
    let assemblyFormat = "`(` operands `)` attr-dict `:` "
            "functional-type(operands, results)";
}

def LLVM_CopySignOp : LLVM_BinarySameArgsIntrOpF<"copysign">;
def LLVM_ExpOp : LLVM_UnaryIntrOpF<"exp">;
def LLVM_Exp2Op : LLVM_UnaryIntrOpF<"exp2">;
def LLVM_Exp10Op : LLVM_UnaryIntrOpF<"exp10">;
def LLVM_LoadExpOp : LLVM_PowFI<"ldexp">;
def LLVM_FractionExpOp : LLVM_TwoResultIntrOp<"frexp", [0, 1], [],
    [Pure], /*requiresFastmath=*/1> {
    let arguments =
        (ins LLVM_ScalarOrVectorOf<LLVM_AnyFloat>:$val,
        DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
    let assemblyFormat = "`(` operands `)` attr-dict `:` "
        "functional-type(operands, results)";
}
def LLVM_FAbsOp : LLVM_UnaryIntrOpF<"fabs">;
def LLVM_FCeilOp : LLVM_UnaryIntrOpF<"ceil">;
def LLVM_FFloorOp : LLVM_UnaryIntrOpF<"floor">;
def LLVM_FMAOp : LLVM_TernarySameArgsIntrOpF<"fma">;
def LLVM_FMulAddOp : LLVM_TernarySameArgsIntrOpF<"fmuladd">;
def LLVM_Log10Op : LLVM_UnaryIntrOpF<"log10">;
def LLVM_Log2Op : LLVM_UnaryIntrOpF<"log2">;
def LLVM_LogOp : LLVM_UnaryIntrOpF<"log">;
def LLVM_Prefetch : LLVM_ZeroResultIntrOp<"prefetch", [0],
  /*traits=*/[], /*requiresAccessGroup=*/0, /*requiresAliasAnalysis=*/1,
  /*requiresArgAndResultAttrs=*/0, /*requiresOpBundles=*/0,
  /*immArgPositions=*/[1, 2, 3], /*immArgAttrNames=*/["rw", "hint", "cache"]
> {
  let arguments = !con((ins LLVM_AnyPointer:$addr, I32Attr:$rw, I32Attr:$hint,
                           I32Attr:$cache), baseArgs);
}
def LLVM_RoundEvenOp : LLVM_UnaryIntrOpF<"roundeven">;
def LLVM_RoundOp : LLVM_UnaryIntrOpF<"round">;
def LLVM_FTruncOp : LLVM_UnaryIntrOpF<"trunc">;
def LLVM_SqrtOp : LLVM_UnaryIntrOpF<"sqrt">;
def LLVM_PowOp : LLVM_BinarySameArgsIntrOpF<"pow">;
def LLVM_PowIOp : LLVM_PowFI<"powi">;
def LLVM_RintOp : LLVM_UnaryIntrOpF<"rint">;
def LLVM_NearbyintOp : LLVM_UnaryIntrOpF<"nearbyint">;
class LLVM_IntRoundIntrOpBase<string func, Type element = LLVM_AnyFloat> :
        LLVM_OneResultIntrOp<func, [0], [0], [Pure]> {
  let arguments = (ins element:$val);
  let assemblyFormat = "`(` operands `)` attr-dict `:` "
      "functional-type(operands, results)";
}
class LLVM_ScalarOrVectorIntRoundIntrOpBase<string func> :
  LLVM_IntRoundIntrOpBase<func, LLVM_ScalarOrVectorOf<LLVM_AnyFloat>>;
def LLVM_LroundOp : LLVM_ScalarOrVectorIntRoundIntrOpBase<"lround">;
def LLVM_LlroundOp : LLVM_IntRoundIntrOpBase<"llround">;
def LLVM_LrintOp : LLVM_ScalarOrVectorIntRoundIntrOpBase<"lrint">;
def LLVM_LlrintOp : LLVM_ScalarOrVectorIntRoundIntrOpBase<"llrint">;
def LLVM_BitReverseOp : LLVM_UnaryIntrOpI<"bitreverse">;
def LLVM_ByteSwapOp : LLVM_UnaryIntrOpI<"bswap">;
def LLVM_CountLeadingZerosOp : LLVM_CountZerosIntrOp<"ctlz">;
def LLVM_CountTrailingZerosOp : LLVM_CountZerosIntrOp<"cttz">;
def LLVM_CtPopOp : LLVM_UnaryIntrOpI<"ctpop">;
def LLVM_FshlOp : LLVM_TernarySameArgsIntrOpI<"fshl">;
def LLVM_FshrOp : LLVM_TernarySameArgsIntrOpI<"fshr">;
def LLVM_MaxNumOp : LLVM_BinarySameArgsIntrOpF<"maxnum">;
def LLVM_MinNumOp : LLVM_BinarySameArgsIntrOpF<"minnum">;
def LLVM_MaximumOp : LLVM_BinarySameArgsIntrOpF<"maximum">;
def LLVM_MinimumOp : LLVM_BinarySameArgsIntrOpF<"minimum">;
def LLVM_SMaxOp : LLVM_BinarySameArgsIntrOpI<"smax">;
def LLVM_SMinOp : LLVM_BinarySameArgsIntrOpI<"smin">;
def LLVM_UMaxOp : LLVM_BinarySameArgsIntrOpI<"umax">;
def LLVM_UMinOp : LLVM_BinarySameArgsIntrOpI<"umin">;

class LLVM_CmpIntrOp<string func>
    : LLVM_OneResultIntrOp<func, [0], [0], [Pure, SameTypeOperands]> {
  let arguments = (ins LLVM_ScalarOrVectorOf<AnySignlessInteger>:$a,
      LLVM_ScalarOrVectorOf<AnySignlessInteger>:$b);
  let results = (outs LLVM_ScalarOrVectorOf<AnySignlessInteger>:$res);
  let assemblyFormat = "`(` operands `)` attr-dict `:` "
                       "functional-type(operands, results)";
}

def LLVM_SCmpOp : LLVM_CmpIntrOp<"scmp">;
def LLVM_UCmpOp : LLVM_CmpIntrOp<"ucmp">;

def LLVM_SinOp : LLVM_UnaryIntrOpF<"sin">;
def LLVM_CosOp : LLVM_UnaryIntrOpF<"cos">;
def LLVM_TanOp : LLVM_UnaryIntrOpF<"tan">;
def LLVM_SincosOp : LLVM_TwoResultIntrOp<"sincos", [], [0],
  [Pure], /*requiresFastmath=*/1> {
  let arguments =
      (ins LLVM_ScalarOrVectorOf<LLVM_AnyFloat>:$val,
      DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
  let assemblyFormat = "`(` operands `)` attr-dict `:` "
      "functional-type(operands, results)";
  let hasVerifier = 1;
}

def LLVM_ASinOp : LLVM_UnaryIntrOpF<"asin">;
def LLVM_ACosOp : LLVM_UnaryIntrOpF<"acos">;
def LLVM_ATanOp : LLVM_UnaryIntrOpF<"atan">;
def LLVM_ATan2Op : LLVM_BinarySameArgsIntrOpF<"atan2">;

def LLVM_SinhOp : LLVM_UnaryIntrOpF<"sinh">;
def LLVM_CoshOp : LLVM_UnaryIntrOpF<"cosh">;
def LLVM_TanhOp : LLVM_UnaryIntrOpF<"tanh">;

class LLVM_MemcpyIntrOpBase<string name> :
    LLVM_ZeroResultIntrOp<name, [0, 1, 2],
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
     DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>],
    /*requiresAccessGroup=*/1, /*requiresAliasAnalysis=*/1,
    /*requiresArgAndResultAttrs=*/1, /*requiresOpBundles=*/0,
    /*immArgPositions=*/[3], /*immArgAttrNames=*/["isVolatile"]> {
  dag args = (ins Arg<LLVM_AnyPointer,"",[MemWrite]>:$dst,
                  Arg<LLVM_AnyPointer,"",[MemRead]>:$src,
                  AnySignlessInteger:$len, I1Attr:$isVolatile,
                  // `!tbaa.struct`: which fields of the aggregate the copy
                  // touches, so it is not treated as one opaque access.
                  OptionalAttr<LLVM_TBAAStructFieldArrayAttr>:$tbaa_struct);
  // Append the arguments defined by LLVM_IntrOpBase.
  let arguments = !con(args, baseArgs);
  let builders = [
    OpBuilder<(ins "Value":$dst, "Value":$src, "Value":$len,
                   "bool":$isVolatile), [{
      build($_builder, $_state, dst, src, len,
            $_builder.getBoolAttr(isVolatile));
    }]>,
    OpBuilder<(ins "Value":$dst, "Value":$src, "Value":$len,
                   "IntegerAttr":$isVolatile), [{
      build($_builder, $_state, dst, src, len, isVolatile,
            /*tbaa_struct=*/nullptr,
            /*access_groups=*/nullptr, /*alias_scopes=*/nullptr,
            /*noalias_scopes=*/nullptr, /*tbaa=*/nullptr,
            /*annotations=*/nullptr,
            /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
    }]>
  ];

  // The generated builder writes the alias metadata; tbaa.struct is specific to
  // the aggregate-copy intrinsics, so it is appended here rather than in the
  // shared alias code, which also covers loads and stores.
  let llvmBuilder = baseLlvmBuilder # setAccessGroupsMetadataCode
      # setAliasAnalysisMetadataCode # [{
        moduleTranslation.setTBAAStructMetadata(op.getTbaaStructAttr(), inst);
      }] # baseLlvmBuilderArgAndResultAttrs # baseLlvmBuilderCoda;
}

def LLVM_MemcpyOp : LLVM_MemcpyIntrOpBase<"memcpy">;
def LLVM_MemmoveOp : LLVM_MemcpyIntrOpBase<"memmove">;

def LLVM_MemcpyInlineOp :
    LLVM_ZeroResultIntrOp<"memcpy.inline", [0, 1, 2],
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
     DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>],
    /*requiresAccessGroup=*/1, /*requiresAliasAnalysis=*/1,
    /*requiresArgAndResultAttrs=*/1, /*requiresOpBundles=*/0,
    /*immArgPositions=*/[2, 3], /*immArgAttrNames=*/["len", "isVolatile"]> {
  dag args = (ins Arg<LLVM_AnyPointer,"",[MemWrite]>:$dst,
                  Arg<LLVM_AnyPointer,"",[MemRead]>:$src,
                  APIntAttr:$len, I1Attr:$isVolatile,
                  OptionalAttr<LLVM_TBAAStructFieldArrayAttr>:$tbaa_struct);
  // Append the arguments defined by LLVM_IntrOpBase.
  let arguments = !con(args, baseArgs);
  let builders = [
    OpBuilder<(ins "Value":$dst, "Value":$src, "IntegerAttr":$len,
                   "bool":$isVolatile), [{
      build($_builder, $_state, dst, src, len,
            $_builder.getBoolAttr(isVolatile));
    }]>,
    OpBuilder<(ins "Value":$dst, "Value":$src, "IntegerAttr":$len,
                   "IntegerAttr":$isVolatile), [{
      build($_builder, $_state, dst, src, len, isVolatile,
            /*tbaa_struct=*/nullptr,
            /*access_groups=*/nullptr, /*alias_scopes=*/nullptr,
            /*noalias_scopes=*/nullptr, /*tbaa=*/nullptr,
            /*annotations=*/nullptr,
            /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
    }]>
  ];

  // Same as the memcpy base: tbaa.struct is specific to the aggregate copies.
  let llvmBuilder = baseLlvmBuilder # setAccessGroupsMetadataCode
      # setAliasAnalysisMetadataCode # [{
        moduleTranslation.setTBAAStructMetadata(op.getTbaaStructAttr(), inst);
      }] # baseLlvmBuilderArgAndResultAttrs # baseLlvmBuilderCoda;
}

def LLVM_MemsetOp : LLVM_ZeroResultIntrOp<"memset", [0, 2],
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
     DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>],
    /*requiresAccessGroup=*/1, /*requiresAliasAnalysis=*/1,
    /*requiresArgAndResultAttrs=*/1, /*requiresOpBundles=*/0,
    /*immArgPositions=*/[3], /*immArgAttrNames=*/["isVolatile"]> {
  dag args = (ins Arg<LLVM_AnyPointer,"",[MemWrite]>:$dst,
                  I8:$val, AnySignlessInteger:$len, I1Attr:$isVolatile);
  // Append the arguments defined by LLVM_IntrOpBase.
  let arguments = !con(args, baseArgs);
  let builders = [
    OpBuilder<(ins "Value":$dst, "Value":$val, "Value":$len,
                    "bool":$isVolatile), [{
      build($_builder, $_state, dst, val, len,
            $_builder.getBoolAttr(isVolatile));
    }]>,
    OpBuilder<(ins "Value":$dst, "Value":$val, "Value":$len,
                    "IntegerAttr":$isVolatile), [{
      build($_builder, $_state, dst, val, len, isVolatile,
            /*access_groups=*/nullptr, /*alias_scopes=*/nullptr,
            /*noalias_scopes=*/nullptr, /*tbaa=*/nullptr,
            /*annotations=*/nullptr,
            /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
    }]>
  ];
}

def LLVM_MemsetInlineOp : LLVM_ZeroResultIntrOp<"memset.inline", [0, 2],
    [DeclareOpInterfaceMethods<PromotableMemOpInterface>,
     DeclareOpInterfaceMethods<DestructurableAccessorOpInterface>,
     DeclareOpInterfaceMethods<SafeMemorySlotAccessOpInterface>],
    /*requiresAccessGroup=*/1, /*requiresAliasAnalysis=*/1,
    /*requiresArgAndResultAttrs=*/1, /*requiresOpBundles=*/0,
    /*immArgPositions=*/[2, 3], /*immArgAttrNames=*/["len", "isVolatile"]> {
  dag args = (ins Arg<LLVM_AnyPointer,"",[MemWrite]>:$dst,
                  I8:$val, APIntAttr:$len, I1Attr:$isVolatile);
  // Append the arguments defined by LLVM_IntrOpBase.
  let arguments = !con(args, baseArgs);
  let builders = [
    OpBuilder<(ins "Value":$dst, "Value":$val, "IntegerAttr":$len,
                    "bool":$isVolatile), [{
      build($_builder, $_state, dst, val, len,
            $_builder.getBoolAttr(isVolatile));
    }]>,
    OpBuilder<(ins "Value":$dst, "Value":$val, "IntegerAttr":$len,
                    "IntegerAttr":$isVolatile), [{
      build($_builder, $_state, dst, val, len, isVolatile,
            /*access_groups=*/nullptr, /*alias_scopes=*/nullptr,
            /*noalias_scopes=*/nullptr, /*tbaa=*/nullptr,
            /*annotations=*/nullptr,
            /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
    }]>
  ];
}

def LLVM_NoAliasScopeDeclOp
    : LLVM_ZeroResultIntrOp<"experimental.noalias.scope.decl", [], [],
                            /*requiresAccessGroup=*/0,
                            /*requiresAliasAnalysis=*/1> {
  // The scope this op declares is its operand; the alias attributes below are
  // the metadata attached to the *call*, which is separate -- a scope
  // declaration sitting inside another noalias region carries that region's
  // !noalias like any other instruction.
  let arguments = !con((ins LLVM_AliasScopeAttr:$scope), baseArgs);
  string llvmBuilder = [{
    // Wrap the scope argument into a list since the LLVM IR intrinsic takes
    // a list containing exactly one scope rather than a scope itself.
    llvm::MDNode* node = moduleTranslation.getOrCreateAliasScopes({$scope});
    llvm::Instruction *inst = builder.CreateNoAliasScopeDeclaration(node);
    moduleTranslation.setAliasScopeMetadata(op, inst);
    moduleTranslation.setTBAAMetadata(op, inst);
  }];
  string mlirBuilder = [{
    FailureOr<SmallVector<LLVM::AliasScopeAttr>> scopeAttrs =
      moduleImport.matchAliasScopeAttrs(llvmOperands[0]);
    // Drop the intrinsic if the alias scope translation fails since the scope
    // is not used by an aliasing operation, such as a load or store, that is
    // used to convert the alias scope metadata.
    if (failed(scopeAttrs))
      return success();
    if (scopeAttrs->size() != 1)
      return failure();
    $_op = LLVM::NoAliasScopeDeclOp::create(
      $_builder, $_location, (*scopeAttrs)[0], /*alias_scopes=*/nullptr,
      /*noalias_scopes=*/nullptr, /*tbaa=*/nullptr, /*annotations=*/nullptr);
  }];
  let assemblyFormat = "$scope attr-dict";
}

def LLVM_PtrMaskOp
  : LLVM_OneResultIntrOp<"ptrmask", [], [0, 1],
                          [Pure, AllTypesMatch<["ptr", "res"]>]> {
  let arguments = (ins LLVM_ScalarOrVectorOf<LLVM_AnyPointer>:$ptr,
                   LLVM_ScalarOrVectorOf<AnyInteger>:$mask);
  let results = (outs LLVM_ScalarOrVectorOf<LLVM_AnyPointer>:$res);

  let assemblyFormat = "$ptr `,` $mask attr-dict `:` functional-type(operands, results)";
}

//
// Memory marker intrinsics.
//

/// Base operation for lifetime markers.
class LLVM_LifetimeBaseOp<string opName> : LLVM_ZeroResultIntrOp<opName, [0],
    [DeclareOpInterfaceMethods<PromotableOpInterface>],
    /*requiresAccessGroup=*/0, /*requiresAliasAnalysis=*/1> {
  dag args = (ins LLVM_AnyPointer:$ptr);
  // Append the alias attributes defined by LLVM_IntrOpBase.
  let arguments = !con(args, baseArgs);
  // Keep the pre-alias-attribute spelling working for the many in-tree callers
  // that build a lifetime marker with just a pointer.
  let builders = [
    OpBuilder<(ins "Value":$ptr), [{
      // No access_groups: LLVM_ZeroResultIntrOp sets requiresAccessGroup=0,
      // so baseArgs carries only the alias-analysis attributes.
      build($_builder, $_state, ptr,
            /*alias_scopes=*/nullptr, /*noalias_scopes=*/nullptr,
            /*tbaa=*/nullptr, /*annotations=*/nullptr);
    }]>
  ];
  let assemblyFormat = "$ptr attr-dict `:` qualified(type($ptr))";
}

def LLVM_LifetimeStartOp : LLVM_LifetimeBaseOp<"lifetime.start">;
def LLVM_LifetimeEndOp : LLVM_LifetimeBaseOp<"lifetime.end">;

def LLVM_InvariantStartOp : LLVM_OneResultIntrOp<"invariant.start", [], [1],
    [DeclareOpInterfaceMethods<PromotableOpInterface>],
    /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
    /*immArgPositions=*/[0], /*immArgAttrNames=*/["size"],
    /*requiresAliasAnalysis=*/1> {
  dag args = (ins I64Attr:$size, LLVM_AnyPointer:$ptr);
  // Append the alias attributes defined by LLVM_IntrOpBase.
  let arguments = !con(args, baseArgs);
  let builders = [
    OpBuilder<(ins "Type":$res, "uint64_t":$size, "Value":$ptr), [{
      build($_builder, $_state, res, $_builder.getI64IntegerAttr(size), ptr,
            /*alias_scopes=*/nullptr, /*noalias_scopes=*/nullptr,
            /*tbaa=*/nullptr, /*annotations=*/nullptr);
    }]>
  ];
  let results = (outs LLVM_DefaultPointer:$res);
  let assemblyFormat = "$size `,` $ptr attr-dict `:` qualified(type($ptr))";
}

def LLVM_InvariantEndOp : LLVM_ZeroResultIntrOp<"invariant.end", [2],
    [DeclareOpInterfaceMethods<PromotableOpInterface>],
    /*requiresAccessGroup=*/0, /*requiresAliasAnalysis=*/0,
    /*requiresArgAndResultAttrs=*/0, /*requiresOpBundles=*/0,
    /*immArgPositions=*/[1], /*immArgAttrNames=*/["size"]> {
  let arguments = (ins LLVM_DefaultPointer:$start,
                       I64Attr:$size,
                       LLVM_AnyPointer:$ptr);
  let assemblyFormat = "$start `,` $size `,` $ptr attr-dict `:` "
      "qualified(type($ptr))";
}

def LLVM_LaunderInvariantGroupOp
    : LLVM_OneResultIntrOp<"launder.invariant.group", [], [0],
        [DeclareOpInterfaceMethods<PromotableOpInterface>,
         SameOperandsAndResultType]> {
  let arguments = (ins LLVM_AnyPointer:$ptr);
  let results = (outs LLVM_AnyPointer:$res);
  let assemblyFormat = "$ptr attr-dict `:` qualified(type($ptr))";
}

def LLVM_StripInvariantGroupOp
    : LLVM_OneResultIntrOp<"strip.invariant.group", [], [0],
        [DeclareOpInterfaceMethods<PromotableOpInterface>,
         SameOperandsAndResultType]> {
  let arguments = (ins LLVM_AnyPointer:$ptr);
  let results = (outs LLVM_AnyPointer:$res);
  let assemblyFormat = "$ptr attr-dict `:` qualified(type($ptr))";
}

// Constrained Floating-Point Intrinsics.

class LLVM_ConstrainedIntr<string mnem, int numArgs,
                           bit overloadedResult, list<int> overloadedOperands,
                           bit hasRoundingMode>
    : LLVM_OneResultIntrOp<"experimental.constrained." # mnem,
                           /*overloadedResults=*/
                           !cond(!gt(overloadedResult, 0) : [0],
                                 true : []),
                           overloadedOperands,
                           /*traits=*/[Pure, DeclareOpInterfaceMethods<FPExceptionBehaviorOpInterface>]
                           # !cond(
                               !gt(hasRoundingMode, 0) : [DeclareOpInterfaceMethods<RoundingModeOpInterface>],
                               true : []),
                           /*requiresFastmath=*/0,
                           /*requiresArgAndResultAttrs=*/0,
                           /*immArgPositions=*/[],
                           /*immArgAttrNames=*/[]> {
  dag regularArgs = !dag(ins, !listsplat(LLVM_Type, numArgs), !foreach(i, !range(numArgs), "arg_" #i));
  dag attrArgs = !con(!cond(!gt(hasRoundingMode, 0) : (ins ValidRoundingModeAttr:$roundingmode),
                            true : (ins)),
                      (ins FPExceptionBehaviorAttr:$fpExceptionBehavior));
  let arguments = !con(regularArgs, attrArgs);
  let llvmBuilder = [{
    SmallVector<llvm::Value *> args =
      moduleTranslation.lookupValues(opInst.getOperands());
    SmallVector<llvm::Type *> overloadedTypes; }] #
    !cond(!gt(overloadedResult, 0) : [{
    // Take into account overloaded result type.
    overloadedTypes.push_back($_resultType); }],
    // No overloaded result type.
          true : "") # [{
    llvm::transform(ArrayRef<unsigned>}] # overloadedOperandsCpp # [{,
                    std::back_inserter(overloadedTypes),
                    [&args](unsigned index) { return args[index]->getType(); });
    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::Function *callee =
      llvm::Intrinsic::getOrInsertDeclaration(module,
        llvm::Intrinsic::experimental_constrained_}] #
    mnem # [{, overloadedTypes); }] #
    !cond(!gt(hasRoundingMode, 0) : [{
    // Get rounding mode using interface.
    llvm::RoundingMode rounding =
        moduleTranslation.translateRoundingMode($roundingmode); }],
          true : [{
    // No rounding mode.
    std::optional<llvm::RoundingMode> rounding; }]) # [{
    llvm::fp::ExceptionBehavior except =
      moduleTranslation.translateFPExceptionBehavior($fpExceptionBehavior);
    $res = builder.CreateConstrainedFPCall(callee, args, "", rounding, except);
  }];
  let mlirBuilder = [{
    SmallVector<Value> mlirOperands;
    SmallVector<NamedAttribute> mlirAttrs;
    if (failed(moduleImport.convertIntrinsicArguments(
        llvmOperands.take_front( }] # numArgs # [{), {}, false,
        {}, {}, mlirOperands, mlirAttrs))) {
      return failure();
    }

    FPExceptionBehaviorAttr fpExceptionBehaviorAttr =
        $_fpExceptionBehavior_attr($fpExceptionBehavior);
    mlirAttrs.push_back(
        $_builder.getNamedAttr(
            $_qualCppClassName::getFPExceptionBehaviorAttrName(),
            fpExceptionBehaviorAttr)); }] #
    !cond(!gt(hasRoundingMode, 0) : [{
    RoundingModeAttr roundingModeAttr = $_roundingMode_attr($roundingmode);
    mlirAttrs.push_back(
        $_builder.getNamedAttr($_qualCppClassName::getRoundingModeAttrName(),
                               roundingModeAttr));
    }], true : "") # [{
    $res = $_qualCppClassName::create($_builder, $_location,
      $_resultType, mlirOperands, mlirAttrs);
  }];
}

def LLVM_ConstrainedUIToFP
    : LLVM_ConstrainedIntr<"uitofp", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/1> {
    let assemblyFormat = [{
        $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:` type($arg_0) `to` type(results)
    }];
}

def LLVM_ConstrainedSIToFP
    : LLVM_ConstrainedIntr<"sitofp", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/1> {
    let assemblyFormat = [{
        $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:` type($arg_0) `to` type(results)
    }];
}

def LLVM_ConstrainedFPTruncIntr
    : LLVM_ConstrainedIntr<"fptrunc", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:` type($arg_0) `to` type(results)
  }];
}

def LLVM_ConstrainedFPExtIntr
    : LLVM_ConstrainedIntr<"fpext", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:` type($arg_0) `to` type(results)
  }];
}

def LLVM_ConstrainedFPToSI
    : LLVM_ConstrainedIntr<"fptosi", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:` type($arg_0) `to` type(results)
  }];
}

def LLVM_ConstrainedFPToUI
    : LLVM_ConstrainedIntr<"fptoui", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:` type($arg_0) `to` type(results)
  }];
}

class LLVM_ConstrainedFPBinOpIntr<string mnem>
    : LLVM_ConstrainedIntr<mnem, /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $roundingmode $fpExceptionBehavior attr-dict `:`
      type($arg_0) `,` type($arg_1) `to` type(results)
  }];
}

def LLVM_ConstrainedFAddIntr : LLVM_ConstrainedFPBinOpIntr<"fadd">;
def LLVM_ConstrainedFSubIntr : LLVM_ConstrainedFPBinOpIntr<"fsub">;
def LLVM_ConstrainedFMulIntr : LLVM_ConstrainedFPBinOpIntr<"fmul">;
def LLVM_ConstrainedFDivIntr : LLVM_ConstrainedFPBinOpIntr<"fdiv">;
def LLVM_ConstrainedFRemIntr : LLVM_ConstrainedFPBinOpIntr<"frem">;

// The rest of the constrained intrinsics. Each is the plain operation plus the
// two things that make it constrained: the rounding mode it must assume and
// how it must treat exceptions. LLVM carries both as `MDString` operands,
// which is why they are attributes rather than operands here.
//
// `numArgs` counts only the real arguments and `overloadedOperands` names the
// ones whose type does not follow the result.
//
// `hasRoundingMode` is false where the result does not depend on the current
// rounding mode, and those take only the exception-behaviour operand. That is
// not the same as being exact: `maxnum` and `minimum` pick an operand,
// `ceil`, `floor`, `trunc`, `round` and `roundeven` name their rounding in the
// operation, `lround` and `llround` round half away from zero by definition,
// and `fptosi` and `fptoui` truncate by definition. Whether a new one needs a
// rounding mode is decided by that question, not by whether it is arithmetic.
//
// `overloadedResult` is 1 on every one of these, because every one of them
// mangles its result type into the intrinsic name. It exists for the case
// where the name is fixed and only the operands vary, which none of the
// constrained intrinsics are.

def LLVM_ConstrainedPow
    : LLVM_ConstrainedIntr<"pow", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedATan2
    : LLVM_ConstrainedIntr<"atan2", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedFMA
    : LLVM_ConstrainedIntr<"fma", /*numArgs=*/3,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 `,` $arg_2 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedFMulAdd
    : LLVM_ConstrainedIntr<"fmuladd", /*numArgs=*/3,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 `,` $arg_2 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedSqrt
    : LLVM_ConstrainedIntr<"sqrt", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedASin
    : LLVM_ConstrainedIntr<"asin", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedACos
    : LLVM_ConstrainedIntr<"acos", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedATan
    : LLVM_ConstrainedIntr<"atan", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedSin
    : LLVM_ConstrainedIntr<"sin", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedCos
    : LLVM_ConstrainedIntr<"cos", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedTan
    : LLVM_ConstrainedIntr<"tan", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedSinh
    : LLVM_ConstrainedIntr<"sinh", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedCosh
    : LLVM_ConstrainedIntr<"cosh", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedTanh
    : LLVM_ConstrainedIntr<"tanh", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLog
    : LLVM_ConstrainedIntr<"log", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLog10
    : LLVM_ConstrainedIntr<"log10", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLog2
    : LLVM_ConstrainedIntr<"log2", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedExp
    : LLVM_ConstrainedIntr<"exp", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedExp2
    : LLVM_ConstrainedIntr<"exp2", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedRint
    : LLVM_ConstrainedIntr<"rint", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedNearbyInt
    : LLVM_ConstrainedIntr<"nearbyint", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedPowI
    : LLVM_ConstrainedIntr<"powi", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLdExp
    : LLVM_ConstrainedIntr<"ldexp", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[1],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLRInt
    : LLVM_ConstrainedIntr<"lrint", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLLRInt
    : LLVM_ConstrainedIntr<"llrint", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/1> {
  let assemblyFormat = [{
    $arg_0 $roundingmode $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLRound
    : LLVM_ConstrainedIntr<"lround", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedLLRound
    : LLVM_ConstrainedIntr<"llround", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[0],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedMaxNum
    : LLVM_ConstrainedIntr<"maxnum", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedMinNum
    : LLVM_ConstrainedIntr<"minnum", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedMaximum
    : LLVM_ConstrainedIntr<"maximum", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedMinimum
    : LLVM_ConstrainedIntr<"minimum", /*numArgs=*/2,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 `,` $arg_1 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedCeil
    : LLVM_ConstrainedIntr<"ceil", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedFloor
    : LLVM_ConstrainedIntr<"floor", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedRound
    : LLVM_ConstrainedIntr<"round", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedRoundEven
    : LLVM_ConstrainedIntr<"roundeven", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

def LLVM_ConstrainedTrunc
    : LLVM_ConstrainedIntr<"trunc", /*numArgs=*/1,
        /*overloadedResult=*/1, /*overloadedOperands=*/[],
        /*hasRoundingMode=*/0> {
  let assemblyFormat = [{
    $arg_0 $fpExceptionBehavior attr-dict `:`
      functional-type(operands, results)
  }];
}

// Intrinsics with multiple returns.

class LLVM_ArithWithOverflowOp<string mnem>
    : LLVM_IntrOp<mnem, [0], [], [Pure, SameOperandsElementType], 2>,
      Arguments<(ins LLVM_ScalarOrVectorOf<AnySignlessInteger>,
                 LLVM_ScalarOrVectorOf<AnySignlessInteger>)>;

def LLVM_SAddWithOverflowOp : LLVM_ArithWithOverflowOp<"sadd.with.overflow">;
def LLVM_UAddWithOverflowOp : LLVM_ArithWithOverflowOp<"uadd.with.overflow">;
def LLVM_SSubWithOverflowOp : LLVM_ArithWithOverflowOp<"ssub.with.overflow">;
def LLVM_USubWithOverflowOp : LLVM_ArithWithOverflowOp<"usub.with.overflow">;
def LLVM_SMulWithOverflowOp : LLVM_ArithWithOverflowOp<"smul.with.overflow">;
def LLVM_UMulWithOverflowOp : LLVM_ArithWithOverflowOp<"umul.with.overflow">;

//
// Saturation Arithmetic Intrinsics.
//

def LLVM_SAddSat : LLVM_BinarySameArgsIntrOpI<"sadd.sat">;
def LLVM_UAddSat : LLVM_BinarySameArgsIntrOpI<"uadd.sat">;
def LLVM_SSubSat : LLVM_BinarySameArgsIntrOpI<"ssub.sat">;
def LLVM_USubSat : LLVM_BinarySameArgsIntrOpI<"usub.sat">;
def LLVM_SSHLSat : LLVM_BinarySameArgsIntrOpI<"sshl.sat">;
def LLVM_USHLSat : LLVM_BinarySameArgsIntrOpI<"ushl.sat">;

//
// Optimization hint intrinsics.
//

def LLVM_AssumeOp
    : LLVM_ZeroResultIntrOp<"assume", /*overloadedOperands=*/[], /*traits=*/[],
                            /*requiresAccessGroup=*/0,
                            /*requiresAliasAnalysis=*/1,
                            /*requiresArgAndResultAttrs=*/0,
                            /*requiresOpBundles=*/1> {
  dag args = (ins I1:$cond);
  let arguments = !con(args, baseArgs);

  let assemblyFormat = [{
    $cond
    ( custom<OpBundles>($op_bundle_operands, type($op_bundle_operands),
                        $op_bundle_tags)^ )?
    `:` type($cond) attr-dict
  }];

  let builders = [
    OpBuilder<(ins "Value":$cond)>,
    OpBuilder<(ins "Value":$cond, "llvm::StringRef":$tag, "ValueRange":$args)>,
    OpBuilder<(ins "Value":$cond, "AssumeAlignTag":$tag, "Value":$ptr,
                   "Value":$align)>,
    OpBuilder<(ins "Value":$cond, "AssumeSeparateStorageTag":$tag,
                   "Value":$ptr1, "Value":$ptr2)>
  ];

  let hasVerifier = 1;
}

def LLVM_SSACopyOp : LLVM_OneResultIntrOp<"ssa.copy", [], [0],
                                            [Pure, SameOperandsAndResultType]> {
  let arguments = (ins AnyType:$operand);

  let assemblyFormat = "$operand attr-dict `:` type($operand)";
}

def LLVM_IsConstantOp : LLVM_IntrOp<"is.constant", [], [0], [Pure], 1> {
  let arguments = (ins LLVM_Type:$val);
  let results = (outs I1:$res);
}

def LLVM_ExpectOp
  : LLVM_OneResultIntrOp<"expect", [], [0],
                         [Pure, SameOperandsAndResultType]> {
  let arguments = (ins AnySignlessInteger:$val,
                       AnySignlessInteger:$expected);
  let assemblyFormat = "$val `,` $expected attr-dict `:` type($val)";
}

def LLVM_ExpectWithProbabilityOp
  : LLVM_OneResultIntrOp<"expect.with.probability", [], [0],
                         [Pure, AllTypesMatch<["val", "expected", "res"]>],
                         /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
                         /*immArgPositions=*/[2], /*immArgAttrNames=*/["prob"]> {
  let arguments = (ins AnySignlessInteger:$val,
                       AnySignlessInteger:$expected,
                       F64Attr:$prob);
  let assemblyFormat = "$val `,` $expected `,` $prob attr-dict `:` type($val)";
}

def LLVM_ThreadlocalAddressOp : LLVM_OneResultIntrOp<"threadlocal.address", [],
                                [0], [Pure]> {
  let arguments = (ins LLVM_AnyPointer:$global);
}

//
// Coroutine intrinsics.
//

def LLVM_CoroIdOp : LLVM_IntrOp<"coro.id", [], [], [], 1> {
  let arguments = (ins I32:$align,
                       LLVM_AnyPointer:$promise,
                       LLVM_AnyPointer:$coroaddr,
                       LLVM_AnyPointer:$fnaddrs);
  let assemblyFormat = "$align `,` $promise `,` $coroaddr `,` $fnaddrs"
    " attr-dict `:` functional-type(operands, results)";
}

def LLVM_CoroBeginOp : LLVM_IntrOp<"coro.begin", [], [], [], 1> {
  let arguments = (ins LLVM_TokenType:$token,
                       LLVM_AnyPointer:$mem);
  let assemblyFormat = "$token `,` $mem attr-dict `:` functional-type(operands, results)";
}

def LLVM_CoroSizeOp : LLVM_IntrOp<"coro.size", [0], [], [], 1> {
  let assemblyFormat = "attr-dict `:` type($res)";
}

def LLVM_CoroAlignOp : LLVM_IntrOp<"coro.align", [0], [], [], 1> {
  let assemblyFormat = "attr-dict `:` type($res)";
}

def LLVM_CoroSaveOp : LLVM_IntrOp<"coro.save", [], [], [], 1> {
  let arguments = (ins LLVM_AnyPointer:$handle);
  let assemblyFormat = "$handle attr-dict `:` functional-type(operands, results)";
}

def LLVM_CoroSuspendOp : LLVM_IntrOp<"coro.suspend", [], [], [], 1> {
  let arguments = (ins LLVM_TokenType:$save,
                       I1:$final);
  let assemblyFormat = "$save `,` $final attr-dict `:` type($res)";
}

def LLVM_CoroEndOp : LLVM_IntrOp<"coro.end", [], [], [], 1> {
  let arguments = (ins LLVM_AnyPointer:$handle,
                       I1:$unwind,
                       LLVM_TokenType:$retvals);
  let assemblyFormat = "$handle `,` $unwind `,` $retvals attr-dict `:` functional-type(operands, results)";
}

def LLVM_CoroFreeOp : LLVM_IntrOp<"coro.free", [], [], [], 1> {
  let arguments = (ins LLVM_TokenType:$id,
                       LLVM_AnyPointer:$handle);
  let assemblyFormat = "$id `,` $handle attr-dict `:` functional-type(operands, results)";
}

def LLVM_CoroResumeOp : LLVM_IntrOp<"coro.resume", [], [], [], 0> {
  let arguments = (ins LLVM_AnyPointer:$handle);
  let assemblyFormat = "$handle attr-dict `:` qualified(type($handle))";
}

def LLVM_CoroPromiseOp : LLVM_IntrOp<"coro.promise", [], [], [], 1> {
  let arguments = (ins LLVM_AnyPointer:$handle,
                       I32:$align,
                       I1:$from);
  let results = (outs LLVM_AnyPointer:$res);
  let assemblyFormat = "$handle `,` $align `,` $from attr-dict `:` functional-type(operands, results)";
}

//
// Debug function intrinsics.
//

class LLVM_DbgIntrOp<string name, string argName, list<Trait> traits = []>
    : LLVM_IntrOp<name, [], [], traits, 0> {
  let mlirBuilder = [{
    // Add debug intrindic to the list of intrinsics that need to be converted once the
    // full function was converted.
    moduleImport.addDebugIntrinsic(inst);
    return success();
  }];
  let assemblyFormat = [{
    qualified($varInfo) (qualified($locationExpr)^)? `=` $}] # argName #
      [{ `:` qualified(type($}] # argName # [{)) attr-dict
  }];
}

def LLVM_DbgDeclareOp : LLVM_DbgIntrOp<"dbg.declare", "addr", [
    DeclareOpInterfaceMethods<PromotableOpInterface, [
      "requiresReplacedValues", "visitReplacedValues"
    ]>]> {
  let summary = "Describes how the address relates to a source language variable.";
  let arguments = (ins
    LLVM_AnyPointer:$addr,
    LLVM_DILocalVariableAttr:$varInfo,
    DefaultValuedAttr<LLVM_DIExpressionAttr, "{}">:$locationExpr
  );
  let llvmBuilder = [{
    // Debug records without debug locations are invalid.
    if(!builder.getCurrentDebugLocation())
      return success();
    llvm::DILocalScope *scope = getLocalScopeFromLoc(builder, opInst.getLoc(),
                                                     moduleTranslation);

    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::DIBuilder debugInfoBuilder(*module);
    debugInfoBuilder.insertDeclare(moduleTranslation.lookupValue(opInst.getOperand(0)),
                     llvm::cast<llvm::DILocalVariable>(
                         moduleTranslation.translateDebugInfo($varInfo)),
                     moduleTranslation.translateExpression($locationExpr),
                     moduleTranslation.translateLoc(opInst.getLoc(), scope),
                     builder.GetInsertPoint());
  }];
}

def LLVM_DbgValueOp : LLVM_DbgIntrOp<"dbg.value", "value",
    [DeclareOpInterfaceMethods<PromotableOpInterface>]> {
  let summary = "Describes how the value relates to a source language variable.";
  let arguments = (ins
    LLVM_Type:$value,
    LLVM_DILocalVariableAttr:$varInfo,
    DefaultValuedAttr<LLVM_DIExpressionAttr, "{}">:$locationExpr
  );
  let llvmBuilder = [{
    // Debug records without debug locations are invalid.
    if(!builder.getCurrentDebugLocation())
      return success();
    llvm::DILocalScope *scope = getLocalScopeFromLoc(builder, opInst.getLoc(),
                                                     moduleTranslation);

    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::DIBuilder debugInfoBuilder(*module);
    debugInfoBuilder.insertDbgValueIntrinsic(
        moduleTranslation.lookupValue(opInst.getOperand(0)),
        llvm::cast<llvm::DILocalVariable>(
            moduleTranslation.translateDebugInfo($varInfo)),
        moduleTranslation.translateExpression($locationExpr),
        moduleTranslation.translateLoc(opInst.getLoc(), scope),
        builder.GetInsertPoint());
  }];
}

// Named machine registers. The register name is an `MDString` inside a node --
// `metadata !{!"sp"}` -- rather than a bare string as
// `llvm.allow.runtime.check` uses, so the wrapper is unpacked on the way in and
// rebuilt on the way out.
def LLVM_RegisterAccess {
  code nameFromMetadata = [{
    auto *wrapper = dyn_cast<llvm::MetadataAsValue>(llvmOperands[0]);
    if (!wrapper)
      return failure();
    auto *node = dyn_cast<llvm::MDNode>(wrapper->getMetadata());
    if (!node || node->getNumOperands() != 1)
      return failure();
    auto *name = dyn_cast<llvm::MDString>(node->getOperand(0).get());
    if (!name)
      return failure();
  }];
  code nameToMetadata = [{
    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::LLVMContext &ctx = module->getContext();
    llvm::Metadata *nameNode =
        llvm::MDNode::get(ctx, llvm::MDString::get(ctx, $reg));
    llvm::Value *nameArg = llvm::MetadataAsValue::get(ctx, nameNode);
  }];
}

def LLVM_ReadRegisterOp
    : LLVM_OneResultIntrOp<"read_register", [0], [], []> {
  let summary = "Reads a named machine register.";
  let arguments = (ins StrAttr:$reg);
  let assemblyFormat = "$reg attr-dict `:` type($res)";
  let llvmBuilder = LLVM_RegisterAccess.nameToMetadata # [{
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(
        module, llvm::Intrinsic::read_register, {$_resultType});
    $res = builder.CreateCall(fn, {nameArg});
  }];
  let mlirBuilder = LLVM_RegisterAccess.nameFromMetadata # [{
    $res = $_builder.create<LLVM::ReadRegisterOp>(
        $_location, $_resultType, $_builder.getStringAttr(name->getString()));
  }];
}

def LLVM_WriteRegisterOp
    : LLVM_ZeroResultIntrOp<"write_register", [1], []> {
  let summary = "Writes a named machine register.";
  let arguments = (ins StrAttr:$reg, AnySignlessInteger:$value);
  let assemblyFormat = "$reg `,` $value attr-dict `:` qualified(type($value))";
  let llvmBuilder = LLVM_RegisterAccess.nameToMetadata # [{
    llvm::Value *value = moduleTranslation.lookupValue(opInst.getOperand(0));
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(
        module, llvm::Intrinsic::write_register, {value->getType()});
    builder.CreateCall(fn, {nameArg, value});
  }];
  let mlirBuilder = LLVM_RegisterAccess.nameFromMetadata # [{
    $_op = LLVM::WriteRegisterOp::create($_builder, $_location,
        $_builder.getStringAttr(name->getString()), $value);
  }];
}

def LLVM_ReadVolatileRegisterOp
    : LLVM_OneResultIntrOp<"read_volatile_register", [0], [], []> {
  let summary = "Reads a named machine register, without eliding the read.";
  let description = [{
    `llvm.read_volatile_register`. The same read as `llvm.read_register`, but
    LLVM gives it `IntrHasSideEffects` where the other has `IntrReadMem`, so
    two reads of one register cannot be folded into one.

    A separate op rather than a flag on `llvm.intr.read_register`, because the
    difference *is* the side-effect behaviour: an op models that in its traits,
    which cannot vary per instance.
  }];
  let arguments = (ins StrAttr:$reg);
  let assemblyFormat = "$reg attr-dict `:` type($res)";
  let llvmBuilder = LLVM_RegisterAccess.nameToMetadata # [{
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(
        module, llvm::Intrinsic::read_volatile_register, {$_resultType});
    $res = builder.CreateCall(fn, {nameArg});
  }];
  let mlirBuilder = LLVM_RegisterAccess.nameFromMetadata # [{
    $res = $_builder.create<LLVM::ReadVolatileRegisterOp>(
        $_location, $_resultType, $_builder.getStringAttr(name->getString()));
  }];
}

// The two constrained comparisons. They do not fit `LLVM_ConstrainedIntr`:
// their first metadata operand is a comparison predicate, not a rounding mode,
// so putting them through that class would read the predicate as one.
class LLVM_ConstrainedFPCmpIntr<string mnem>
    : LLVM_OneResultIntrOp<"experimental.constrained." # mnem, [], [0],
          [DeclareOpInterfaceMethods<FPExceptionBehaviorOpInterface>,
           // LLVM declares the second operand as `LLVMMatchType<0>`, so the
           // two always agree; saying so lets the format print one type.
           AllTypesMatch<["lhs", "rhs"]>]> {
  let arguments = (ins LLVM_ScalarOrVectorOf<LLVM_AnyFloat>:$lhs,
                       LLVM_ScalarOrVectorOf<LLVM_AnyFloat>:$rhs,
                       FCmpPredicate:$predicate,
                       FPExceptionBehaviorAttr:$fpExceptionBehavior);
  let assemblyFormat = [{
    $predicate `,` $lhs `,` $rhs $fpExceptionBehavior attr-dict `:`
      qualified(type($lhs)) `to` qualified(type($res))
  }];
  let llvmBuilder = [{
    $res = builder.CreateConstrainedFPCmp(
        llvm::Intrinsic::experimental_constrained_}] # mnem # [{,
        convertFCmpPredicateToLLVM($predicate), $lhs, $rhs, "",
        moduleTranslation.translateFPExceptionBehavior($fpExceptionBehavior));
  }];
  let mlirBuilder = [{
    auto *cmp = cast<llvm::ConstrainedFPCmpIntrinsic>(inst);
    $res = $_builder.create<$_qualCppClassName>($_location, $_resultType,
        $lhs, $rhs,
        LLVM::FCmpPredicateAttr::get($_builder.getContext(),
            convertFCmpPredicateFromLLVM(cmp->getPredicate())),
        $_fpExceptionBehavior_attr($fpExceptionBehavior));
  }];
}

def LLVM_ConstrainedFCmp : LLVM_ConstrainedFPCmpIntr<"fcmp"> {
  let summary = "Constrained floating-point comparison, quiet.";
}

def LLVM_ConstrainedFCmpS : LLVM_ConstrainedFPCmpIntr<"fcmps"> {
  let summary = "Constrained floating-point comparison, signalling.";
}

def LLVM_AllowRuntimeCheckOp
    : LLVM_OneResultIntrOp<"allow.runtime.check", [], [], []> {
  let summary = "Asks whether a runtime check of the given kind is allowed.";
  let description = [{
    `__builtin_allow_runtime_check`. The kind names the check -- the frontend
    picks the string -- and LLVM carries it as an `MDString` operand rather
    than a value, so it is an attribute here.
  }];
  let arguments = (ins StrAttr:$kind);
  let assemblyFormat = "$kind attr-dict `:` type($res)";
  let llvmBuilder = [{
    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::LLVMContext &ctx = module->getContext();
    llvm::Function *fn = llvm::Intrinsic::getOrInsertDeclaration(
        module, llvm::Intrinsic::allow_runtime_check);
    $res = builder.CreateCall(fn, {llvm::MetadataAsValue::get(
        ctx, llvm::MDString::get(ctx, $kind))});
  }];
  let mlirBuilder = [{
    // The operand is metadata, not a value, so the generic argument
    // conversion cannot reach it -- it asserts on a `MetadataAsValue`.
    auto *wrapper = dyn_cast<llvm::MetadataAsValue>(llvmOperands[0]);
    if (!wrapper)
      return failure();
    auto *kind = dyn_cast<llvm::MDString>(wrapper->getMetadata());
    if (!kind)
      return failure();
    $res = $_builder.create<LLVM::AllowRuntimeCheckOp>(
        $_location, $_resultType, $_builder.getStringAttr(kind->getString()));
  }];
}

def LLVM_DbgAssignOp : LLVM_IntrOp<"dbg.assign", [], [], [], 0> {
  let summary = "Describes an assignment made to a source language variable.";
  let description = [{
    The assignment-tracking form of `llvm.dbg.value`. It names the value
    assigned *and* the address assigned to, and carries a `DIAssignID` linking
    it to the store that performs the assignment -- the store carries the same
    identity in its `llvm.dbg_assign_id` attribute.
  }];
  let arguments = (ins
    LLVM_Type:$value,
    LLVM_DILocalVariableAttr:$varInfo,
    DefaultValuedAttr<LLVM_DIExpressionAttr, "{}">:$locationExpr,
    LLVM_DIAssignIDAttr:$assignID,
    LLVM_AnyPointer:$address,
    DefaultValuedAttr<LLVM_DIExpressionAttr, "{}">:$addressExpr
  );
  let llvmBuilder = [{
    // Debug records without debug locations are invalid.
    if(!builder.getCurrentDebugLocation())
      return success();
    llvm::DILocalScope *scope = getLocalScopeFromLoc(builder, opInst.getLoc(),
                                                     moduleTranslation);

    // A debug *record*, not a call to `llvm.dbg.assign`: the intrinsic form no
    // longer survives a module. `Module::removeDebugIntrinsicDeclarations`
    // asserts that the declaration has no uses left, so emitting the call
    // aborts the export rather than producing anything.
    //
    // `DIBuilder::insertDbgAssign` takes the linked store and reads the
    // identity off it; here the identity is the operand, and the store that
    // shares it carries it in its own `!DIAssignID` attachment, so the record
    // is built directly.
    llvm::DbgVariableRecord *record = llvm::DbgVariableRecord::createDVRAssign(
        moduleTranslation.lookupValue(opInst.getOperand(0)),
        llvm::cast<llvm::DILocalVariable>(
            moduleTranslation.translateDebugInfo($varInfo)),
        moduleTranslation.translateExpression($locationExpr),
        llvm::cast<llvm::DIAssignID>(
            moduleTranslation.translateDIAssignID($assignID)),
        moduleTranslation.lookupValue(opInst.getOperand(1)),
        moduleTranslation.translateExpression($addressExpr),
        moduleTranslation.translateLoc(opInst.getLoc(), scope));
    builder.GetInsertBlock()->insertDbgRecordBefore(record,
                                                    builder.GetInsertPoint());
  }];
  let mlirBuilder = [{
    // Converted once the full function is available, as the other debug
    // intrinsics are: the operands need not dominate the record yet.
    moduleImport.addDebugIntrinsic(inst);
    return success();
  }];
  // The address expression prints before the address rather than after it: an
  // attribute with no buildable type sitting immediately before the `:` makes
  // the format ambiguous, and an operand there does not.
  let assemblyFormat = [{
    qualified($varInfo) (qualified($locationExpr)^)? `=` $value
      `,` `id` `=` qualified($assignID)
      `,` `addr` `=` qualified($addressExpr) $address
      `:` qualified(type($value)) `,` qualified(type($address)) attr-dict
  }];
}

def LLVM_DbgLabelOp : LLVM_IntrOp<"dbg.label", [], [], [], 0> {
  let summary = "Relates the program to a debug information label.";
  let arguments = (ins LLVM_DILabelAttr:$label);
  let llvmBuilder = [{
    // Debug records without debug locations are invalid.
    if(!builder.getCurrentDebugLocation())
      return success();
    llvm::DILocalScope *scope = getLocalScopeFromLoc(builder, opInst.getLoc(),
                                                     moduleTranslation);

    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::DIBuilder debugInfoBuilder(*module);
    debugInfoBuilder.insertLabel(
        llvm::cast<llvm::DILabel>(moduleTranslation.translateDebugInfo($label)),
        moduleTranslation.translateLoc(opInst.getLoc(), scope),
        builder.GetInsertPoint());
  }];
  let mlirBuilder = [{
    DILabelAttr labelAttr = $_label_attr($label);
    // Drop the intrinsic if the label translation fails due to cylic metadata.
    if (!labelAttr)
      return success();
    $_op = $_qualCppClassName::create($_builder, $_location, labelAttr);
  }];
  let assemblyFormat = "$label attr-dict";
}

//
// Variadic function intrinsics.
//

def LLVM_VaStartOp : LLVM_ZeroResultIntrOp<"vastart", [0], [],
                       /*requiresAccessGroup=*/0,
                       /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins LLVM_AnyPointer:$arg_list), baseArgs);
  let assemblyFormat = "$arg_list attr-dict `:` qualified(type($arg_list))";
  let summary = "Initializes `arg_list` for subsequent variadic argument extractions.";
}

def LLVM_VaCopyOp : LLVM_ZeroResultIntrOp<"vacopy", [0], [],
                      /*requiresAccessGroup=*/0,
                      /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins LLVM_AnyPointer:$dest_list,
                            LLVM_AnyPointer:$src_list), baseArgs);
  let assemblyFormat = "$src_list `to` $dest_list attr-dict `:` type(operands)";
  let summary = "Copies the current argument position from `src_list` to `dest_list`.";
}

def LLVM_VaEndOp : LLVM_ZeroResultIntrOp<"vaend", [0], [],
                     /*requiresAccessGroup=*/0,
                     /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins LLVM_AnyPointer:$arg_list), baseArgs);
  let assemblyFormat = "$arg_list attr-dict `:` qualified(type($arg_list))";
  let summary = "Destroys `arg_list`, which has been initialized by `intr.vastart` or `intr.vacopy`.";
}

//
// Exception handling intrinsics.
//

def LLVM_EhTypeidForOp : LLVM_OneResultIntrOp<"eh.typeid.for", [], [0]> {
    let arguments = (ins LLVM_AnyPointer:$type_info);
    let assemblyFormat = "$type_info attr-dict `:` functional-type(operands, results)";
}

//
// Stack save/restore intrinsics.
//

def LLVM_StackSaveOp : LLVM_OneResultIntrOp<"stacksave", [0], [], [],
                         /*requiresFastmath=*/0,
                         /*requiresArgAndResultAttrs=*/0,
                         /*immArgPositions=*/[], /*immArgAttrNames=*/[],
                         /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins), baseArgs);
  let assemblyFormat = "attr-dict `:` qualified(type($res))";
}

def LLVM_StackRestoreOp : LLVM_ZeroResultIntrOp<"stackrestore", [0], [],
                            /*requiresAccessGroup=*/0,
                            /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins LLVM_AnyPointer:$ptr), baseArgs);
  let assemblyFormat = "$ptr attr-dict `:` qualified(type($ptr))";
}

//
// Vector Reductions.
//

// LLVM vector reduction over a single vector.
class LLVM_VecReductionBase<string mnem, Type element, bit requiresFastmath=0>
    : LLVM_OneResultIntrOp<"vector.reduce." # mnem, [], [0],
                           [Pure, SameOperandsAndResultElementType],
                           requiresFastmath> {
      dag commonArgs = (ins LLVM_VectorOf<element>:$in);
}

class LLVM_VecReductionF<string mnem>
    : LLVM_VecReductionBase<mnem, AnyFloat, /*requiresFastmath=*/1> {
  dag fmfArg = (
    ins DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags);
  let arguments = !con(commonArgs, fmfArg);

  let assemblyFormat = "`(` operands `)` attr-dict `:` "
      "functional-type(operands, results)";
}

class LLVM_VecReductionI<string mnem>
    : LLVM_VecReductionBase<mnem, AnySignlessInteger> {
      let arguments = commonArgs;
}

// LLVM vector reduction over a single vector, with an initial value,
// and with permission to reassociate the reduction operations.
class LLVM_VecReductionAccBase<string mnem, Type element>
    : LLVM_OneResultIntrOp</*mnem=*/"vector.reduce." # mnem,
                           /*overloadedResults=*/[],
                           /*overloadedOperands=*/[1],
                           /*traits=*/[Pure, SameOperandsAndResultElementType],
                           /*requiresFastmath=*/1>,
      Arguments<(ins element:$start_value,
                     LLVM_VectorOf<element>:$input,
                     DefaultValuedAttr<LLVM_FastmathFlagsAttr, "{}">:$fastmathFlags)>;

class LLVM_VecReductionAccF<string mnem>
    : LLVM_VecReductionAccBase<mnem, AnyFloat>;

def LLVM_vector_reduce_add : LLVM_VecReductionI<"add">;
def LLVM_vector_reduce_and : LLVM_VecReductionI<"and">;
def LLVM_vector_reduce_mul : LLVM_VecReductionI<"mul">;
def LLVM_vector_reduce_or : LLVM_VecReductionI<"or">;
def LLVM_vector_reduce_smax : LLVM_VecReductionI<"smax">;
def LLVM_vector_reduce_smin : LLVM_VecReductionI<"smin">;
def LLVM_vector_reduce_umax : LLVM_VecReductionI<"umax">;
def LLVM_vector_reduce_umin : LLVM_VecReductionI<"umin">;
def LLVM_vector_reduce_xor : LLVM_VecReductionI<"xor">;

def LLVM_vector_reduce_fmax : LLVM_VecReductionF<"fmax">;
def LLVM_vector_reduce_fmin : LLVM_VecReductionF<"fmin">;
def LLVM_vector_reduce_fmaximum : LLVM_VecReductionF<"fmaximum">;
def LLVM_vector_reduce_fminimum : LLVM_VecReductionF<"fminimum">;

def LLVM_vector_reduce_fadd : LLVM_VecReductionAccF<"fadd">;
def LLVM_vector_reduce_fmul : LLVM_VecReductionAccF<"fmul">;

//
// LLVM Matrix operations.
//

/// Create a column major, strided 2-D matrix load, as specified in the LLVM
/// MatrixBuilder.
/// data       - Start address of the matrix read
/// rows       - Number of rows in matrix (must be a constant)
/// isVolatile - True if the load operation is marked as volatile.
/// columns    - Number of columns in matrix (must be a constant)
/// stride     - Space between columns
def LLVM_MatrixColumnMajorLoadOp : LLVM_OneResultIntrOp<"matrix.column.major.load"> {
  let arguments = (ins LLVM_AnyPointer:$data, AnySignlessInteger:$stride, I1Attr:$isVolatile,
                   I32Attr:$rows, I32Attr:$columns);
  let results = (outs LLVM_AnyVector:$res);
  let builders = [LLVM_OneResultOpBuilder];
  let assemblyFormat = "$data `,` `<` `stride` `=` $stride `>` attr-dict"
    "`:` type($res) `from` qualified(type($data)) `stride` type($stride)";

  string llvmBuilder = [{
    llvm::MatrixBuilder mb(builder);
    const llvm::DataLayout &dl =
      builder.GetInsertBlock()->getModule()->getDataLayout();
    llvm::Type *ElemTy = moduleTranslation.convertType(
        op.getType().getElementType());
    llvm::Align align = dl.getABITypeAlign(ElemTy);
    $res = mb.CreateColumnMajorLoad(
      ElemTy, $data, align, $stride, $isVolatile, $rows,
      $columns);
  }];
  string mlirBuilder = [{
    $res = LLVM::MatrixColumnMajorLoadOp::create($_builder,
      $_location, $_resultType, $data, $stride,
      $_int_attr($isVolatile), $_int_attr($rows), $_int_attr($columns));
  }];
}

/// Create a column major, strided 2-D matrix store, as specified in the LLVM
/// MatrixBuilder.
/// matrix     - Matrix to store
/// ptr        - Pointer to write back to
/// isVolatile - True if the load operation is marked as volatile.
/// rows       - Number of rows in matrix (must be a constant)
/// columns    - Number of columns in matrix (must be a constant)
/// stride     - Space between columns
def LLVM_MatrixColumnMajorStoreOp : LLVM_ZeroResultIntrOp<"matrix.column.major.store"> {
  let arguments = (ins LLVM_AnyVector:$matrix, LLVM_AnyPointer:$data,
                   AnySignlessInteger:$stride, I1Attr:$isVolatile, I32Attr:$rows,
                   I32Attr:$columns);
  let builders = [LLVM_VoidResultTypeOpBuilder, LLVM_ZeroResultOpBuilder];
  let assemblyFormat = "$matrix `,` $data `,` `<` `stride` `=` $stride `>` "
    "attr-dict`:` type($matrix) `to` qualified(type($data)) `stride` type($stride)";

  string llvmBuilder = [{
    llvm::MatrixBuilder mb(builder);
    const llvm::DataLayout &dl =
      builder.GetInsertBlock()->getModule()->getDataLayout();
    Type elementType = op.getMatrix().getType().getElementType();
    llvm::Align align = dl.getABITypeAlign(
      moduleTranslation.convertType(elementType));
    mb.CreateColumnMajorStore(
      $matrix, $data, align, $stride, $isVolatile,
      $rows, $columns);
  }];
  string mlirBuilder = [{
    $_op = LLVM::MatrixColumnMajorStoreOp::create($_builder,
      $_location, $matrix, $data, $stride,
      $_int_attr($isVolatile), $_int_attr($rows), $_int_attr($columns));
  }];
}

/// Create a llvm.matrix.multiply call, multiplying 2-D matrices LHS and RHS, as
/// specified in the LLVM MatrixBuilder.
def LLVM_MatrixMultiplyOp : LLVM_OneResultIntrOp<"matrix.multiply"> {
  let arguments = (ins LLVM_AnyVector:$lhs, LLVM_AnyVector:$rhs, I32Attr:$lhs_rows,
                   I32Attr:$lhs_columns, I32Attr:$rhs_columns);
  let results = (outs LLVM_AnyVector:$res);
  let builders = [LLVM_OneResultOpBuilder];
  let assemblyFormat = "$lhs `,` $rhs attr-dict "
    "`:` `(` type($lhs) `,` type($rhs) `)` `->` type($res)";

  string llvmBuilder = [{
    llvm::MatrixBuilder mb(builder);
    $res = mb.CreateMatrixMultiply(
      $lhs, $rhs, $lhs_rows, $lhs_columns,
      $rhs_columns);
  }];
  string mlirBuilder = [{
    $res = LLVM::MatrixMultiplyOp::create($_builder,
      $_location, $_resultType, $lhs, $rhs,
      $_int_attr($lhs_rows), $_int_attr($lhs_columns), $_int_attr($rhs_columns));
  }];
}

/// Create a llvm.matrix.transpose call, transposing a `rows` x `columns` 2-D
/// `matrix`, as specified in the LLVM MatrixBuilder.
def LLVM_MatrixTransposeOp : LLVM_OneResultIntrOp<"matrix.transpose"> {
  let arguments = (ins LLVM_AnyVector:$matrix, I32Attr:$rows, I32Attr:$columns);
  let results = (outs LLVM_AnyVector:$res);
  let builders = [LLVM_OneResultOpBuilder];
  let assemblyFormat = "$matrix attr-dict `:` type($matrix) `into` type($res)";

  string llvmBuilder = [{
    llvm::MatrixBuilder mb(builder);
    $res = mb.CreateMatrixTranspose(
      $matrix, $rows, $columns);
  }];
  string mlirBuilder = [{
    $res = LLVM::MatrixTransposeOp::create($_builder,
      $_location, $_resultType, $matrix,
      $_int_attr($rows), $_int_attr($columns));
  }];
}

//
// LLVM masked operations.
//

/// Create a llvm.get.active.lane.mask to set a mask up to a given position.
def LLVM_GetActiveLaneMaskOp
    : LLVM_OneResultIntrOp<"get.active.lane.mask", [0], [0], [Pure]> {
  let arguments = (ins AnySignlessInteger:$base, AnySignlessInteger:$n);
  let assemblyFormat = "$base `,` $n attr-dict `:` "
    "type($base) `,` type($n) `to` type($res)";
}

/// Create a call to Masked Load intrinsic.
def LLVM_MaskedLoadOp : LLVM_OneResultIntrOp<"masked.load"> {
  let arguments = (ins LLVM_AnyPointer:$data, LLVM_VectorOf<I1>:$mask,
                   Optional<LLVM_AnyVector>:$pass_thru, I32Attr:$alignment,
                   UnitAttr:$nontemporal);
  let results = (outs LLVM_AnyVector:$res);
  let assemblyFormat =
    "operands attr-dict `:` functional-type(operands, results)";

  string llvmBuilder = [{
    auto *inst = $pass_thru ? builder.CreateMaskedLoad(
        $_resultType, $data, llvm::Align($alignment), $mask, $pass_thru) :
      builder.CreateMaskedLoad(
        $_resultType, $data, llvm::Align($alignment), $mask);
    $res = inst;
  }] #setNonTemporalMetadataCode;
  string mlirBuilder = [{
    auto *intrinInst = dyn_cast<llvm::IntrinsicInst>(inst);
    bool nontemporal = intrinInst->hasMetadata(llvm::LLVMContext::MD_nontemporal);
    llvm::Align alignment = intrinInst->getParamAlign(0).valueOrOne();
    $res = LLVM::MaskedLoadOp::create($_builder, $_location,
      $_resultType, $data, $mask, $pass_thru,
      $_builder.getI32IntegerAttr(alignment.value()),
      nontemporal ? $_builder.getUnitAttr() : nullptr);
  }];
  list<int> llvmArgIndices = [0, 1, 2, -1, -1];
}

/// Create a call to Masked Store intrinsic.
def LLVM_MaskedStoreOp : LLVM_ZeroResultIntrOp<"masked.store"> {
  let arguments = (ins LLVM_AnyVector:$value, LLVM_AnyPointer:$data,
                   LLVM_VectorOf<I1>:$mask, I32Attr:$alignment);
  let builders = [LLVM_VoidResultTypeOpBuilder, LLVM_ZeroResultOpBuilder];
  let assemblyFormat = "$value `,` $data `,` $mask attr-dict `:` "
    "type($value) `,` type($mask) `into` qualified(type($data))";

  string llvmBuilder = [{
    builder.CreateMaskedStore(
      $value, $data, llvm::Align($alignment), $mask);
  }];
  string mlirBuilder = [{
    auto *intrinInst = dyn_cast<llvm::IntrinsicInst>(inst);
    llvm::Align alignment = intrinInst->getParamAlign(1).valueOrOne();
    $_op = LLVM::MaskedStoreOp::create($_builder, $_location,
      $value, $data, $mask, $_builder.getI32IntegerAttr(alignment.value()));
  }];
  list<int> llvmArgIndices = [0, 1, 2, -1];
}

/// Create a call to Masked Gather intrinsic.
def LLVM_masked_gather : LLVM_OneResultIntrOp<"masked.gather"> {
  let arguments = (ins LLVM_VectorOf<LLVM_AnyPointer>:$ptrs,
                   LLVM_VectorOf<I1>:$mask, Variadic<LLVM_AnyVector>:$pass_thru,
                   I32Attr:$alignment);
  let results = (outs LLVM_AnyVector:$res);
  let builders = [LLVM_OneResultOpBuilder];
  let assemblyFormat =
    "operands attr-dict `:` functional-type(operands, results)";

  string llvmBuilder = [{
    $res = $pass_thru.empty() ? builder.CreateMaskedGather(
        $_resultType, $ptrs, llvm::Align($alignment), $mask) :
      builder.CreateMaskedGather(
        $_resultType, $ptrs, llvm::Align($alignment), $mask, $pass_thru[0]);
  }];
  string mlirBuilder = [{
    auto *intrinInst = dyn_cast<llvm::IntrinsicInst>(inst);
    llvm::Align alignment = intrinInst->getParamAlign(0).valueOrOne();
    $res = LLVM::masked_gather::create($_builder, $_location,
      $_resultType, $ptrs, $mask, $pass_thru,
      $_builder.getI32IntegerAttr(alignment.value()));
  }];
  list<int> llvmArgIndices = [0, 1, 2, -1];

  let hasVerifier = 1;
}

/// Create a call to Masked Scatter intrinsic.
def LLVM_masked_scatter : LLVM_ZeroResultIntrOp<"masked.scatter"> {
  let arguments = (ins LLVM_AnyVector:$value, LLVM_VectorOf<LLVM_AnyPointer>:$ptrs,
                   LLVM_VectorOf<I1>:$mask, I32Attr:$alignment);
  let builders = [LLVM_VoidResultTypeOpBuilder, LLVM_ZeroResultOpBuilder];
  let assemblyFormat = "$value `,` $ptrs `,` $mask attr-dict `:` "
    "type($value) `,` type($mask) `into` type($ptrs)";

  string llvmBuilder = [{
    builder.CreateMaskedScatter(
      $value, $ptrs, llvm::Align($alignment), $mask);
  }];
  string mlirBuilder = [{
    auto *intrinInst = dyn_cast<llvm::IntrinsicInst>(inst);
    llvm::Align alignment = intrinInst->getParamAlign(1).valueOrOne();
    $_op = LLVM::masked_scatter::create($_builder, $_location,
      $value, $ptrs, $mask, $_builder.getI32IntegerAttr(alignment.value()));
  }];
  list<int> llvmArgIndices = [0, 1, 2, -1];

  let hasVerifier = 1;
}

/// Create a call to Masked Expand Load intrinsic.
def LLVM_masked_expandload
  : LLVM_OneResultIntrOp<"masked.expandload", [0], [],
  /*traits=*/[], /*requiresFastMath=*/0, /*requiresArgAndResultAttrs=*/1,
  /*immArgPositions=*/[], /*immArgAttrNames=*/[]> {
  dag args = (ins LLVM_AnyPointer:$ptr,
                       LLVM_VectorOf<I1>:$mask,
                       LLVM_AnyVector:$passthru);

  let arguments = !con(args, baseArgs);

  let builders = [
    OpBuilder<(ins "TypeRange":$resTy, "Value":$ptr, "Value":$mask, "Value":$passthru, CArg<"uint64_t", "1">:$align)>
  ];
}

/// Create a call to Masked Compress Store intrinsic.
def LLVM_masked_compressstore
    : LLVM_ZeroResultIntrOp<"masked.compressstore", [0],
  /*traits=*/[], /*requiresAccessGroup=*/0, /*requiresAliasAnalysis=*/0,
  /*requiresArgAndResultAttrs=*/1, /*requiresOpBundles=*/0,
  /*immArgPositions=*/[], /*immArgAttrNames=*/[]> {
  dag args = (ins LLVM_AnyVector:$value,
                       LLVM_AnyPointer:$ptr,
                       LLVM_VectorOf<I1>:$mask);

  let arguments = !con(args, baseArgs);

  let builders = [
    OpBuilder<(ins "Value":$value, "Value":$ptr, "Value":$mask, CArg<"uint64_t", "1">:$align)>
  ];
}

//
// Annotate intrinsics.
//

// The annotation intrinsics take a pointer and clang emits them from
// `__attribute__((annotate(...)))`, so LLVM attaches the same alias metadata it
// would to any other pointer use. Without `requiresAliasAnalysis` the op has no
// slot for it and `!noalias` is declined -- measured on one real library,
// where this was the single remaining `inst.unhandled` entry across the app.
def LLVM_VarAnnotation
    : LLVM_ZeroResultIntrOp<"var.annotation", [0, 1],
        [AllTypesMatch<["annotation", "fileName", "attr"]>],
        /*requiresAccessGroup=*/0, /*requiresAliasAnalysis=*/1> {
  // `!con(..., baseArgs)` because an explicit `arguments` replaces the list
  // the base class would have generated, alias attributes included.
  let arguments = !con((ins LLVM_AnyPointer:$val,
                            LLVM_AnyPointer:$annotation,
                            LLVM_AnyPointer:$fileName,
                            I32:$line,
                            LLVM_AnyPointer:$attr), baseArgs);
}

def LLVM_PtrAnnotation
    : LLVM_OneResultIntrOp<"ptr.annotation", [0], [2],
        [AllTypesMatch<["res", "ptr"]>,
         AllTypesMatch<["annotation", "fileName", "attr"]>],
        /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
        /*immArgPositions=*/[], /*immArgAttrNames=*/[],
        /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins LLVM_AnyPointer:$ptr,
                            LLVM_AnyPointer:$annotation,
                            LLVM_AnyPointer:$fileName,
                            I32:$line,
                            LLVM_AnyPointer:$attr), baseArgs);
  let results = (outs LLVM_AnyPointer:$res);
}

def LLVM_Annotation
    : LLVM_OneResultIntrOp<"annotation", [0], [2],
        [AllTypesMatch<["res", "integer"]>,
         AllTypesMatch<["annotation", "fileName"]>]> {
  let arguments = (ins AnySignlessInteger:$integer,
                       LLVM_AnyPointer:$annotation,
                       LLVM_AnyPointer:$fileName,
                       I32:$line);
  let results = (outs AnySignlessInteger:$res);
}

//
// Trap intrinsics.
//

def LLVM_Trap : LLVM_ZeroResultIntrOp<"trap", [], [],
                  /*requiresAccessGroup=*/0,
                  /*requiresAliasAnalysis=*/1> {
  let arguments = !con((ins), baseArgs);
  let assemblyFormat = "attr-dict";
}

def LLVM_DebugTrap : LLVM_ZeroResultIntrOp<"debugtrap"> {
  let assemblyFormat = "attr-dict";
}

def LLVM_UBSanTrap : LLVM_ZeroResultIntrOp<"ubsantrap",
  /*overloadedOperands=*/[], /*traits=*/[],
  /*requiresAccessGroup=*/0, /*requiresAliasAnalysis=*/1,
  /*requiresArgAndResultAttrs=*/0, /*requiresOpBundles=*/0,
  /*immArgPositions=*/[0], /*immArgAttrNames=*/["failureKind"]> {
  let arguments = !con((ins I8Attr:$failureKind), baseArgs);

  let assemblyFormat = "prop-dict attr-dict";
}

/// Create a call to vscale intrinsic.
def LLVM_vscale : LLVM_IntrOp<"vscale", [0], [], [], 1>;

/// Create a call to stepvector intrinsic.
def LLVM_StepVectorOp
    : LLVM_IntrOp<"stepvector", [0], [], [Pure], 1> {
  let arguments = (ins);
  let results = (outs LLVM_VectorOf<AnySignlessInteger>:$res);
  let assemblyFormat = "attr-dict `:` type($res)";
}

/// Create a call to vector.insert intrinsic
def LLVM_vector_insert
    : LLVM_OneResultIntrOp<"vector.insert",
                  /*overloadedResults=*/[0], /*overloadedOperands=*/[1],
                  /*traits=*/[Pure, AllTypesMatch<["dstvec", "res"]>,
                  PredOpTrait<"vectors are not bigger than 2^17 bits.", And<[
                    CPred<"getSrcVectorBitWidth() <= 131072">,
                    CPred<"getDstVectorBitWidth() <= 131072">
                  ]>>,
                  PredOpTrait<"it is not inserting scalable into fixed-length vectors.",
                    CPred<"!isScalableVectorType($srcvec.getType()) || "
                          "isScalableVectorType($dstvec.getType())">>],
                  /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
                  /*immArgPositions=*/[2], /*immArgAttrNames=*/["pos"]> {
  let arguments = (ins LLVM_AnyVector:$dstvec, LLVM_AnyVector:$srcvec,
                       I64Attr:$pos);
  let results = (outs LLVM_AnyVector:$res);
  let assemblyFormat = "$srcvec `,` $dstvec `[` $pos `]` attr-dict `:` "
    "type($srcvec) `into` type($res)";
  let extraClassDeclaration = [{
    uint64_t getVectorBitWidth(Type vector) {
      return getVectorNumElements(vector).getKnownMinValue() *
             ::llvm::cast<VectorType>(vector).getElementType()
                .getIntOrFloatBitWidth();
    }
    uint64_t getSrcVectorBitWidth() {
      return getVectorBitWidth(getSrcvec().getType());
    }
    uint64_t getDstVectorBitWidth() {
      return getVectorBitWidth(getDstvec().getType());
    }
  }];
}

/// Create a call to vector.extract intrinsic
def LLVM_vector_extract
    : LLVM_OneResultIntrOp<"vector.extract",
                 /*overloadedResults=*/[0], /*overloadedOperands=*/[0],
                 /*traits=*/[Pure,
                  PredOpTrait<"vectors are not bigger than 2^17 bits.", And<[
                    CPred<"getSrcVectorBitWidth() <= 131072">,
                    CPred<"getResVectorBitWidth() <= 131072">
                  ]>>,
                  PredOpTrait<"it is not extracting scalable from fixed-length vectors.",
                    CPred<"!isScalableVectorType($res.getType()) || "
                          "isScalableVectorType($srcvec.getType())">>],
                  /*requiresFastmath=*/0, /*requiresArgAndResultAttrs=*/0,
                  /*immArgPositions=*/[1], /*immArgAttrNames=*/["pos"]> {
  let arguments = (ins LLVM_AnyVector:$srcvec, I64Attr:$pos);
  let results = (outs LLVM_AnyVector:$res);
  let assemblyFormat = "$srcvec `[` $pos `]` attr-dict `:` "
    "type($res) `from` type($srcvec)";
  let extraClassDeclaration = [{
    uint64_t getVectorBitWidth(Type vector) {
      return getVectorNumElements(vector).getKnownMinValue() *
             ::llvm::cast<VectorType>(vector).getElementType()
                .getIntOrFloatBitWidth();
    }
    uint64_t getSrcVectorBitWidth() {
      return getVectorBitWidth(getSrcvec().getType());
    }
    uint64_t getResVectorBitWidth() {
      return getVectorBitWidth(getRes().getType());
    }
  }];
}

def LLVM_vector_interleave2
    : LLVM_OneResultIntrOp<"vector.interleave2",
        /*overloadedResults=*/[0], /*overloadedOperands=*/[],
        /*traits=*/[
          Pure, AllTypesMatch<["vec1", "vec2"]>,
          PredOpTrait<
            "result has twice as many elements as 'vec1'",
            And<[CPred<"getVectorNumElements($res.getType()) == "
                       "getVectorNumElements($vec1.getType()) * 2">,
                 CPred<"::llvm::cast<VectorType>($vec1.getType()).getElementType() == "
                       "::llvm::cast<VectorType>($res.getType()).getElementType()">]>>,
        ]>,
        Arguments<(ins LLVM_AnyVector:$vec1, LLVM_AnyVector:$vec2)>;

def LLVM_vector_deinterleave2
    : LLVM_OneResultIntrOp<"vector.deinterleave2",
        /*overloadedResults=*/[], /*overloadedOperands=*/[0],
        /*traits=*/[Pure]>,
        Arguments<(ins LLVM_AnyVector:$vec)>;

//
// LLVM Vector Predication operations.
//

class LLVM_VPBinaryBase<string mnem, Type element>
    : LLVM_OneResultIntrOp<"vp." # mnem, [0], [], [Pure]>,
      Arguments<(ins LLVM_VectorOf<element>:$lhs, LLVM_VectorOf<element>:$rhs,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

class LLVM_VPBinaryI<string mnem> : LLVM_VPBinaryBase<mnem, AnySignlessInteger>;

class LLVM_VPBinaryF<string mnem> : LLVM_VPBinaryBase<mnem, AnyFloat>;

class LLVM_VPUnaryBase<string mnem, Type element>
    : LLVM_OneResultIntrOp<"vp." # mnem, [0], [], [Pure]>,
      Arguments<(ins LLVM_VectorOf<element>:$op,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

class LLVM_VPUnaryF<string mnem> : LLVM_VPUnaryBase<mnem, AnyFloat>;

class LLVM_VPTernaryBase<string mnem, Type element>
    : LLVM_OneResultIntrOp<"vp." # mnem, [0], [], [Pure]>,
      Arguments<(ins LLVM_VectorOf<element>:$op1, LLVM_VectorOf<element>:$op2,
                     LLVM_VectorOf<element>:$op3, LLVM_VectorOf<I1>:$mask,
                     I32:$evl)>;

class LLVM_VPTernaryF<string mnem> : LLVM_VPTernaryBase<mnem, AnyFloat>;

class LLVM_VPReductionBase<string mnem, Type element>
    : LLVM_OneResultIntrOp<"vp.reduce." # mnem, [], [1], [Pure]>,
      Arguments<(ins element:$satrt_value, LLVM_VectorOf<element>:$val,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

class LLVM_VPReductionI<string mnem> : LLVM_VPReductionBase<mnem, AnySignlessInteger>;

class LLVM_VPReductionF<string mnem> : LLVM_VPReductionBase<mnem, AnyFloat>;

class LLVM_VPSelectBase<string mnem>
    : LLVM_OneResultIntrOp<"vp." # mnem, [], [1], [Pure]>,
      Arguments<(ins LLVM_VectorOf<I1>:$cond, LLVM_AnyVector:$true_val,
                     LLVM_AnyVector:$false_val, I32:$evl)>;

class LLVM_VPCastBase<string mnem, Type element>
    : LLVM_OneResultIntrOp<"vp." # mnem, [0], [0], [Pure]>,
      Arguments<(ins LLVM_VectorOf<element>:$src,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

class LLVM_VPCastI<string mnem>   : LLVM_VPCastBase<mnem, AnySignlessInteger>;

class LLVM_VPCastF<string mnem>   : LLVM_VPCastBase<mnem, AnyFloat>;

class LLVM_VPCastPtr<string mnem> : LLVM_VPCastBase<mnem, LLVM_AnyPointer>;

// Integer Binary
def LLVM_VPAddOp  : LLVM_VPBinaryI<"add">;
def LLVM_VPSubOp  : LLVM_VPBinaryI<"sub">;
def LLVM_VPMulOp  : LLVM_VPBinaryI<"mul">;
def LLVM_VPSDivOp : LLVM_VPBinaryI<"sdiv">;
def LLVM_VPUDivOp : LLVM_VPBinaryI<"udiv">;
def LLVM_VPSRemOp : LLVM_VPBinaryI<"srem">;
def LLVM_VPURemOp : LLVM_VPBinaryI<"urem">;
def LLVM_VPAShrOp : LLVM_VPBinaryI<"ashr">;
def LLVM_VPLShrOp : LLVM_VPBinaryI<"lshr">;
def LLVM_VPShlOp  : LLVM_VPBinaryI<"shl">;
def LLVM_VPOrOp   : LLVM_VPBinaryI<"or">;
def LLVM_VPAndOp  : LLVM_VPBinaryI<"and">;
def LLVM_VPXorOp  : LLVM_VPBinaryI<"xor">;
def LLVM_VPSMaxOp : LLVM_VPBinaryI<"smax">;
def LLVM_VPSMinOp : LLVM_VPBinaryI<"smin">;
def LLVM_VPUMaxOp : LLVM_VPBinaryI<"umax">;
def LLVM_VPUMinOp : LLVM_VPBinaryI<"umin">;

// Float Binary
def LLVM_VPFAddOp : LLVM_VPBinaryF<"fadd">;
def LLVM_VPFSubOp : LLVM_VPBinaryF<"fsub">;
def LLVM_VPFMulOp : LLVM_VPBinaryF<"fmul">;
def LLVM_VPFDivOp : LLVM_VPBinaryF<"fdiv">;
def LLVM_VPFRemOp : LLVM_VPBinaryF<"frem">;

// Float Unary
def LLVM_VPFNegOp : LLVM_VPUnaryF<"fneg">;

// Float Ternary
def LLVM_VPFMulAddOp  : LLVM_VPTernaryF<"fmuladd">;
def LLVM_VPFmaOp      : LLVM_VPTernaryF<"fma">;

// Integer Reduction
def LLVM_VPReduceAddOp  : LLVM_VPReductionI<"add">;
def LLVM_VPReduceMulOp  : LLVM_VPReductionI<"mul">;
def LLVM_VPReduceAndOp  : LLVM_VPReductionI<"and">;
def LLVM_VPReduceOrOp   : LLVM_VPReductionI<"or">;
def LLVM_VPReduceXorOp  : LLVM_VPReductionI<"xor">;
def LLVM_VPReduceSMaxOp : LLVM_VPReductionI<"smax">;
def LLVM_VPReduceSMinOp : LLVM_VPReductionI<"smin">;
def LLVM_VPReduceUMaxOp : LLVM_VPReductionI<"umax">;
def LLVM_VPReduceUMinOp : LLVM_VPReductionI<"umin">;

// Float Reduction
def LLVM_VPReduceFAddOp : LLVM_VPReductionF<"fadd">;
def LLVM_VPReduceFMulOp : LLVM_VPReductionF<"fmul">;
def LLVM_VPReduceFMaxOp : LLVM_VPReductionF<"fmax">;
def LLVM_VPReduceFMinOp : LLVM_VPReductionF<"fmin">;

def LLVM_VPSelectMinOp : LLVM_VPSelectBase<"select">;
def LLVM_VPMergeMinOp  : LLVM_VPSelectBase<"merge">;

// Load/store
def LLVM_VPLoadOp
    : LLVM_OneResultIntrOp<"vp.load", [0], [0], []>,
      Arguments<(ins LLVM_AnyPointer:$ptr,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

def LLVM_VPStoreOp
    : LLVM_ZeroResultIntrOp<"vp.store", [0, 1], []>,
      Arguments<(ins LLVM_AnyVector:$val,
                     LLVM_AnyPointer:$ptr,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

// Strided load/store
def LLVM_VPStridedLoadOp
    : LLVM_OneResultIntrOp<"experimental.vp.strided.load", [0], [0, 1], []>,
      Arguments<(ins LLVM_AnyPointer:$ptr, AnySignlessInteger:$stride,
                     LLVM_VectorOf<I1>:$mask, I32:$evl)>;

def LLVM_VPStridedStoreOp
    : LLVM_ZeroResultIntrOp<"experimental.vp.strided.store",[0, 1, 2], []>,
      Arguments<(ins LLVM_AnyVector:$val, LLVM_AnyPointer:$ptr,
                     AnySignlessInteger:$stride, LLVM_VectorOf<I1>:$mask, I32:$evl)>;

def LLVM_VPTruncOp : LLVM_VPCastI<"trunc">;
def LLVM_VPZExtOp  : LLVM_VPCastI<"zext">;
def LLVM_VPSExtOp  : LLVM_VPCastI<"sext">;

def LLVM_VPFPTruncOp : LLVM_VPCastF<"fptrunc">;
def LLVM_VPFPExtOp   : LLVM_VPCastF<"fpext">;

def LLVM_VPFPToUIOp : LLVM_VPCastF<"fptoui">;
def LLVM_VPFPToSIOp : LLVM_VPCastF<"fptosi">;

def LLVM_VPUIToFPOp : LLVM_VPCastI<"uitofp">;
def LLVM_VPSIToFPOp : LLVM_VPCastI<"sitofp">;

def LLVM_VPPtrToIntOp : LLVM_VPCastPtr<"ptrtoint">;
def LLVM_VPIntToPtrOp : LLVM_VPCastI<"inttoptr">;

#endif // LLVM_INTRINSIC_OP
