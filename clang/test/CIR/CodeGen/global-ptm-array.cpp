// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR

// CIR crashed on ConstantOp with array-of-DataMemberType (e.g., global
// array of pointer-to-member-data). The ConstantOp ABI lowering handler
// only handled bare DataMemberType/MethodType but not composite types
// containing them. Fixed by adding recursive attribute lowering for
// ConstArrayAttr, ConstRecordAttr, and GlobalViewAttr.

struct S {
    int x;
    int y;
    int z;
};

int S::*const ptm_array[] = {&S::x, &S::y, &S::z};

int test() {
    S s = {10, 20, 30};
    int sum = 0;
    for (auto m : ptm_array)
        sum += s.*m;
    return sum;
}

// CIR: define {{.*}}@_Z4testv
