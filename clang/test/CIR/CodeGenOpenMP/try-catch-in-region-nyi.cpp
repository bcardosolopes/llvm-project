// RUN: %clang_cc1 -fopenmp -fcxx-exceptions -fexceptions -x c++ \
// RUN:   -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -verify %s -o %t.cir

// A typed catch clause lowers to llvm.eh.typeid.for. When it is emitted inside
// an OpenMP region that OpenMPIRBuilder outlines via llvm::CodeExtractor
// (parallel/task/target/teams/taskloop), CodeExtractor refuses to extract the
// region (its PR39545 guard rejects eh.typeid.for), which previously aborted
// the compile in OpenMPIRBuilder::finalize. Diagnose it as not-yet-implemented
// instead of crashing. catch-all (`catch(...)`) handlers emit no eh.typeid.for
// and remain supported.

void mayThrow();

void typedCatchInParallel() {
#pragma omp parallel
  {
    // expected-error@+1 {{ClangIR code gen Not Yet Implemented: typed catch clause inside an OpenMP region}}
    try {
      throw 1;
    } catch (int) {
    }
  }
}

void typedCatchInParallelFor() {
#pragma omp parallel for
  for (int i = 0; i < 4; ++i) {
    // expected-error@+1 {{ClangIR code gen Not Yet Implemented: typed catch clause inside an OpenMP region}}
    try {
      mayThrow();
    } catch (int) {
    }
  }
}

// catch-all handlers are still supported inside an outlined region: no
// eh.typeid.for is generated, so CodeExtractor accepts the region. No
// diagnostic is expected here; -verify fails if one is emitted.
void catchAllInParallel() {
#pragma omp parallel
  {
    try {
      mayThrow();
    } catch (...) {
    }
  }
}

// An in-region typed catch whose body rethrows (`throw;`), wrapped by an outer
// (non-region) try/catch, drives the diagnosed try through its failure path.
// The in-region typed catch is diagnosed as NYI; that failure then propagates
// out to the enclosing non-region try, which must abort emission cleanly. Its
// enterCXXTryStmt already pushed an EHCatchScope, so it is popped on the bail
// path -- otherwise the enclosing try's LexicalScope cleanup ran popCleanupBlock
// over the still-live catch scope and asserted "top not a cleanup!".
void rethrowInParallelUnderOuterTry() {
  try {
#pragma omp parallel
    {
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: typed catch clause inside an OpenMP region}}
      try {
        mayThrow();
      } catch (double) {
        throw;
      }
    }
  } catch (int) {
  }
}
