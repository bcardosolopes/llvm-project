// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm -fexceptions %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// CIR emits global init functions in wrong order for dependent variables.
// When y depends on x and z depends on y, z's init must come after y's init.
// CIR now correctly matches the traditional codegen: _GLOBAL__sub_I calls init functions in
// declaration order (x, y, z), which respects the dependency chain.

int f();

static int x = f();
static int y = x - 1;
static int z = ~y;

int test() { return z; }

// Verify _GLOBAL__sub_I calls init functions in the correct dependency order.
// Despite the numbering (.1 is z's init, .2 is y's init), the calling order
// must be: x first, then y=x-1, then z=~y.

// LLVM-LABEL: define internal void @_GLOBAL__sub_I
// LLVM:         call void @__cxx_global_var_init()
// LLVM-NEXT:    call void @__cxx_global_var_init.2()
// LLVM-NEXT:    call void @__cxx_global_var_init.1()

// OGCG-LABEL: define internal void @_GLOBAL__sub_I
// OGCG:         call void @__cxx_global_var_init()
// OGCG-NEXT:    call void @__cxx_global_var_init.2()
// OGCG-NEXT:    call void @__cxx_global_var_init.1()
