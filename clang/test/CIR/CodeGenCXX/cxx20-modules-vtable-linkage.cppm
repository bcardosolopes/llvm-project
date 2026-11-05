// REQUIRES: !system-windows, !system-cygwin
//
// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: cd %t
//
// RUN: %clang_cc1 -triple %itanium_abi_triple -std=c++20 -emit-module-interface \
// RUN:     %t/Mod.cppm -o %t/Mod.pcm
//
// RUN: %clang_cc1 -triple %itanium_abi_triple -std=c++20 %t/Mod.pcm \
// RUN:     -fclangir -emit-llvm -o - | FileCheck %t/Mod.cppm --check-prefix=CIR-MOD
// RUN: %clang_cc1 -triple %itanium_abi_triple -std=c++20 %t/Mod.pcm \
// RUN:     -emit-llvm -o - | FileCheck %t/Mod.cppm --check-prefix=OGCG-MOD
//
// RUN: %clang_cc1 -triple %itanium_abi_triple -std=c++20 -fmodule-file=Mod=%t/Mod.pcm \
// RUN:     %t/Use.cpp -fclangir -emit-llvm -o - | FileCheck %t/Use.cpp --check-prefix=CIR-USE
// RUN: %clang_cc1 -triple %itanium_abi_triple -std=c++20 -fmodule-file=Mod=%t/Mod.pcm \
// RUN:     %t/Use.cpp -emit-llvm -o - | FileCheck %t/Use.cpp --check-prefix=OGCG-USE

//--- Mod.cppm
export module Mod;

export class Base {
public:
  virtual ~Base();
};

Base::~Base() {}

// CIR-MOD: @_ZTVW3Mod4Base = unnamed_addr constant
// OGCG-MOD: @_ZTVW3Mod4Base = unnamed_addr constant

//--- Use.cpp
import Mod;
int use() {
  Base *base = new Base();
  delete base;
  return 42;
}

// CIR-USE: @_ZTVW3Mod4Base = external
// CIR-USE-NOT: @_ZTIW3Mod4Base
// CIR-USE-NOT: @_ZTSW3Mod4Base
// OGCG-USE: @_ZTVW3Mod4Base = external
// OGCG-USE-NOT: @_ZTIW3Mod4Base
// OGCG-USE-NOT: @_ZTSW3Mod4Base
