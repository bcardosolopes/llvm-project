//===-- LLVMDialect.td - LLVM IR dialect definition --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVMIR_DIALECT
#define LLVMIR_DIALECT

include "mlir/IR/DialectBase.td"

def LLVM_Dialect : Dialect {
  let name = "llvm";
  let cppNamespace = "::mlir::LLVM";

  let hasConstantMaterializer = 1;
  let useDefaultAttributePrinterParser = 1;
  let hasRegionArgAttrVerify = 1;
  let hasRegionResultAttrVerify = 1;
  let hasOperationAttrVerify = 1;

  let discardableAttrs = (ins
    /// Attribute encoding size and type of GPU workgroup attributions.
    "WorkgroupAttributionAttr":$workgroup_attribution
  );

  let extraClassDeclaration = [{
    static StringRef getTargetAttrName() { return "llvm.target"; }
    /// Name of the data layout attributes.
    static StringRef getDataLayoutAttrName() { return "llvm.data_layout"; }
    static StringRef getNoAliasScopesAttrName() { return "noalias_scopes"; }
    static StringRef getAliasScopesAttrName() { return "alias_scopes"; }
    static StringRef getAccessGroupsAttrName() { return "access_groups"; }
    static StringRef getIdentAttrName() { return "llvm.ident"; }
    static StringRef getModuleFlags() { return "llvm.module.flags"; }
    static StringRef getCommandlineAttrName() { return "llvm.commandline"; }
    static StringRef getMmraAttrName() { return "llvm.mmra"; }
    /// Name of the attribute holding `!ro_after_init`. That named metadata is
    /// the marker a read-only-after-init pass writes to record that it has
    /// already run on a module -- `checkAndInsertMetadata` bails when it is
    /// present. The node is empty, so its presence is the whole content.
    static StringRef getRoAfterInitAttrName() { return "llvm.ro_after_init"; }
    /// Name of the attribute marking an operation as `!nosanitize`. LLVM
    /// permits that metadata on any instruction, so it has no single carrier op
    /// and rides as a discardable dialect attribute instead.
    static StringRef getNoSanitizeAttrName() { return "llvm.nosanitize"; }
    /// Name of the attribute holding `!llvm.loop` for a carrier that has no
    /// typed `loop_annotation` slot. LLVM permits the metadata on any
    /// instruction, so the four terminators that model it are not the whole
    /// story and anything else rides here rather than being dropped.
    static StringRef getLoopAnnotationAttrName() {
      return "llvm.loop_annotation";
    }
    /// Names of the two MemProf metadata kinds. LLVM allows them on any
    /// `CallBase`, which is more ops than the dialect has typed call slots for,
    /// so they ride as discardable dialect attributes.
    static StringRef getMemProfAttrName() { return "llvm.memprof"; }
    static StringRef getCallsiteAttrName() { return "llvm.callsite"; }
    /// Name of the attribute holding a value profile, `!prof !{!"VP", ...}`:
    /// the profiled kind, the total count, then each value with its count.
    /// LLVM puts it on indirect calls, memory intrinsics and vtable loads, so
    /// it rides as a discardable dialect attribute.
    static StringRef getValueProfileAttrName() { return "llvm.value_profile"; }
    /// Name of the attribute holding `!PGOFuncName`, the name a function's
    /// profile and the value profiles that target it know it by when that is
    /// not its symbol name (`<file>;<name>` for a local function).
    static StringRef getPGOFuncNameAttrName() { return "llvm.pgo_func_name"; }
    /// Names of the attributes holding the call-site attributes of an
    /// intrinsic call, which an intrinsic op has no slot for: the function
    /// attributes (in the `passthrough` encoding of `llvm.call`, every one of
    /// them), one parameter attribute dictionary per call argument, and the
    /// return attributes. A memory intrinsic's `align` and `noundef`, and the
    /// `nounwind` and `minsize` clang writes on every call it emits, live
    /// there.
    static StringRef getIntrinsicCallFnAttrsName() {
      return "llvm.call_fn_attrs";
    }
    static StringRef getIntrinsicCallArgAttrsName() {
      return "llvm.call_arg_attrs";
    }
    static StringRef getIntrinsicCallResAttrsName() {
      return "llvm.call_res_attrs";
    }
    /// Name of the attribute on a terminator holding, for each successor, the
    /// position of that edge among its successor's phi incoming values, or -1.
    /// The import records it where LLVM's incoming order differs from the
    /// order the export uses by default: the predecessors in reverse block
    /// order, each terminator's successors last to first, which is the block
    /// use-list order after parsing. Passes after the export read the order
    /// (`SimplifyCFG`'s merges, for one), and IRPGO's control-flow hash covers
    /// what they produce.
    static StringRef getPhiPositionsAttrName() { return "llvm.phi_positions"; }
    /// Name of the attribute on a terminator holding, for each successor, the
    /// fast-math flags of that successor's phis: an array of
    /// `#llvm.fastmath` per block argument, or a unit attribute when none
    /// has any. A block argument has no flags of its own, and a phi of
    /// floating-point type can carry them in LLVM IR. Every edge into the
    /// block holds the same flags.
    static StringRef getPhiFastmathAttrName() { return "llvm.phi_fastmath"; }
    /// Name of the attribute holding the compile units of `!llvm.dbg.cu` that
    /// nothing else in the module reaches. The import decides that by walking
    /// every location and attribute, not just subprograms; the export writes
    /// out whatever the attribute lists, reachable or not.
    static StringRef getDebugCompileUnitsAttrName() { return "llvm.dbg.cu"; }
    /// Name of the unit attribute that has the export list the compile units
    /// of `llvm.dbg.cu` first in `!llvm.dbg.cu`, in that attribute's order,
    /// rather than in the order the module first reaches them, which depends
    /// on what the module holds.
    static StringRef getDebugCompileUnitsInOrderAttrName() {
      return "llvm.dbg.cu_in_order";
    }
    /// Name of the attribute holding `!fpmath`, the accuracy a floating-point
    /// result is allowed to lose. Every FP-typed instruction can carry it.
    static StringRef getFPMathAttrName() { return "llvm.fpmath"; }
    /// Name of the attribute holding `!align`. Spelled with the `_metadata`
    /// suffix because plain `llvm.align` is already the *parameter* attribute,
    /// which the dialect verifies against a parameter type -- this one asserts
    /// the alignment of a pointer a load produced, which is a different claim.
    static StringRef getAlignMetadataAttrName() {
      return "llvm.align_metadata";
    }
    /// Name of the attribute holding `!noalias.addrspace`.
    static StringRef getNoaliasAddrspaceAttrName() {
      return "llvm.noalias_addrspace";
    }
    /// Name of the attribute holding `!make.implicit`, which marks a null
    /// check the target may fold into a faulting load. The node is valueless.
    static StringRef getMakeImplicitAttrName() {
      return "llvm.make_implicit";
    }
    /// Name of the attribute holding `!coro.outside.frame`, which keeps an
    /// alloca out of the coroutine frame. The node is valueless.
    static StringRef getCoroOutsideFrameAttrName() {
      return "llvm.coro_outside_frame";
    }
    /// Name of the attribute holding `!func_sanitize`, the two words a
    /// function's sanitizer prologue compares against.
    static StringRef getFuncSanitizeAttrName() {
      return "llvm.func_sanitize";
    }
    /// Name of the attribute holding `!mmra`, the memory model relaxation
    /// annotations. Each entry is a `(prefix, suffix)` string pair.
    static StringRef getMMRAAttrName() { return "llvm.mmra"; }
    /// Name of the attribute holding `!pcsections`. Each entry is a section
    /// name and the auxiliary constants that follow it. LLVM allows it on a
    /// function *and* on an instruction, so it rides as one discardable
    /// attribute and is written back from both export paths.
    static StringRef getPCSectionsAttrName() { return "llvm.pcsections"; }
    /// Name of the attribute holding `!callback`. Each entry is the callee
    /// argument index, the payload argument indices, and whether the callee is
    /// passed the remaining varargs. Function-only.
    static StringRef getCallbackAttrName() { return "llvm.callback"; }
    /// Name of the attribute holding a call's tail-call marker for an op that
    /// has no typed `TailCallKind` slot. `llvm.call` has one; the intrinsic ops
    /// do not, and LLVM marks intrinsic calls `tail` all the time.
    static StringRef getTailCallKindAttrName() {
      return "llvm.tail_call_kind";
    }
    /// Name of the attribute holding `!llvm.mem.parallel_loop_access`, the
    /// loops a memory operation is parallel with respect to. The legacy form
    /// of `!llvm.access.group`, but `Loop::isAnnotatedParallel` still falls
    /// back to it, and that check is all-or-nothing: one memory operation in
    /// the loop with neither and the whole loop stops counting as parallel.
    static StringRef getMemParallelLoopAccessAttrName() {
      return "llvm.mem_parallel_loop_access";
    }
    /// Name of the attribute holding `!DIAssignID`, the identity that links a
    /// store to the `#dbg_assign` record describing the assignment it makes.
    /// The node carries nothing; being the same node is the whole content, so
    /// the attribute is the identity itself.
    static StringRef getDIAssignIDAttrName() { return "llvm.dbg_assign_id"; }
    /// Name of the attribute holding `!llvm.preserve.access.index`, the record
    /// type a BPF CO-RE relocation is taken against. The node is a `DIType`.
    static StringRef getPreserveAccessIndexAttrName() {
      return "llvm.preserve_access_index";
    }

    /// LLVM's names for the call-site function attributes that `llvm.call` and
    /// `llvm.invoke` model with a typed slot.
    ///
    /// Both the import and the export of `passthrough` on those ops consult
    /// this: the import leaves these out of the bag so the same fact is never
    /// reachable two ways, and the export rejects them if a hand-written module
    /// puts one there anyway. It lives here so the two cannot disagree, which
    /// is the failure this whole family of bugs keeps taking.
    static constexpr StringLiteral kModelledCallSiteAttrNames[] = {
          // Modelled on the call because a pass asks about them.
          "allocsize",  "alwaysinline", "builtin",  "cold",
          "convergent", "inlinehint",   "memory",   "minsize",
          "noinline",   "nomerge",      "noreturn", "nounwind",
          "optsize",    "willreturn",
          // Modelled because `llvm.func` models them: the same LLVM attribute
          // must not be a structured attribute on one carrier and a string in
          // a bag on the other. This half is exactly the set the function
          // import calls explicit, and a `static_assert` in `ModuleImport.cpp`
          // checks that it stays so.
          "optnone",    "uwtable",      "vscale_range",
          "frame-pointer", "target-features", "target-cpu", "tune-cpu",
          "unsafe-fp-math", "no-infs-fp-math", "no-nans-fp-math",
          "approx-func-fp-math", "no-signed-zeros-fp-math",
          "denormal-fp-math", "denormal-fp-math-f32", "fp-contract",
          "instrument-function-entry", "instrument-function-exit",
          "aarch64_pstate_sm_enabled", "aarch64_pstate_sm_body",
          "aarch64_pstate_sm_compatible", "aarch64_new_za", "aarch64_in_za",
          "aarch64_out_za", "aarch64_inout_za", "aarch64_preserves_za"};

    static ArrayRef<StringLiteral> getModelledCallSiteAttrNames() {
      return kModelledCallSiteAttrNames;
    }

    /// Names of llvm parameter attributes.
    static StringRef getAlignAttrName() { return "llvm.align"; }
    static StringRef getAllocAlignAttrName() { return "llvm.allocalign"; }
    static StringRef getAllocatedPointerAttrName() { return "llvm.allocptr"; }
    static StringRef getByValAttrName() { return "llvm.byval"; }
    static StringRef getByRefAttrName() { return "llvm.byref"; }
    static StringRef getCapturesAttrName() { return "llvm.captures"; }
    static StringRef getImmArgAttrName() { return "llvm.immarg"; }
    static StringRef getInitializesAttrName() { return "llvm.initializes"; }
    static StringRef getNoExtAttrName() { return "llvm.noext"; }
    static StringRef getSwiftAsyncAttrName() { return "llvm.swiftasync"; }
    static StringRef getSwiftErrorAttrName() { return "llvm.swifterror"; }
    static StringRef getSwiftSelfAttrName() { return "llvm.swiftself"; }
    static StringRef getNoUndefAttrName() { return "llvm.noundef"; }
    static StringRef getDereferenceableAttrName() { return "llvm.dereferenceable"; }
    static StringRef getDereferenceableOrNullAttrName() { return "llvm.dereferenceable_or_null"; }
    static StringRef getElementTypeAttrName() { return "llvm.elementtype"; }
    static StringRef getInAllocaAttrName() { return "llvm.inalloca"; }
    static StringRef getInRegAttrName() { return "llvm.inreg"; }
    static StringRef getNestAttrName() { return "llvm.nest"; }
    static StringRef getNoAliasAttrName() { return "llvm.noalias"; }
    static StringRef getNoCaptureAttrName() { return "llvm.nocapture"; }
    static StringRef getNoFPClassAttrName() { return "llvm.nofpclass"; }
    static StringRef getNoFreeAttrName() { return "llvm.nofree"; }
    static StringRef getNonNullAttrName() { return "llvm.nonnull"; }
    static StringRef getPreallocatedAttrName() { return "llvm.preallocated"; }
    static StringRef getRangeAttrName() { return "llvm.range"; }
    static StringRef getReadonlyAttrName() { return "llvm.readonly"; }
    static StringRef getReturnedAttrName() { return "llvm.returned"; }
    static StringRef getSExtAttrName() { return "llvm.signext"; }
    static StringRef getStackAlignmentAttrName() { return "llvm.alignstack"; }
    static StringRef getStructRetAttrName() { return "llvm.sret"; }
    static StringRef getWriteOnlyAttrName() { return "llvm.writeonly"; }
    static StringRef getWritableAttrName() { return "llvm.writable"; }
    static StringRef getDeadOnUnwindAttrName() { return "llvm.dead_on_unwind"; }
    static StringRef getDeadOnReturnAttrName() { return "llvm.dead_on_return"; }
    static StringRef getZExtAttrName() { return "llvm.zeroext"; }
    static StringRef getOpBundleSizesAttrName() { return "op_bundle_sizes"; }
    static StringRef getOpBundleTagsAttrName() { return "op_bundle_tags"; }
    // TODO Restrict the usage of this to parameter attributes once there is an
    // alternative way of modeling memory effects on FunctionOpInterface.
    /// Name of the attribute that will cause the creation of a readnone memory
    /// effect when lowering to the LLVMDialect.
    static StringRef getReadnoneAttrName() { return "llvm.readnone"; }

    /// Verifies if the given string is a well-formed data layout descriptor.
    /// Uses `reportError` to report errors.
    static LogicalResult verifyDataLayoutString(
        StringRef descr, llvm::function_ref<void (const Twine &)> reportError);

    /// Name of the target triple attribute.
    static StringRef getTargetTripleAttrName() { return "llvm.target_triple"; }

    /// Name of the C wrapper emission attribute.
    static StringRef getEmitCWrapperAttrName() {
      return "llvm.emit_c_interface";
    }

    /// Name of the module level assembly attribute.
    static StringRef getModuleLevelAsmAttrName() { return "llvm.module_asm"; }

    /// Name of the dependent libraries attribute.
    static StringRef getDependentLibrariesAttrName() {
      return "llvm.dependent_libraries";
    }

    /// Names of known llvm module flag keys.
    static StringRef getModuleFlagKeyCGProfileName() {
      return "CG Profile";
    }
    static StringRef getModuleFlagKeyProfileSummaryName() {
      return "ProfileSummary";
    }

    /// Returns `true` if the given type is compatible with the LLVM dialect.
    static bool isCompatibleType(Type);


    Type parseType(DialectAsmParser &p) const override;
    void printType(Type, DialectAsmPrinter &p) const override;

  private:
    /// Verifies a parameter attribute attached to a parameter of type
    /// paramType.
    LogicalResult verifyParameterAttribute(Operation *op,
                                           Type paramType,
                                           NamedAttribute paramAttr);

    /// Register all types.
    void registerTypes();

    /// A cache storing compatible LLVM types that have been verified. This
    /// can save us lots of verification time if there are many occurrences
    /// of some deeply-nested aggregate types in the program.
    ThreadLocalCache<DenseSet<Type>> compatibleTypes;

    /// Register the attributes of this dialect.
    void registerAttributes();
  }];
}

#endif  // LLVMIR_DIALECT
