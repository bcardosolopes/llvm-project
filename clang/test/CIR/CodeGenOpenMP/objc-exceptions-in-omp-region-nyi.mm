// RUN: %clang_cc1 -fopenmp -fobjc-arc -fobjc-runtime=ios-15.0 \
// RUN:   -fobjc-exceptions -fexceptions -x objective-c++ \
// RUN:   -triple arm64-apple-ios15.0 -fclangir -emit-cir -verify %s -o %t.cir

// Objective-C @throw and @try bypass the C++ emitCXXThrowExpr / emitCXXTryStmt
// OpenMP-escape gates because they lower through objc_exception_throw and the
// ObjC @try emitter instead. This test covers the two ObjC analogues of those
// gates for a region that OpenMPIRBuilder outlines via llvm::CodeExtractor
// (parallel/task/target/teams/taskloop).

@class NSException;

// An ObjC @throw whose active unwind destination is an EH scope established
// *outside* the region (here a C++ try wrapping the region) would tie the
// throw's unwind edge to that enclosing, out-of-region cir.try. FlattenCFG then
// produces a malformed cross-region block successor that previously crashed
// collectUnreachable's block-successor BFS during CIR-to-LLVM lowering.
void tryWrapsParallelObjCThrow(id exc) {
  try {
#pragma omp parallel
    {
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      @throw exc;
    }
  } catch (...) {
  }
}

// A typed ObjC @catch (an `id` catch is NOT a catch-all on the Darwin runtime)
// lowers to a handler that emits llvm.eh.typeid.for. Inside an outlined region
// CodeExtractor refuses to extract it (its PR39545 guard), previously aborting
// in OMPIRBuilder::finalize.
void typedCatchInParallel(id exc) {
#pragma omp parallel
  {
    // expected-error@+1 {{ClangIR code gen Not Yet Implemented: typed catch clause inside an OpenMP region}}
    @try {
      @throw exc;
    } @catch (id e) {
    }
  }
}

// An ObjC @throw with no enclosing handler builds no cross-region unwind edge:
// the in-region synthetic cleanup try keeps its landing pad and cir.resume in
// the region. No diagnostic is expected; -verify fails if one is emitted.
void objcThrowNoHandler(id exc) {
#pragma omp parallel
  {
    @throw exc;
  }
}

// A @try with only @finally (no @catch) emits no eh.typeid.for and stays
// supported inside an outlined region. No diagnostic is expected.
void finallyOnlyInParallel(id exc) {
#pragma omp parallel
  {
    @try {
      @throw exc;
    } @finally {
    }
  }
}
