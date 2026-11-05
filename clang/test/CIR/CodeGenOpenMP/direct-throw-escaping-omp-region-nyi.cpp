// RUN: %clang_cc1 -fopenmp -fcxx-exceptions -fexceptions -x c++ \
// RUN:   -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -verify %s -o %t.cir

// A direct C++ throw inside an OpenMP region that OpenMPIRBuilder outlines via
// llvm::CodeExtractor (parallel/task/target/teams/taskloop) may have an active
// unwind destination -- e.g. a C++ try wrapping the region -- established
// *outside* the region. CIRGen would tie the throw's unwind edge to that
// enclosing cir.try, whose unwind region sits outside the omp op. When
// FlattenCFG lowers the try, the resulting cross-region block successor is
// malformed and previously crashed collectUnreachable's block-successor BFS in
// the CIR-to-LLVM lowering. A direct throw reaches CIRGen through emitCXXThrowExpr
// (not emitCall), so the throwing-call gate does not cover it; diagnose it as
// not-yet-implemented instead of crashing.

void tryWrapsParallelDirectThrow() {
  try {
#pragma omp parallel
    {
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      throw 1;
    }
  } catch (int) {
  }
}

void tryWrapsParallelForDirectThrow() {
  try {
#pragma omp parallel for
    for (int i = 0; i < 4; ++i)
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      throw 1;
  } catch (int) {
  }
}

// A direct throw with no enclosing handler is not an invoke; it simply escapes
// to std::terminate. No cross-region unwind edge is built and no diagnostic is
// expected. -verify fails if one is emitted.
void directThrowNoHandler() {
#pragma omp parallel
  {
    throw 1;
  }
}
