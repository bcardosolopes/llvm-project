// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

extern void use(void *);

struct SF9 {
  float a, b, c, d, e, f, g, h, i;
};

struct SD5 {
  double a, b, c, d, e;
};

// CIR-NOT: @__const.
// LLVM-NOT: @__const.
// OGCG-NOT: @__const.

float fa9(void) {
  float a[9] = {1.0f};
  use(a);
  return a[8];
}

// CIR-LABEL: cir.func{{.*}} @fa9() -> !cir.float
// CIR: %[[A:.*]] = cir.alloca !cir.array<!cir.float x 9>, !cir.ptr<!cir.array<!cir.float x 9>>, ["a", init]
// CIR: %[[VOID:.*]] = cir.cast bitcast %[[A]] : !cir.ptr<!cir.array<!cir.float x 9>> -> !cir.ptr<!void>
// CIR: cir.libc.memset {{.*}} from %[[VOID]] set to {{.*}} {cir.dst_align = 16 : i64}
// CIR: %[[DECAY:.*]] = cir.cast array_to_ptrdecay %[[A]] : !cir.ptr<!cir.array<!cir.float x 9>> -> !cir.ptr<!cir.float>
// CIR: %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.float
// CIR: cir.store align(16) %[[ONE]], %[[DECAY]] : !cir.float, !cir.ptr<!cir.float>
// CIR-NOT: cir.libc.memcpy
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local float @fa9()
// LLVM: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 0, i64 36, i1 false)
// LLVM: store float 1.000000e+00, ptr %{{.*}}, align 16
// LLVM-NOT: @llvm.memcpy

// OGCG-LABEL: define dso_local float @fa9()
// OGCG: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 0, i64 36, i1 false)
// OGCG: store float 1.000000e+00, ptr %{{.*}}, align 16
// OGCG-NOT: @llvm.memcpy

double da9(void) {
  double a[9] = {1.0};
  use(a);
  return a[8];
}

// CIR-LABEL: cir.func{{.*}} @da9() -> !cir.double
// CIR: %[[A:.*]] = cir.alloca !cir.array<!cir.double x 9>, !cir.ptr<!cir.array<!cir.double x 9>>, ["a", init]
// CIR: %[[VOID:.*]] = cir.cast bitcast %[[A]] : !cir.ptr<!cir.array<!cir.double x 9>> -> !cir.ptr<!void>
// CIR: cir.libc.memset {{.*}} from %[[VOID]] set to {{.*}} {cir.dst_align = 16 : i64}
// CIR: %[[DECAY:.*]] = cir.cast array_to_ptrdecay %[[A]] : !cir.ptr<!cir.array<!cir.double x 9>> -> !cir.ptr<!cir.double>
// CIR: %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.double
// CIR: cir.store align(16) %[[ONE]], %[[DECAY]] : !cir.double, !cir.ptr<!cir.double>
// CIR-NOT: cir.libc.memcpy
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local double @da9()
// LLVM: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 0, i64 72, i1 false)
// LLVM: store double 1.000000e+00, ptr %{{.*}}, align 16
// LLVM-NOT: @llvm.memcpy

// OGCG-LABEL: define dso_local double @da9()
// OGCG: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 0, i64 72, i1 false)
// OGCG: store double 1.000000e+00, ptr %{{.*}}, align 16
// OGCG-NOT: @llvm.memcpy

float fs9(void) {
  struct SF9 s = {1.0f};
  use(&s);
  return s.i;
}

// CIR-LABEL: cir.func{{.*}} @fs9() -> !cir.float
// CIR: %[[S:.*]] = cir.alloca !rec_SF9, !cir.ptr<!rec_SF9>, ["s", init]
// CIR: %[[VOID:.*]] = cir.cast bitcast %[[S]] : !cir.ptr<!rec_SF9> -> !cir.ptr<!void>
// CIR: cir.libc.memset {{.*}} from %[[VOID]] set to {{.*}} {cir.dst_align = 4 : i64}
// CIR: %[[FIELD:.*]] = cir.get_member %[[S]][0] {{.*}} : !cir.ptr<!rec_SF9> -> !cir.ptr<!cir.float>
// CIR: %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.float
// CIR: cir.store align(4) %[[ONE]], %[[FIELD]] : !cir.float, !cir.ptr<!cir.float>
// CIR-NOT: cir.libc.memcpy
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local float @fs9()
// LLVM: call void @llvm.memset{{.*}}(ptr align 4 %{{.*}}, i8 0, i64 36, i1 false)
// LLVM: store float 1.000000e+00, ptr %{{.*}}, align 4
// LLVM-NOT: @llvm.memcpy

// OGCG-LABEL: define dso_local float @fs9()
// OGCG: call void @llvm.memset{{.*}}(ptr align 4 %{{.*}}, i8 0, i64 36, i1 false)
// OGCG: store float 1.000000e+00, ptr %{{.*}}, align 4
// OGCG-NOT: @llvm.memcpy

double ds5(void) {
  struct SD5 s = {1.0};
  use(&s);
  return s.e;
}

// CIR-LABEL: cir.func{{.*}} @ds5() -> !cir.double
// CIR: %[[S:.*]] = cir.alloca !rec_SD5, !cir.ptr<!rec_SD5>, ["s", init]
// CIR: %[[VOID:.*]] = cir.cast bitcast %[[S]] : !cir.ptr<!rec_SD5> -> !cir.ptr<!void>
// CIR: cir.libc.memset {{.*}} from %[[VOID]] set to {{.*}} {cir.dst_align = 8 : i64}
// CIR: %[[FIELD:.*]] = cir.get_member %[[S]][0] {{.*}} : !cir.ptr<!rec_SD5> -> !cir.ptr<!cir.double>
// CIR: %[[ONE:.*]] = cir.const #cir.fp<1.000000e+00> : !cir.double
// CIR: cir.store align(8) %[[ONE]], %[[FIELD]] : !cir.double, !cir.ptr<!cir.double>
// CIR-NOT: cir.libc.memcpy
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local double @ds5()
// LLVM: call void @llvm.memset{{.*}}(ptr align 8 %{{.*}}, i8 0, i64 40, i1 false)
// LLVM: store double 1.000000e+00, ptr %{{.*}}, align 8
// LLVM-NOT: @llvm.memcpy

// OGCG-LABEL: define dso_local double @ds5()
// OGCG: call void @llvm.memset{{.*}}(ptr align 8 %{{.*}}, i8 0, i64 40, i1 false)
// OGCG: store double 1.000000e+00, ptr %{{.*}}, align 8
// OGCG-NOT: @llvm.memcpy
