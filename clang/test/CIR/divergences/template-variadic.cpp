// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Variadic template constexpr static member — constant-folded, no global emitted.

// CIR: define {{.*}} i32 @_Z4testv()
// CIR-NOT: @_ZN7CounterIJidcEE5countE

// OGCG: define {{.*}} i32 @_Z4testv()

template<typename... Args>
struct Counter {
    static constexpr int count = sizeof...(Args);
};

int test() {
    return Counter<int, double, char>::count;
}
