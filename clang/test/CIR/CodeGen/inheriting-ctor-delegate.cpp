// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Tests that inherited constructor delegates with variadic base constructors
// are correctly inlined. Previously this hit:
// "NYI: emitCXXConstructorCall for inherited constructor delegate"

struct A { A(int, int, ...); int n; };
struct Z { };
struct B : Z, virtual A { Z z; using A::A; };

// The complete-object constructor for B inlines the inherited constructor,
// directly calling A's base constructor with the forwarded arguments.

// CIR-LABEL: @__cxx_global_var_init
// CIR: cir.call @_ZN1AC2Eiiz

// LLVM-LABEL: @__cxx_global_var_init
// LLVM: call void (ptr, i32, i32, ...) @_ZN1AC2Eiiz({{.*}}, i32 noundef 1, i32 noundef 2, i32 noundef 3, i32 noundef 4)
B b(1, 2, 3, 4);
