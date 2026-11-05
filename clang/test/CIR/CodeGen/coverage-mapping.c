// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping -dump-coverage-mapping %s -o /dev/null | FileCheck %s --check-prefix=DUMP

static inline int dead(int x) {
  if (x)
    return 1;
  return 0;
}

int logical_and(int a, int b) { return a && b; }
int logical_or(int a, int b) { return a || b; }
int ternary_conditional(int x) { return x ? x : 4; }
int binary_conditional(int x) { return x ?: 4; }
int direct_goto(int a) {
  if (a)
    goto done;
  return 0;
done:
  return 1;
}

int indirect_goto(void) {
  void *target = &&done;
  goto *target;
  return 0;
done:
  return 1;
}

int live(int n) {
  int s = 0;
  for (int i = 0; i < n; ++i)
    if (i)
      s += i;
  while (s > 10)
    --s;
  do
    ++s;
  while (s < n);
  return s;
}

// CHECK-DAG: @__covrec_{{[0-9A-F]+}}u = linkonce_odr hidden constant
// CHECK-DAG: @__covrec_{{[0-9A-F]+}} = linkonce_odr hidden constant
// CHECK: @__llvm_coverage_mapping = private constant
// CHECK: @__profc_logical_and = private global [3 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_logical_or = private global [3 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_ternary_conditional = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_binary_conditional = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_direct_goto = private global [3 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_indirect_goto = private global [2 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @__profc_live = private global [5 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK: @llvm.used = appending global

// DUMP-LABEL: logical_and:
// DUMP: Branch,File 0{{.*}} = #1, (#0 - #1)
// DUMP: Branch,File 0{{.*}} = #2, (#1 - #2)

// DUMP-LABEL: logical_or:
// DUMP: Branch,File 0{{.*}} = (#0 - #1), #1
// DUMP: Branch,File 0{{.*}} = (#1 - #2), #2

// DUMP-LABEL: ternary_conditional:
// DUMP: Branch,File 0{{.*}} = #1, (#0 - #1)
// DUMP: Gap,File 0{{.*}} = #1
// DUMP: File 0{{.*}} = (#0 - #1)

// DUMP-LABEL: binary_conditional:
// DUMP: Branch,File 0{{.*}} = #1, (#0 - #1)
// DUMP: File 0{{.*}} = (#0 - #1)

// DUMP-LABEL: direct_goto:
// DUMP: Branch,File 0{{.*}} = #1, (#0 - #1)
// DUMP: File 0{{.*}} = #2

// DUMP-LABEL: indirect_goto:
// DUMP: Gap,File 0{{.*}} = 0
// DUMP: File 0{{.*}} = #1
