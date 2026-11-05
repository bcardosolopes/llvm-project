// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Indirect calls must keep byval on later MEMORY-class aggregates even
// when an earlier aggregate is split into multiple ABI arguments. The same
// synthesized byval metadata must also upgrade const-call memory effects.

struct S {
  const char *p;
  unsigned long n;
};

struct Big {
  void *a[3];
};

typedef int (*PlainFn)(struct S, struct Big, void *);

int call_indirect_byval(PlainFn fn, void *ctx) {
  struct Big big = {{ctx, ctx, ctx}};
  return fn((struct S){"x", 1}, big, ctx);
}

int (*fp_const)(struct S, struct Big, void *) __attribute__((const));
int (*fp_pure)(struct S, struct Big, void *) __attribute__((pure));

int call_indirect_byval_const(void *ctx) {
  struct Big big = {{ctx, ctx, ctx}};
  return fp_const((struct S){"x", 1}, big, ctx);
}

int call_indirect_byval_pure(void *ctx) {
  struct Big big = {{ctx, ctx, ctx}};
  return fp_pure((struct S){"x", 1}, big, ctx);
}

// CIR-LABEL: cir.func{{.*}} @call_indirect_byval(
// CIR: cir.call %{{.*}}(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) {cir.call_arg_attrs = [{}, {}, {cir.align = 8 : i64, cir.byval = !rec_Big, cir.byval_size = 24 : i64, cir.noundef}, {cir.noundef}]}

// CIR-LABEL: cir.func{{.*}} @call_indirect_byval_const(
// CIR: cir.call %{{.*}}(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) nothrow side_effect(const) {cir.call_arg_attrs = [{}, {}, {cir.align = 8 : i64, cir.byval = !rec_Big, cir.byval_size = 24 : i64, cir.noundef}, {cir.noundef}]}

// CIR-LABEL: cir.func{{.*}} @call_indirect_byval_pure(
// CIR: cir.call %{{.*}}(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) nothrow side_effect(pure) {cir.call_arg_attrs = [{}, {}, {cir.align = 8 : i64, cir.byval = !rec_Big, cir.byval_size = 24 : i64, cir.noundef}, {cir.noundef}]}

// LLVM-LABEL: define{{.*}} i32 @call_indirect_byval(
// LLVM: call i32 %{{.*}}(ptr %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.Big) align 8 %{{.*}}, ptr noundef %{{.*}})

// LLVM-LABEL: define{{.*}} i32 @call_indirect_byval_const(
// LLVM: call i32 %{{.*}}(ptr readnone %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.Big) align 8 %{{.*}}, ptr noundef readnone %{{.*}}) #[[CONST_CALL:[0-9]+]]

// LLVM-LABEL: define{{.*}} i32 @call_indirect_byval_pure(
// LLVM: call i32 %{{.*}}(ptr readonly %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.Big) align 8 %{{.*}}, ptr noundef readonly %{{.*}}) #[[PURE_CALL:[0-9]+]]
// LLVM: attributes #[[CONST_CALL]] = { nounwind willreturn memory(argmem: readwrite) }
// LLVM: attributes #[[PURE_CALL]] = { nounwind willreturn memory(read, argmem: readwrite) }

// OGCG-LABEL: define{{.*}} i32 @call_indirect_byval(
// OGCG: call i32 %{{.*}}(ptr %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.Big) align 8 %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: define{{.*}} i32 @call_indirect_byval_const(
// OGCG: call i32 %{{.*}}(ptr readnone %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.Big) align 8 %{{.*}}, ptr noundef readnone %{{.*}}) #[[OGCG_CONST_CALL:[0-9]+]]

// OGCG-LABEL: define{{.*}} i32 @call_indirect_byval_pure(
// OGCG: call i32 %{{.*}}(ptr readonly %{{.*}}, i64 %{{.*}}, ptr noundef byval(%struct.Big) align 8 %{{.*}}, ptr noundef readonly %{{.*}}) #[[OGCG_PURE_CALL:[0-9]+]]
// OGCG: attributes #[[OGCG_CONST_CALL]] = { nounwind willreturn memory(argmem: readwrite) }
// OGCG: attributes #[[OGCG_PURE_CALL]] = { nounwind willreturn memory(read, argmem: readwrite) }
