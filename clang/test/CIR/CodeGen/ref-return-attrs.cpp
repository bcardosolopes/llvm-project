// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// Verify nonnull/align/dereferenceable on reference return values.

int global_x = 42;

int& ref_return() { return global_x; }
// CIR: cir.func {{.*}} @_Z10ref_returnv() -> (!cir.ptr<!s32i> {cir.align = 4 : i64, cir.dereferenceable = 4 : i64, cir.nonnull, cir.noundef})
// LLVM: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z10ref_returnv()
// OGCG: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z10ref_returnv()

const int& const_ref_return() { return global_x; }
// CIR: cir.func {{.*}} @_Z16const_ref_returnv() -> (!cir.ptr<!s32i> {cir.align = 4 : i64, cir.dereferenceable = 4 : i64, cir.nonnull, cir.noundef})
// LLVM: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z16const_ref_returnv()
// OGCG: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z16const_ref_returnv()

struct S { double d; int i; };
S global_s;
S& struct_ref_return() { return global_s; }
// CIR: cir.func {{.*}} @_Z17struct_ref_returnv() -> (!cir.ptr<!rec_S> {cir.align = 8 : i64, cir.dereferenceable = 16 : i64, cir.nonnull, cir.noundef})
// LLVM: define dso_local noundef nonnull align 8 dereferenceable(16) ptr @_Z17struct_ref_returnv()
// OGCG: define dso_local noundef nonnull align 8 dereferenceable(16) ptr @_Z17struct_ref_returnv()

char global_c = 0;
char& char_ref_return() { return global_c; }
// CIR: cir.func {{.*}} @_Z15char_ref_returnv() -> (!cir.ptr<!s8i> {cir.align = 1 : i64, cir.dereferenceable = 1 : i64, cir.nonnull, cir.noundef})
// LLVM: define dso_local noundef nonnull align 1 dereferenceable(1) ptr @_Z15char_ref_returnv()
// OGCG: define dso_local noundef nonnull align 1 dereferenceable(1) ptr @_Z15char_ref_returnv()
