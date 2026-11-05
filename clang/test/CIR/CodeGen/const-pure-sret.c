// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that __attribute__((const)) and __attribute__((pure)) functions
// returning large structs (via sret) get upgraded memory effects since the
// sret pointer needs to be writable. This matches OGCG's AddPotentialArgAccess
// behavior (CGCall.cpp:2436-2444, 2725-2728).

typedef struct { int a[16]; } Big;

Big __attribute__((const)) get_big(void);
Big __attribute__((pure)) get_big_pure(int x);
void use(Big *b);

void test_const_sret(void) {
  Big b = get_big();
  use(&b);
}

void test_pure_sret(int x) {
  Big b = get_big_pure(x);
  use(&b);
}

// const sret: memory(argmem: readwrite) — NOT memory(none)
// CHECK-DAG: declare void @get_big(ptr dead_on_unwind {{.*}}writable sret(%struct.Big) align 4) [[CONST_SRET_ATTRS:#[0-9]+]]
// OGCG-DAG: declare void @get_big(ptr dead_on_unwind writable sret(%struct.Big) align 4) [[CONST_SRET_ATTRS:#[0-9]+]]

// pure sret: memory(read, argmem: readwrite) — NOT memory(read)
// CHECK-DAG: declare void @get_big_pure(ptr dead_on_unwind {{.*}}writable sret(%struct.Big) align 4, i32 noundef) [[PURE_SRET_ATTRS:#[0-9]+]]
// OGCG-DAG: declare void @get_big_pure(ptr dead_on_unwind writable sret(%struct.Big) align 4, i32 noundef) [[PURE_SRET_ATTRS:#[0-9]+]]

// When const/pure sret functions also have pointer parameters, those pointers
// get readnone/readonly attrs (matching OGCG's MemAttrForPtrArgs loop in
// CGCall.cpp:3028-3045).
Big __attribute__((const)) get_big_with_ptr(void *, int);
Big __attribute__((pure)) get_big_pure_with_ptr(void *, int);

void test_const_ptr_param(void *p) {
  Big b = get_big_with_ptr(p, 42);
  use(&b);
}

void test_pure_ptr_param(void *p) {
  Big b = get_big_pure_with_ptr(p, 42);
  use(&b);
}

// const sret with ptr param: ptr param gets readnone
// CHECK-DAG: declare void @get_big_with_ptr(ptr dead_on_unwind {{.*}}writable sret(%struct.Big) align 4, ptr noundef readnone, i32 noundef) [[CONST_SRET_ATTRS]]
// OGCG-DAG: declare void @get_big_with_ptr(ptr dead_on_unwind writable sret(%struct.Big) align 4, ptr noundef readnone, i32 noundef) [[CONST_SRET_ATTRS]]

// pure sret with ptr param: ptr param gets readonly
// CHECK-DAG: declare void @get_big_pure_with_ptr(ptr dead_on_unwind {{.*}}writable sret(%struct.Big) align 4, ptr noundef readonly, i32 noundef) [[PURE_SRET_ATTRS]]
// OGCG-DAG: declare void @get_big_pure_with_ptr(ptr dead_on_unwind writable sret(%struct.Big) align 4, ptr noundef readonly, i32 noundef) [[PURE_SRET_ATTRS]]

// CHECK-DAG: attributes [[CONST_SRET_ATTRS]] = { nounwind willreturn memory(argmem: readwrite){{.*}} }
// CHECK-DAG: attributes [[PURE_SRET_ATTRS]] = { nounwind willreturn memory(read, argmem: readwrite{{.*}}){{.*}} }
// OGCG-DAG: attributes [[CONST_SRET_ATTRS]] = { nounwind willreturn memory(argmem: readwrite){{.*}} }
// OGCG-DAG: attributes [[PURE_SRET_ATTRS]] = { nounwind willreturn memory(read, argmem: readwrite{{.*}}){{.*}} }
