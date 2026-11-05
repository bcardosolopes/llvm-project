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
// CIR: cir.call_llvm_intrinsic "xray.typedevent" {{%.*}}, {{%.*}}, {{%.*}} : (!u64i, !cir.ptr<!void>, !u64i) -> ()

// LLVM-LABEL: @_Z16alwaysInstrumentv
// LLVM: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 10)

// OGCG-LABEL: @_Z16alwaysInstrumentv
// OGCG: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 10)
[[clang::xray_always_instrument]] void alwaysInstrument() {
  auto EventType = 1;
  static constexpr char kPhase[] = "instrument";
  __xray_typedevent(EventType, kPhase, 10);
}

// CIR-LABEL: @_Z15neverInstrumentv
// CIR-NOT: cir.call_llvm_intrinsic "xray.typedevent"

// LLVM-LABEL: @_Z15neverInstrumentv
// LLVM-NOT: call void @llvm.xray.typedevent(

// OGCG-LABEL: @_Z15neverInstrumentv
// OGCG-NOT: call void @llvm.xray.typedevent(
[[clang::xray_never_instrument]] void neverInstrument() {
  auto EventType = 2;
  static constexpr char kPhase[] = "never";
  __xray_typedevent(EventType, kPhase, 5);
}

// CIR-LABEL: @_Z21conditionalInstrumenti
// CIR: cir.call_llvm_intrinsic "xray.typedevent" {{%.*}}, {{%.*}}, {{%.*}} : (!u64i, !cir.ptr<!void>, !u64i) -> ()
// CIR: cir.call_llvm_intrinsic "xray.typedevent" {{%.*}}, {{%.*}}, {{%.*}} : (!u64i, !cir.ptr<!void>, !u64i) -> ()

// LLVM-LABEL: @_Z21conditionalInstrumenti
// LLVM: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 4)
// LLVM: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 6)

// OGCG-LABEL: @_Z21conditionalInstrumenti
// OGCG: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 4)
// OGCG: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 6)
[[clang::xray_always_instrument]] void conditionalInstrument(int v) {
  auto TrueEventType = 3;
  auto UntrueEventType = 4;
  static constexpr char kTrue[] = "true";
  static constexpr char kUntrue[] = "untrue";
  if (v % 2)
    __xray_typedevent(TrueEventType, kTrue, 4);
  else
    __xray_typedevent(UntrueEventType, kUntrue, 6);
}
