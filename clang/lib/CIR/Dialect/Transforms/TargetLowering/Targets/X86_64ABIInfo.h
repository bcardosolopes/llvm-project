#include "ABIInfo.h"
#include "clang/CIR/Target/x86.h"

namespace cir {
class X86_64ABIInfo : public cir::ABIInfo {
  using Class = cir::X86ArgClass;

  /// Implement the X86_64 ABI merging algorithm.
  ///
  /// Merge an accumulating classification \arg Accum with a field
  /// classification \arg Field.
  ///
  /// \param Accum - The accumulating classification. This should
  /// always be either NoClass or the result of a previous merge
  /// call. In addition, this should never be Memory (the caller
  /// should just return Memory for the aggregate).
  static Class merge(Class Accum, Class Field);

  /// Implement the X86_64 ABI post merging algorithm.
  ///
  /// Post merger cleanup, reduces a malformed Hi and Lo pair to
  /// final MEMORY or SSE classes when necessary.
  ///
  /// \param AggregateSize - The size of the current aggregate in
  /// the classification process.
  ///
  /// \param Lo - The classification for the parts of the type
  /// residing in the low word of the containing object.
  ///
  /// \param Hi - The classification for the parts of the type
  /// residing in the higher words of the containing object.
  ///
  void postMerge(unsigned AggregateSize, Class &Lo, Class &Hi) const;

  /// Determine the x86_64 register classes in which the given type T should be
  /// passed.
  ///
  /// \param Lo - The classification for the parts of the type
  /// residing in the low word of the containing object.
  ///
  /// \param Hi - The classification for the parts of the type
  /// residing in the high word of the containing object.
  ///
  /// \param OffsetBase - The bit offset of this type in the
  /// containing object.  Some parameters are classified different
  /// depending on whether they straddle an eightbyte boundary.
  ///
  /// \param isNamedArg - Whether the argument in question is a "named"
  /// argument, as used in AMD64-ABI 3.5.7.
  ///
  /// \param IsRegCall - Whether the calling conversion is regcall.
  ///
  /// If a word is unused its result will be NoClass; if a type should
  /// be passed in Memory then at least the classification of \arg Lo
  /// will be Memory.
  ///
  /// The \arg Lo class will be NoClass iff the argument is ignored.
  ///
  /// If the \arg Lo class is ComplexX87, then the \arg Hi class will
  /// also be ComplexX87.
  void classify(mlir::Type T, uint64_t OffsetBase, Class &Lo, Class &Hi,
                bool isNamedArg, bool IsRegCall = false) const;

  mlir::Type GetSSETypeAtOffset(mlir::Type IRType, unsigned IROffset,
                                mlir::Type SourceTy,
                                unsigned SourceOffset) const;

  mlir::Type GetINTEGERTypeAtOffset(mlir::Type DestTy, unsigned IROffset,
                                    mlir::Type SourceTy,
                                    unsigned SourceOffset) const;

  /// The ABI specifies that a value should be passed in a full vector XMM/YMM
  /// register. Pick a CIR type that will be passed as a vector register.
  mlir::Type GetByteVectorType(mlir::Type Ty) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be passed in memory.
  ///
  /// \param freeIntRegs - The number of free integer registers remaining
  /// available.
  /// Returns true if the given vector type is "illegal" from the perspective
  /// of the x86_64 ABI — either too small (<=64 bits, uses integer registers)
  /// or too large (exceeds the native vector size for the current AVX level).
  /// Such vectors must be passed in memory, not in SSE/AVX registers.
  bool isIllegalVectorType(mlir::Type Ty) const;

  unsigned getIndirectBitIntAlign(mlir::Type ty, bool isBitIntType) const;

  ::cir::ABIArgInfo getIndirectResult(mlir::Type ty, bool isBitIntType,
                                      bool isMatrixType,
                                      unsigned freeIntRegs) const;

  /// getIndirectReturnResult - Give a source type \arg Ty, return a suitable
  /// result such that the return value will be returned via a hidden pointer
  /// argument (sret).
  ::cir::ABIArgInfo getIndirectReturnResult(mlir::Type ty,
                                            bool isBitIntType) const;

  /// The 0.98 ABI revision clarified a lot of ambiguities,
  /// unfortunately in ways that were not always consistent with
  /// certain previous compilers.  In particular, platforms which
  /// required strict binary compatibility with older versions of GCC
  /// may need to exempt themselves.
  bool honorsRevision0_98() const {
    return !getTarget().getTriple().isOSDarwin();
  }

  /// GCC classifies <1 x long long> as SSE but some platform ABIs choose to
  /// classify it as INTEGER (for compatibility with older clang compilers).
  bool classifyIntegerMMXAsSSE() const {
    // Clang <= 3.8 did not do this.
    if (getContext().getLangOpts().getClangABICompat() <=
        clang::LangOptions::ClangABI::Ver3_8)
      return false;

    const llvm::Triple &triple = getTarget().getTriple();
    if (triple.isOSDarwin() || triple.isPS() || triple.isOSFreeBSD())
      return false;
    return true;
  }

  /// GCC classifies vectors of __int128 as memory.
  bool passInt128VectorsInMem() const {
    // Clang <= 9.0 did not do this.
    if (getContext().getLangOpts().getClangABICompat() <=
        clang::LangOptions::ClangABI::Ver9)
      return false;

    const llvm::Triple &triple = getTarget().getTriple();
    return triple.isOSLinux() || triple.isOSNetBSD();
  }

  /// Whether a C++ record type greater than 128 bits should be passed in
  /// memory when it appears as a base class subobject. Clang <= 20.0 did not
  /// apply this check to base classes, and PlayStation targets never do
  /// (OGCG X86.cpp:1352-1360).
  bool returnCXXRecordGreaterThan128InMem() const {
    if (getContext().getLangOpts().getClangABICompat() <=
            clang::LangOptions::ClangABI::Ver20 ||
        getTarget().getTriple().isPS())
      return false;
    return true;
  }

  ::cir::X86AVXABILevel AVXLevel;

public:
  X86_64ABIInfo(LowerTypes &CGT, cir::X86AVXABILevel AVXLevel)
      : ABIInfo(CGT), AVXLevel(AVXLevel) {}

  cir::ABIArgInfo classifyReturnType(mlir::Type RetTy,
                                     bool isBitIntType = false) const;

  cir::ABIArgInfo classifyArgumentType(mlir::Type Ty, bool isBitIntType,
                                       bool isMatrixType,
                                       unsigned freeIntRegs,
                                       unsigned &neededInt, unsigned &neededSSE,
                                       bool isNamedArg, bool IsRegCall) const;

  /// Classify a struct type for the regcall calling convention by recursively
  /// decomposing it field-by-field and tallying register needs.
  cir::ABIArgInfo classifyRegCallStructType(mlir::Type Ty, unsigned &neededInt,
                                            unsigned &neededSSE,
                                            unsigned &maxVectorWidth) const;

  cir::ABIArgInfo classifyRegCallStructTypeImpl(mlir::Type Ty,
                                                unsigned &neededInt,
                                                unsigned &neededSSE,
                                                unsigned &maxVectorWidth) const;

  bool isX86VectorTypeForVectorCall(mlir::Type Ty) const;

  bool isHomogeneousAggregateForVectorCall(mlir::Type Ty, mlir::Type &Base,
                                           uint64_t &Members) const;

  cir::ABIArgInfo classifyWindowsType(mlir::Type Ty, unsigned &FreeSSERegs,
                                      bool IsReturnType,
                                      bool IsVectorCall) const;

  cir::ABIArgInfo reclassifyHvaArgForVectorCall(
      mlir::Type Ty, unsigned &FreeSSERegs,
      const cir::ABIArgInfo &Current) const;

  void computeInfo(LowerFunctionInfo &FI) const override;
};

} // namespace cir
