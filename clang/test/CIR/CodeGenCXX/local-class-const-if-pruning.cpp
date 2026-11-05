// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Regression: constant-folded dead if-arms should not keep local
// class constructors or local anonymous-struct methods alive in CIR.

inline void OmittingCode(float x) {
  if (0) {
    struct SSSS {
      float bv;
      SSSS(const float &from) : bv(from) {}
    };

    SSSS dead(x);
  }

  struct SSSS {
    float bv;
    SSSS(const float &from) : bv(from) {}
  };

  SSSS live(x);
}

void CallOmittingCode() { OmittingCode(1); }

inline void LocalAnonStruct() {
  if (0) {
    struct {
      void f() {}
    } dead;
    dead.f();
  }

  struct {
    void g() {}
  } live;
  live.g();
}

void CallLocalAnonStruct() { LocalAnonStruct(); }

// CIR-NOT: @_ZZ12OmittingCodefEN4SSSSC1ERKf
// CIR-NOT: @_ZZ12OmittingCodefEN4SSSSC2ERKf
// CIR-NOT: @_ZZ15LocalAnonStructvENUt_1fEv
// CIR-LABEL: cir.func{{.*}} @_Z12OmittingCodef
// CIR-NOT: cir.if
// CIR: cir.call @_ZZ12OmittingCodefEN4SSSSC1E_0RKf
// CIR-LABEL: cir.func{{.*}} @_Z15LocalAnonStructv
// CIR-NOT: cir.if
// CIR: cir.call @_ZZ15LocalAnonStructvENUt0_1gEv

// LLVM-NOT: @_ZZ12OmittingCodefEN4SSSSC1ERKf
// LLVM-NOT: @_ZZ12OmittingCodefEN4SSSSC2ERKf
// LLVM-NOT: @_ZZ15LocalAnonStructvENUt_1fEv
// LLVM-LABEL: define linkonce_odr void @_Z12OmittingCodef
// LLVM: call void @_ZZ12OmittingCodefEN4SSSSC1E_0RKf
// LLVM-LABEL: define linkonce_odr void @_Z15LocalAnonStructv
// LLVM: call void @_ZZ15LocalAnonStructvENUt0_1gEv

// OGCG-NOT: @_ZZ12OmittingCodefEN4SSSSC1ERKf
// OGCG-NOT: @_ZZ12OmittingCodefEN4SSSSC2ERKf
// OGCG-NOT: @_ZZ15LocalAnonStructvENUt_1fEv
// OGCG-LABEL: define linkonce_odr void @_Z12OmittingCodef
// OGCG: call void @_ZZ12OmittingCodefEN4SSSSC1E_0RKf
// OGCG-LABEL: define linkonce_odr void @_Z15LocalAnonStructv
// OGCG: call void @_ZZ15LocalAnonStructvENUt0_1gEv
