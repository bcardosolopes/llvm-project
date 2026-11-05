// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Global object with constructor — comdat and global_ctors generation.

// CIR: $_ZN11GlobalClassC1Ei = comdat any
// CIR: @global_obj = global %struct.GlobalClass zeroinitializer
// CIR: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
// CIR: define linkonce_odr void @_ZN11GlobalClassC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat
// CIR: define internal void @__cxx_global_var_init()
// CIR:   call void @_ZN11GlobalClassC1Ei(ptr noundef nonnull align 4 dereferenceable(4) @global_obj, i32 noundef 42)

// OGCG: $_ZN11GlobalClassC1Ei = comdat any
// OGCG: @global_obj = global %struct.GlobalClass zeroinitializer
// OGCG: @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
// OGCG: define internal void @__cxx_global_var_init()
// OGCG: define linkonce_odr void @_ZN11GlobalClassC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct GlobalClass {
    int value;
    GlobalClass(int v) : value(v) {}
};

GlobalClass global_obj(42);

int test() {
    return global_obj.value;
}
