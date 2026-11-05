// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fsanitize=cfi-unrelated-cast -fsanitize-trap=cfi-unrelated-cast -emit-llvm %s -o - | FileCheck %s

// -fsanitize=cfi-unrelated-cast only applies to pointer casts, but the scalar
// bitcast path is also reached for non-pointer casts such as a vector-to-vector
// cast. CIR called destTy->getPointeeType() unconditionally there, which is null
// for a vector type, crashing convertTypeForMem ("Cannot retrieve a NULL type
// pointer"). The CFI check must be guarded to pointer destinations (as classic
// CodeGen does), leaving non-pointer casts untouched.

typedef long long __v2di __attribute__((__vector_size__(16)));
typedef int __v4si __attribute__((__vector_size__(16)));
__v2di bar(void);
void foo(__v4si *P) { *P = (__v4si)bar(); }

// CHECK: define{{.*}} void @foo
// CHECK: ret void
