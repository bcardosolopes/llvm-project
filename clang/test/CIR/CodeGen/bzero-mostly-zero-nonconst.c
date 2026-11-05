// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck --check-prefix=LLVM %s

extern void use(void *);

void sparse_array(int x) {
  int arr[100] = {x};
  use(arr);
}

// CIR-LABEL: cir.func no_inline optnone dso_local @sparse_array(
// CIR: %[[ARR:.*]] = cir.alloca !cir.array<!s32i x 100>, !cir.ptr<!cir.array<!s32i x 100>>, ["arr", init]
// CIR: %[[ARR_VOID:.*]] = cir.cast bitcast %[[ARR]] : !cir.ptr<!cir.array<!s32i x 100>> -> !cir.ptr<!void>
// CIR: %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// CIR: %[[SIZE:.*]] = cir.const #cir.int<400> : !u64i
// CIR: cir.libc.memset %[[SIZE]] bytes from %[[ARR_VOID]] set to %[[ZERO]] {cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// CIR-NOT: ["arrayinit.temp"
// CIR-NOT: cir.do
// CIR: cir.store align(4) %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR: cir.call @use(

// LLVM-LABEL: define dso_local void @sparse_array(
// LLVM: %[[ARR:.*]] = alloca [100 x i32], i64 1, align 16
// LLVM: call void @llvm.memset.p0.i64(ptr align 16 %[[ARR]], i8 0, i64 400, i1 false)
// LLVM-NOT: br label
// LLVM-NOT: store i32 0
// LLVM: store i32 %{{.*}}, ptr %{{.*}}, align 4
// LLVM: call void @use(ptr noundef %{{.*}})

struct A {
  int a, b, c, d, e, f, g, h, i, j, k, *p;
};

struct B {
  struct A a, b, c, d, e, f, g;
};

void sparse_record(int x) {
  struct B s = {.a.a = x, .d.e = x, .f.e = 0, .f.f = 0, .f.p = 0};
  use(&s);
}

// CIR-LABEL: cir.func no_inline optnone dso_local @sparse_record(
// CIR: %[[S:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["s", init]
// CIR: %[[S_VOID:.*]] = cir.cast bitcast %[[S]] : !cir.ptr<!rec_B> -> !cir.ptr<!void>
// CIR: %[[ZERO2:.*]] = cir.const #cir.int<0> : !s32i
// CIR: %[[SIZE2:.*]] = cir.const #cir.int<392> : !u64i
// CIR: cir.libc.memset %[[SIZE2]] bytes from %[[S_VOID]] set to %[[ZERO2]] {cir.dst_align = 8 : i64} : !cir.ptr<!void>, !s32i, !u64i
// CIR: cir.store align(8) %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR: cir.store align(8) %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR-NOT: cir.const #cir.zero : !rec_A
// CIR-NOT: cir.libc.memset %{{.*}} bytes from %{{.*}} set to %{{.*}} : !cir.ptr<!void>, !s32i, !u64i
// CIR-NOT: cir.store align(4) %{{.*}}, %{{.*}} : !s32i, !cir.ptr<!s32i>
// CIR-NOT: cir.store align(8) %{{.*}}, %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CIR: cir.call @use(

// LLVM-LABEL: define dso_local void @sparse_record(
// LLVM: %[[S:.*]] = alloca %struct.B, i64 1, align 8
// LLVM: call void @llvm.memset.p0.i64(ptr align 8 %[[S]], i8 0, i64 392, i1 false)
// LLVM: store i32 %{{.*}}, ptr %{{.*}}, align 8
// LLVM: store i32 %{{.*}}, ptr %{{.*}}, align 8
// LLVM-NOT: store i32 0
// LLVM-NOT: store ptr null
// LLVM-NOT: store %struct.A zeroinitializer
// LLVM-NOT: call void @llvm.memset
// LLVM: call void @use(ptr noundef %[[S]])
