// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef void (*FnTy)(void);

void use(void *);
void foo(void) {}

void init_fn_ptrs(void) {
  FnTy a[] = {foo, foo, foo, foo, foo, foo};
  use(a);
}

// CIR-LABEL: cir.func{{.*}} @init_fn_ptrs()
// CIR: %[[A:.*]] = cir.alloca !cir.array<!cir.ptr<!cir.func<()>> x 6>, !cir.ptr<!cir.array<!cir.ptr<!cir.func<()>> x 6>>, ["a", init] {alignment = 16 : i64}
// CIR: %[[SRC:.*]] = cir.get_global @__const.init_fn_ptrs.a : !cir.ptr<!cir.array<!cir.ptr<!cir.func<()>> x 6>>
// CIR-NEXT: cir.copy %[[SRC]] to %[[A]] {dst_alignment = 16 : i64, src_alignment = 16 : i64} : !cir.ptr<!cir.array<!cir.ptr<!cir.func<()>> x 6>>
// CIR-NOT: cir.libc.memset
// CIR: cir.call @use(

// LLVM-LABEL: define dso_local void @init_fn_ptrs()
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{.*}}, ptr align 16 @__const.init_fn_ptrs.a, i64 48, i1 false)
// LLVM-NOT: call void @llvm.memset
// LLVM-NOT: store ptr @foo
// LLVM: call void @use(ptr noundef %{{.*}})

// OGCG-LABEL: define dso_local void @init_fn_ptrs()
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{.*}}, ptr align 16 @__const.init_fn_ptrs.a, i64 48, i1 false)
// OGCG-NOT: call void @llvm.memset
// OGCG-NOT: store ptr @foo
// OGCG: call void @use(ptr noundef %{{.*}})
