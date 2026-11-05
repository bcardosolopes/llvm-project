// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: __attribute__((pure)) gets memory(read) and __attribute__((const)) gets
// memory(none). Both get nounwind and willreturn.

__attribute__((pure)) int pure_func(int);
__attribute__((const)) int const_func(int);

int test(int x) {
  return pure_func(x) + const_func(x);
}

// LLVM-LABEL: define {{.*}} @_Z4testi
// LLVM: call {{.*}} i32 @_Z9pure_funci(i32 {{.*}}) #[[PURE_ATTR:[0-9]+]]
// LLVM: call {{.*}} i32 @_Z10const_funci(i32 {{.*}}) #[[CONST_ATTR:[0-9]+]]
// LLVM: attributes #[[PURE_ATTR]] = { nounwind willreturn memory(read) }
// LLVM: attributes #[[CONST_ATTR]] = { nounwind willreturn memory(none) }

// OGCG-LABEL: define {{.*}} @_Z4testi
// OGCG: call {{.*}} i32 @_Z9pure_funci(i32 {{.*}}) #[[OPURE_ATTR:[0-9]+]]
// OGCG: call {{.*}} i32 @_Z10const_funci(i32 {{.*}}) #[[OCONST_ATTR:[0-9]+]]
// OGCG: attributes #[[OPURE_ATTR]] = { nounwind willreturn memory(read) }
// OGCG: attributes #[[OCONST_ATTR]] = { nounwind willreturn memory(none) }
