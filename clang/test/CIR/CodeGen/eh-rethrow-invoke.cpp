// RUN: %clang -S -emit-llvm -O0 -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang -S -emit-llvm -O0 -fno-clangir %s -o - | FileCheck %s --check-prefix=OGCG

// Verify the invoke + catch-all rethrow shape in both pipelines.
// This is a semantic replacement for the old textual-diff divergence test.

extern void g();

int f() {
  try {
    g();
  } catch (...) {
    throw;
  }
  return 0;
}

// LLVM-LABEL: define{{.*}} @_Z1fv()
// LLVM: invoke void @_Z1gv()
// LLVM: landingpad { ptr, i32 }
// LLVM: catch ptr null
// LLVM: call ptr @__cxa_begin_catch(ptr
// LLVM: invoke void @__cxa_rethrow()
// LLVM: landingpad { ptr, i32 }
// LLVM: cleanup
// LLVM: invoke void @__cxa_end_catch()
// LLVM: resume { ptr, i32 }

// OGCG-LABEL: define{{.*}} @_Z1fv()
// OGCG: invoke void @_Z1gv()
// OGCG: landingpad { ptr, i32 }
// OGCG: catch ptr null
// OGCG: call ptr @__cxa_begin_catch(ptr
// OGCG: invoke void @__cxa_rethrow()
// OGCG: landingpad { ptr, i32 }
// OGCG: cleanup
// OGCG: invoke void @__cxa_end_catch()
// OGCG: resume { ptr, i32 }
