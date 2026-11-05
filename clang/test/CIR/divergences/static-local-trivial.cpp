// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Static local variable with trivial (constant) initialization is emitted as
// an internal global. Both CIR and OGCG produce the same global definition.

int get_static() {
    static int x = 42;
    return x++;
}

int test() {
    return get_static();
}

// CIR: @_ZZ10get_staticvE1x = internal global i32 42, align 4
// CIR: define dso_local noundef i32 @_Z10get_staticv()
// CIR: define dso_local noundef i32 @_Z4testv()

// OGCG: @_ZZ10get_staticvE1x = internal global i32 42, align 4
// OGCG: define dso_local noundef i32 @_Z10get_staticv()
// OGCG: define dso_local noundef i32 @_Z4testv()
