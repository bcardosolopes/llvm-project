// RUN: %clang_cc1 -fopenmp -fcxx-exceptions -fexceptions -x c++ \
// RUN:   -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -verify %s -o %t.cir

// A potentially-throwing call inside an OpenMP region that OpenMPIRBuilder
// outlines via llvm::CodeExtractor (parallel/task/target/teams/taskloop) may
// have an active unwind destination -- e.g. a C++ try wrapping the region --
// established *outside* the region. CIRGen would build the call's unwind edge
// as a `cir.call exception ... cleanup` targeting that enclosing cir.try, whose
// unwind region sits outside the omp op. When FlattenCFG lowers the try, the
// resulting cross-region block successor is malformed and previously crashed
// collectUnreachable's block-successor BFS in the CIR-to-LLVM lowering. Diagnose
// it as not-yet-implemented instead of crashing.

void mayThrow();

void tryWrapsParallelFor() {
  try {
#pragma omp parallel for
    for (int i = 0; i < 4; ++i)
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      mayThrow();
  } catch (int) {
  }
}

void tryWrapsParallel() {
  try {
#pragma omp parallel
    {
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      mayThrow();
    }
  } catch (int) {
  }
}

// A handler established *inside* the region catches the exception before it
// escapes, so no cross-region unwind edge is built and no diagnostic is
// expected. -verify fails if one is emitted.
void handledInsideParallel() {
#pragma omp parallel
  {
    try {
      mayThrow();
    } catch (...) {
    }
  }
}

// With no enclosing handler at all the throwing call is not an invoke; the
// exception simply escapes to std::terminate. No diagnostic is expected.
void noHandlerInParallel() {
#pragma omp parallel
  { mayThrow(); }
}
