// RUN: %clang_cc1 -fopenmp -fcxx-exceptions -fexceptions -x c++ \
// RUN:   -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -verify %s -o %t.cir

// An exception raised inside an OpenMP region that OpenMPIRBuilder outlines via
// llvm::CodeExtractor (parallel/task/target/teams/taskloop) escapes the region
// when its unwind chain reaches an EH scope established *outside* the region --
// e.g. a C++ try wrapping the region. An in-region cleanup (a RAII destructor
// here) sits between the throw/call and that outer handler, so the innermost EH
// scope is the in-region cleanup, not the outer handler. The cleanup still
// re-raises and the exception unwinds out of the region, building the same
// malformed cross-region block successor that crashes collectUnreachable's
// block-successor BFS during CIR-to-LLVM lowering. Walk the EH chain past
// in-region cleanups so the gate still fires; diagnose it as not-yet-implemented
// instead of crashing.

struct G {
  ~G();
};
void mayThrow();

// Throwing call escaping through an in-region RAII cleanup to an outer try.
void callEscapesThroughCleanup() {
  try {
#pragma omp parallel
    {
      G g;
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      mayThrow();
    }
  } catch (int) {
  }
}

// Direct throw escaping through an in-region RAII cleanup to an outer try.
void throwEscapesThroughCleanup() {
  try {
#pragma omp parallel
    {
      G g;
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      throw 1;
    }
  } catch (int) {
  }
}

// A handler established *inside* the region catches the exception before it can
// escape, even though an in-region cleanup is also present. No cross-region edge
// is built and no diagnostic is expected. -verify fails if one is emitted.
void handledInsideRegionWithCleanup() {
  try {
#pragma omp parallel
    {
      G g;
      try {
        mayThrow();
      } catch (...) {
      }
    }
  } catch (int) {
  }
}

// An in-region cleanup with no enclosing handler: the exception unwinds through
// the cleanup and escapes to std::terminate, which stays inside the region. No
// cross-region edge is built and no diagnostic is expected.
void cleanupNoHandler() {
#pragma omp parallel
  {
    G g;
    throw 1;
  }
}
