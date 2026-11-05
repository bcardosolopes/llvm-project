// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify CIR emits __cxa_guard_acquire/__cxa_guard_release for static local
// variables with non-trivial constructors, per the Itanium C++ ABI.

struct GlobalClass {
    int value;
    GlobalClass(int v) : value(v) {}
    ~GlobalClass() {}
};

int get_static_local() {
    static GlobalClass local(123);
    return local.value;
}

// CIR: cir.global {{.*}} @_ZGVZ16get_static_localvE5local = #cir.int<0> : !u64i
// CIR: cir.func private @__cxa_guard_acquire
// CIR: cir.func private @__cxa_guard_release
// CIR: cir.func {{.*}} @_Z16get_static_localv
// CIR:   cir.if
// CIR:     cir.call @__cxa_guard_acquire({{.*}}) nothrow
// CIR:     cir.if
// CIR:       cir.call @__cxa_guard_release({{.*}}) nothrow

// LLVM: @_ZGVZ16get_static_localvE5local = internal global i64 0
// LLVM: declare {{.*}} @__cxa_guard_acquire
// LLVM: declare {{.*}} @__cxa_guard_release
// LLVM: define {{.*}} @_Z16get_static_localv()
// LLVM: call i32 @__cxa_guard_acquire({{.*}})
// LLVM: call void @__cxa_guard_release({{.*}})

// LLVM: attributes #{{[0-9]+}} = { nounwind {{.*}}}

// OGCG: @_ZGVZ16get_static_localvE5local = internal global i64 0
// OGCG: define {{.*}} @_Z16get_static_localv()
// OGCG: call {{.*}} @__cxa_guard_acquire
// OGCG: call void @__cxa_guard_release
