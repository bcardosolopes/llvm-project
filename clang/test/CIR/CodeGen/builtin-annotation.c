// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

int main(int argc, char **argv) {
    int call = __builtin_annotation(argc, "annotation_a");
    return call;
}

// LLVM: call i32 @llvm.annotation.i32
// OGCG: call i32 @llvm.annotation.i32
