// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++20 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Member pointer types should NOT get noundef (returns or params) because
// -1 is a valid null representation. Regular types should still get noundef.
// Matches OGCG CGCall.cpp:2303-2306 (DetermineNoUndef).

struct S { int x; int y; };

// Member pointer return — no noundef
int S::* get_member_ptr();
int S::* test_memptr_ret() { return get_member_ptr(); }
// CHECK: declare i64 @_Z14get_member_ptrv()
// CHECK-NOT: noundef{{.*}}@_Z14get_member_ptrv
// OGCG: declare i64 @_Z14get_member_ptrv()
// OGCG-NOT: noundef{{.*}}@_Z14get_member_ptrv

// Member pointer parameter — no noundef on the member pointer arg
int test_memptr_param(S *s, int S::*mp) { return s->*mp; }
// CHECK: define {{.*}} @_Z17test_memptr_paramP1SMS_i(ptr noundef %{{.*}}, i64 %{{.*}})
// OGCG: define {{.*}} @_Z17test_memptr_paramP1SMS_i(ptr noundef %{{.*}}, i64 %{{.*}})

// Regular int return — gets noundef
int get_int();
int test_int() { return get_int(); }
// CHECK: declare noundef i32 @_Z7get_intv()
// OGCG: declare noundef i32 @_Z7get_intv()
