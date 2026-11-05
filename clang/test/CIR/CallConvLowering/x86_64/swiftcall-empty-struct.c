// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Swiftcall functions returning or taking empty struct parameters should not
// crash during call-conv lowering. The empty struct type is Direct with zero
// flattened members, so there are no IR arguments/results for it.

#define SWIFTCALL __attribute__((swiftcall))

typedef struct {} struct_empty;

// CIR: define swiftcc {{.*}} @return_struct_empty()
// OGCG: define swiftcc void @return_struct_empty()
SWIFTCALL struct_empty return_struct_empty(void) {
    struct_empty result = {};
    return result;
}

// CIR: define swiftcc void @take_struct_empty()
// OGCG: define swiftcc void @take_struct_empty()
SWIFTCALL void take_struct_empty(struct_empty v) {
}

// CIR-LABEL: define {{.*}} @test_return()
// CIR: call swiftcc
// OGCG-LABEL: define {{.*}} @test_return()
// OGCG: call swiftcc void @return_struct_empty()
void test_return(void) {
    struct_empty x = return_struct_empty();
}

// CIR-LABEL: define {{.*}} @test_pass()
// CIR: call swiftcc void @take_struct_empty()
// OGCG-LABEL: define {{.*}} @test_pass()
// OGCG: call swiftcc void @take_struct_empty()
void test_pass(void) {
    take_struct_empty(return_struct_empty());
}
