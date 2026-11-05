// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

struct NullPtrField {
  int *p;
};

typedef void (*FnTy)(void);

void foo(void) {}

struct FnPtrField {
  FnTy f;
};

void init_null_ptr(void) {
  struct NullPtrField s = {0};
}

// CIR-LABEL: cir.func{{.*}} @init_null_ptr()
// CIR: %[[S:.*]] = cir.alloca !rec_NullPtrField, !cir.ptr<!rec_NullPtrField>, ["s", init] {alignment = 4 : i64}
// CIR: %[[VOID:.*]] = cir.cast bitcast %[[S]] : !cir.ptr<!rec_NullPtrField> -> !cir.ptr<!void>
// CIR: %[[SIZE:.*]] = cir.const #cir.int<4> : !u32i
// CIR: cir.libc.memset %[[SIZE]] bytes from %[[VOID]] set to {{.*}} {cir.dst_align = 4 : i64}

// LLVM-LABEL: define dso_local void @init_null_ptr()
// LLVM: call void @llvm.memset{{.*}}(ptr align 4 %{{.*}}, i8 0, i32 4, i1 false)
// LLVM-NOT: i32 8

// OGCG-LABEL: define dso_local void @init_null_ptr()
// OGCG: call void @llvm.memset{{.*}}(ptr align 4 %{{.*}}, i8 0, i32 4, i1 false)
// OGCG-NOT: i32 8

void init_fn_ptr(void) {
  struct FnPtrField a[1] = {{foo}};
}

// CIR-LABEL: cir.func{{.*}} @init_fn_ptr()
// CIR: %[[A:.*]] = cir.alloca !cir.array<!rec_FnPtrField x 1>, !cir.ptr<!cir.array<!rec_FnPtrField x 1>>, ["a", init] {alignment = 4 : i64}
// CIR: %[[SRC:.*]] = cir.get_global @__const.init_fn_ptr.a : !cir.ptr<!cir.array<!rec_FnPtrField x 1>>
// CIR: cir.copy %[[SRC]] to %[[A]] {dst_alignment = 4 : i64, src_alignment = 4 : i64} : !cir.ptr<!cir.array<!rec_FnPtrField x 1>>

// LLVM-LABEL: define dso_local void @init_fn_ptr()
// LLVM: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 @__const.init_fn_ptr.a, i32 4, i1 false)
// LLVM-NOT: i32 8

// OGCG-LABEL: define dso_local void @init_fn_ptr()
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 @__const.init_fn_ptr.a, i32 4, i1 false)
// OGCG-NOT: i32 8
