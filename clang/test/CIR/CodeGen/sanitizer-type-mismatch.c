// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,object-size \
// RUN:   -fsanitize-recover=alignment,null,object-size %s -o - \
// RUN:   | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fsanitize=alignment,null,object-size \
// RUN:   -fsanitize-recover=alignment,null,object-size
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,object-size \
// RUN:   -fsanitize-recover=alignment,null,object-size %s -o - \
// RUN:   | grep -Ec '^@\.src' | FileCheck %s --check-prefix=SRC-COUNT
// RUN: cir-translate -cir-to-llvmir %t.cir \
// RUN:   | grep -Ec '^@\.src' | FileCheck %s --check-prefix=SRC-COUNT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,object-size \
// RUN:   -fsanitize-recover=alignment,null,object-size %s -o - \
// RUN:   | grep -Fc "c\"'int'\\00\"" | FileCheck %s --check-prefix=INT-DESC-COUNT
// RUN: cir-translate -cir-to-llvmir %t.cir \
// RUN:   | grep -Fc "c\"'int'\\00\"" | FileCheck %s --check-prefix=INT-DESC-COUNT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,object-size \
// RUN:   -fsanitize-trap=alignment,null,object-size %s -o - \
// RUN:   | FileCheck %s --check-prefix=TRAP

union U {
  int i;
};

struct S {
  int k;
};

// CHECK-LABEL: define{{.*}} void @foo
// CHECK: call i64 @llvm.objectsize.i64.p0(ptr {{.*}}){{.*}}!nosanitize
// CHECK: call void @__ubsan_handle_type_mismatch_v1(ptr {{.*}}, i64 %{{.*}}) #[[CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
void foo(void) {
  union U u;
  u.i = 1;
}

// CHECK-LABEL: define{{.*}} i32 @load
// CHECK: call i64 @llvm.objectsize.i64.p0(ptr
// CHECK: ptrtoint ptr
// CHECK: and i64 %{{.*}}, 3
// CHECK: call void @__ubsan_handle_type_mismatch_v1(ptr
int load(int *p) {
  return *p;
}

// CHECK-LABEL: define{{.*}} void @store
// CHECK: call i64 @llvm.objectsize.i64.p0(ptr
// CHECK: ptrtoint ptr
// CHECK: and i64 %{{.*}}, 3
// CHECK: call void @__ubsan_handle_type_mismatch_v1(ptr
void store(int *p, int q) {
  *p = q;
}

// CHECK-LABEL: define{{.*}} ptr @member_access
// CHECK: call i64 @llvm.objectsize.i64.p0(ptr
// CHECK: ptrtoint ptr
// CHECK: and i64 %{{.*}}, 3
// CHECK: call void @__ubsan_handle_type_mismatch_v1(ptr
int *member_access(struct S *p) {
  return &p->k;
}

// CHECK: declare void @__ubsan_handle_type_mismatch_v1(ptr, i64) #[[DECLATTR:[0-9]+]]
// CHECK: attributes #[[DECLATTR]] = { uwtable }
// CHECK: attributes #[[CALLATTR]] = { nomerge nounwind }
// SRC-COUNT: 1
// INT-DESC-COUNT: 1

// TRAP-LABEL: define{{.*}} i32 @load
// TRAP: call void @llvm.ubsantrap(i8 22)
// TRAP: unreachable
