// UNSUPPORTED: system-aix
// UNSUPPORTED: target={{.*}}-zos{{.*}}
//
// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: split-file %s %t
//
// Module interface unit: check CIR matches OGCG for module init function.
// RUN: %clang_cc1 -std=c++20 %t/M.cppm -triple %itanium_abi_triple -emit-module-interface -o %t/M.pcm
// RUN: %clang_cc1 -std=c++20 %t/M.pcm -triple %itanium_abi_triple -fclangir -emit-llvm -o %t/M.cir.ll
// RUN: FileCheck --input-file=%t/M.cir.ll %s --check-prefix=MOD
// RUN: %clang_cc1 -std=c++20 %t/M.pcm -triple %itanium_abi_triple -emit-llvm -o %t/M.ogcg.ll
// RUN: FileCheck --input-file=%t/M.ogcg.ll %s --check-prefix=OGCG-MOD
//
// Importing TU: check CIR matches OGCG for calling module init.
// RUN: %clang_cc1 -std=c++20 %t/Use.cpp -fprebuilt-module-path=%t -triple %itanium_abi_triple -fclangir -emit-llvm -o %t/Use.cir.ll
// RUN: FileCheck --input-file=%t/Use.cir.ll %s --check-prefix=USE
// RUN: %clang_cc1 -std=c++20 %t/Use.cpp -fprebuilt-module-path=%t -triple %itanium_abi_triple -emit-llvm -o %t/Use.ogcg.ll
// RUN: FileCheck --input-file=%t/Use.ogcg.ll %s --check-prefix=OGCG-USE

// C++20 module initializer must have external linkage, guard variable,
// and importing TUs must call the module init via _GLOBAL__sub_I_.

//--- M.cppm
export module M;
export class A {
public:
    A();
    ~A();
    void use();
};
export A a;

// Module init function must have external linkage (not internal).
// MOD: @_ZGIW1M__in_chrg = internal global i8 0
// MOD: define void @_ZGIW1M()
// MOD: load i8, ptr @_ZGIW1M__in_chrg
// MOD: br i1 {{.*}}, label {{.*}}, label {{.*}}, !prof ![[MOD_PROF:[0-9]+]]
// MOD: store i8 1, ptr @_ZGIW1M__in_chrg
// MOD: call void @__cxx_global_var_init()
//
// OGCG-MOD: @_ZGIW1M__in_chrg = internal global i8 0
// OGCG-MOD: define void @_ZGIW1M()
// OGCG-MOD: load i8, ptr @_ZGIW1M__in_chrg
// OGCG-MOD: br i1 {{.*}}, label {{.*}}, label {{.*}}, !prof ![[OGCG_MOD_PROF:[0-9]+]]
// OGCG-MOD: store i8 1, ptr @_ZGIW1M__in_chrg
// OGCG-MOD: call void @__cxx_global_var_init()
// MOD: ![[MOD_PROF]] = !{!"branch_weights", i32 1, i32 1023}
// OGCG-MOD: ![[OGCG_MOD_PROF]] = !{!"branch_weights", i32 1, i32 1023}

//--- Use.cpp
import M;
void use() {
    a.use();
}

// Importing TU must declare and call the module init function.
// USE: define internal void @_GLOBAL__sub_I_Use.cpp()
// USE: call void @_ZGIW1M()
// USE: declare void @_ZGIW1M()
//
// OGCG-USE: declare void @_ZGIW1M()
// OGCG-USE: define internal void @_GLOBAL__sub_I_Use.cpp()
// OGCG-USE: call void @_ZGIW1M()
