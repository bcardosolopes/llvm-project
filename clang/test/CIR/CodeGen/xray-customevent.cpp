// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -triple x86_64-unknown-unknown -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -triple x86_64-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -triple x86_64-unknown-unknown -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// CIR-LABEL: @_Z16alwaysInstrumentv
// CIR: cir.call_llvm_intrinsic "xray.customevent" {{%.*}}, {{%.*}} : (!cir.ptr<!void>, !u64i) -> ()

// LLVM-LABEL: @_Z16alwaysInstrumentv
// LLVM: call void @llvm.xray.customevent(ptr{{.*}}, i64 10)

// OGCG-LABEL: @_Z16alwaysInstrumentv
// OGCG: call void @llvm.xray.customevent(ptr{{.*}}, i64 10)
[[clang::xray_always_instrument]] void alwaysInstrument() {
  static constexpr char kPhase[] = "instrument";
  __xray_customevent(kPhase, 10);
}

// CIR-LABEL: @_Z15neverInstrumentv
// CIR-NOT: cir.call_llvm_intrinsic "xray.customevent"

// LLVM-LABEL: @_Z15neverInstrumentv
// LLVM-NOT: call void @llvm.xray.customevent(

// OGCG-LABEL: @_Z15neverInstrumentv
// OGCG-NOT: call void @llvm.xray.customevent(
[[clang::xray_never_instrument]] void neverInstrument() {
  static constexpr char kPhase[] = "never";
  __xray_customevent(kPhase, 5);
}

// CIR-LABEL: @_Z21conditionalInstrumenti
// CIR: cir.call_llvm_intrinsic "xray.customevent" {{%.*}}, {{%.*}} : (!cir.ptr<!void>, !u64i) -> ()
// CIR: cir.call_llvm_intrinsic "xray.customevent" {{%.*}}, {{%.*}} : (!cir.ptr<!void>, !u64i) -> ()

// LLVM-LABEL: @_Z21conditionalInstrumenti
// LLVM: call void @llvm.xray.customevent(ptr{{.*}}, i64 4)
// LLVM: call void @llvm.xray.customevent(ptr{{.*}}, i64 6)

// OGCG-LABEL: @_Z21conditionalInstrumenti
// OGCG: call void @llvm.xray.customevent(ptr{{.*}}, i64 4)
// OGCG: call void @llvm.xray.customevent(ptr{{.*}}, i64 6)
[[clang::xray_always_instrument]] void conditionalInstrument(int v) {
  static constexpr char kTrue[] = "true";
  static constexpr char kUntrue[] = "untrue";
  if (v % 2)
    __xray_customevent(kTrue, 4);
  else
    __xray_customevent(kUntrue, 6);
}
