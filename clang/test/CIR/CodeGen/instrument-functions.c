// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o %t.cir %s -finstrument-functions
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.ll %s -finstrument-functions
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t-bare.ll %s -finstrument-function-entry-bare
// RUN: FileCheck --check-prefix=BARE %s < %t-bare.ll

int test1(int x) {
  return x;
}

int test2(int) __attribute__((no_instrument_function));
int test2(int x) {
  return x;
}

// test1 gets instrument attributes; test2 (no_instrument_function) does not.
// The attribute groups are defined at the top of CIR output.
// CIR-DAG: #fn_attr = #cir<extra({"instrument-function-entry" = "__cyg_profile_func_enter", "instrument-function-exit" = "__cyg_profile_func_exit"
// CIR-DAG: #fn_attr1 = #cir<extra({"min-legal-vector-width" = "0", "no-trapping-math"
// CIR: cir.func {{.*}} @test1{{.*}} extra(#fn_attr)
// CIR: cir.func {{.*}} @test2{{.*}} extra(#fn_attr1)

// LLVM-LABEL: define {{.*}} @test1
// LLVM: call void @__cyg_profile_func_enter(ptr @test1
// LLVM: call void @__cyg_profile_func_exit(ptr @test1

// LLVM-LABEL: define {{.*}} @test2
// LLVM-NOT: __cyg_profile_func_enter
// LLVM-NOT: __cyg_profile_func_exit
// LLVM: ret

// BARE: @test1({{.*}}) #[[ATTR1:[0-9]+]]
// BARE: @test2({{.*}}) #[[ATTR2:[0-9]+]]
// BARE: attributes #[[ATTR1]] =
// BARE-SAME: "instrument-function-entry-inlined"="__cyg_profile_func_enter_bare"
// BARE: attributes #[[ATTR2]] =
// BARE-NOT: "instrument-function-entry"
