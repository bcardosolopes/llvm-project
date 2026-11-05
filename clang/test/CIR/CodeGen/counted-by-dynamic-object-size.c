// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -Wno-missing-declarations \
// RUN:   -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -Wno-missing-declarations \
// RUN:   -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -Wno-missing-declarations \
// RUN:   -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test that __builtin_dynamic_object_size correctly computes size from
// counted_by attribute on flexible array members, matching OGCG behavior.

struct variable {
  int a;
  int b;
  int length;
  short array[] __attribute__((counted_by(length)));
};

struct bucket {
  int a;
  struct variable *growable;
  int b;
};

struct bucket2 {
  int a;
  struct variable growable;
};

void init(void * __attribute__((pass_dynamic_object_size(0))));

// Test 1: counted_by through pointer indirection (foo->growable->array)
// CIR-LABEL: @test1
// CIR: cir.get_member {{.*}}[2] {name = "length"}
// CIR: cir.binop(mul,
// CIR: cir.cmp(ge,
// CIR: cir.select

// LLVM-LABEL: define {{.*}}@test1
// LLVM: shl nsw i64 {{.*}}, 1
// LLVM: icmp sgt i32 {{.*}}, -1
// LLVM: select i1

// OGCG-LABEL: define {{.*}}@test1
// OGCG: shl nsw i64 {{.*}}, 1
// OGCG: icmp sgt i32 {{.*}}, -1
// OGCG: select i1
void test1(struct bucket *foo) {
  init(foo->growable->array);
}

// Test 2: counted_by with embedded struct (foo->growable.array)
// CIR-LABEL: @test2
// CIR: cir.get_member {{.*}}[2] {name = "length"}
// CIR: cir.binop(mul,
// CIR: cir.cmp(ge,
// CIR: cir.select

// LLVM-LABEL: define {{.*}}@test2
// LLVM: shl nsw i64 {{.*}}, 1
// LLVM: icmp sgt i32 {{.*}}, -1
// LLVM: select i1

// OGCG-LABEL: define {{.*}}@test2
// OGCG: shl nsw i64 {{.*}}, 1
// OGCG: icmp sgt i32 {{.*}}, -1
// OGCG: select i1
void test2(struct bucket2 *foo) {
  init(foo->growable.array);
}

// Test 3: Direct __builtin_dynamic_object_size call on counted_by FAM
struct simple {
  int count;
  int data[] __attribute__((counted_by(count)));
};

// CIR-LABEL: @test_direct_bdos
// CIR: cir.get_member {{.*}} {name = "count"}
// CIR: cir.binop(mul,
// CIR: cir.cmp(ge,
// CIR: cir.select

// LLVM-LABEL: define {{.*}}@test_direct_bdos
// LLVM: shl nsw i64 {{.*}}, 2
// LLVM: icmp sgt i32 {{.*}}, -1
// LLVM: select i1

// OGCG-LABEL: define {{.*}}@test_direct_bdos
// OGCG: shl nsw i64 {{.*}}, 2
// OGCG: icmp sgt i32 {{.*}}, -1
// OGCG: select i1
long long test_direct_bdos(struct simple *p) {
  return __builtin_dynamic_object_size(p->data, 0);
}
