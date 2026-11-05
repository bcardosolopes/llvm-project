// RUN: %clang_cc1 -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-nvcall -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-nvcall,cfi-cast-strict -fclangir -emit-llvm -o - %s | FileCheck --check-prefix=CHECK-STRICT %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-nvcall -fclangir -emit-cir -o - %s | FileCheck --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux -fvisibility=hidden -fsanitize=cfi-nvcall,cfi-cast-strict -fclangir -emit-cir -o - %s | FileCheck --check-prefix=CIR-STRICT %s

struct A {
  virtual void f();
};

struct B : A {
  int i;
  void g();
};

struct C : A {
  void g();
};

// CHECK-LABEL: define hidden void @bg(
// CHECK: [[THIS:%.*]] = load ptr, ptr {{%.*}}, align 8
// CHECK-NEXT: [[VTABLE:%.*]] = load ptr, ptr [[THIS]], align 8
// CHECK-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[VTABLE]], metadata !"_ZTS1B"), !nosanitize [[NOSAN:![0-9]+]]
// CHECK-NEXT: [[VALID:%.*]] = call i1 @llvm.type.test(ptr [[VTABLE]], metadata !"all-vtables"), !nosanitize [[NOSAN]]
// CHECK-NEXT: br i1 [[TEST]], label %[[CONT:.*]], label %[[FAIL:.*]], !prof [[PROF:![0-9]+]], !nosanitize [[NOSAN]]
// CHECK: [[FAIL]]:
// CHECK-NEXT: [[VTINT:%.*]] = ptrtoint ptr [[VTABLE]] to i64, !nosanitize [[NOSAN]]
// CHECK-NEXT: [[VALIDINT:%.*]] = zext i1 [[VALID]] to i64, !nosanitize [[NOSAN]]
// CHECK-NEXT: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, i64 [[VTINT]], i64 [[VALIDINT]]){{.*}}!nosanitize [[NOSAN]]
// CHECK-NEXT: unreachable, !nosanitize [[NOSAN]]
// CHECK: [[CONT]]:
// CHECK-NEXT: call void @_ZN1B1gEv(ptr noundef nonnull align 8 dereferenceable(12) [[THIS]]){{$}}
// CHECK-NEXT: ret void
// CIR: cir.call @_ZN1B1gEv({{.*}}) {cir.nosanitize, cir.ubsan.source_column = 3 : i32, cir.ubsan.source_file = "{{.*}}clang/test/CIR/CodeGenCXX/cfi-nvcall.cpp", cir.ubsan.source_line = 35 : i32, cir.ubsan.type_check_kind = 1 : i8, cir.ubsan.type_info = 0 : i16, cir.ubsan.type_kind = -1 : i16, cir.ubsan.type_name = "'B'", cir.virtual.cfi_vcall_kind = "abort", cir.virtual.typeid = "_ZTS1B", cir.virtual.typetest_kind = "hidden"}
extern "C" void bg(B *b) {
  b->g();
}

// CHECK-LABEL: define hidden void @cg(
// CHECK: [[THIS:%.*]] = load ptr, ptr {{%.*}}, align 8
// CHECK-NEXT: [[VTABLE:%.*]] = load ptr, ptr [[THIS]], align 8
// CHECK-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[VTABLE]], metadata !"_ZTS1A"), !nosanitize [[NOSAN2:![0-9]+]]
// CHECK: [[VALID2:%.*]] = call i1 @llvm.type.test(ptr [[VTABLE]], metadata !"all-vtables"), !nosanitize [[NOSAN2]]
// CHECK-STRICT-LABEL: define hidden void @cg(
// CHECK-STRICT: [[THIS:%.*]] = load ptr, ptr {{%.*}}, align 8
// CHECK-STRICT-NEXT: [[VTABLE:%.*]] = load ptr, ptr [[THIS]], align 8
// CHECK-STRICT-NEXT: [[TEST:%.*]] = call i1 @llvm.type.test(ptr [[VTABLE]], metadata !"_ZTS1C"), !nosanitize [[NOSAN3:![0-9]+]]
// CHECK: [[FAIL:[A-Za-z0-9._]+]]:
// CHECK-NEXT: [[VTINT2:%.*]] = ptrtoint ptr [[VTABLE]] to i64, !nosanitize [[NOSAN2]]
// CHECK-NEXT: [[VALIDINT2:%.*]] = zext i1 [[VALID2:%.*]] to i64, !nosanitize [[NOSAN2]]
// CHECK-NEXT: call void @__ubsan_handle_cfi_check_fail_abort(ptr {{.*}}, i64 [[VTINT2]], i64 [[VALIDINT2]]){{.*}}!nosanitize [[NOSAN2]]
// CHECK-NEXT: unreachable, !nosanitize [[NOSAN2]]
// CHECK: [[CONT2:.*]]:
// CHECK-NEXT: call void @_ZN1C1gEv(ptr noundef nonnull align 8 dereferenceable(8) [[THIS]]){{$}}
// CHECK-NEXT: ret void
// CIR: cir.call @_ZN1C1gEv({{.*}}) {cir.nosanitize, cir.ubsan.source_column = 3 : i32, cir.ubsan.source_file = "{{.*}}clang/test/CIR/CodeGenCXX/cfi-nvcall.cpp", cir.ubsan.source_line = 58 : i32, cir.ubsan.type_check_kind = 1 : i8, cir.ubsan.type_info = 0 : i16, cir.ubsan.type_kind = -1 : i16, cir.ubsan.type_name = "'A'", cir.virtual.cfi_vcall_kind = "abort", cir.virtual.typeid = "_ZTS1A", cir.virtual.typetest_kind = "hidden"}
// CIR-STRICT: cir.call @_ZN1C1gEv({{.*}}) {cir.nosanitize, cir.ubsan.source_column = 3 : i32, cir.ubsan.source_file = "{{.*}}clang/test/CIR/CodeGenCXX/cfi-nvcall.cpp", cir.ubsan.source_line = 58 : i32, cir.ubsan.type_check_kind = 1 : i8, cir.ubsan.type_info = 0 : i16, cir.ubsan.type_kind = -1 : i16, cir.ubsan.type_name = "'C'", cir.virtual.cfi_vcall_kind = "abort", cir.virtual.typeid = "_ZTS1C", cir.virtual.typetest_kind = "hidden"}
extern "C" void cg(C *c) {
  c->g();
}
