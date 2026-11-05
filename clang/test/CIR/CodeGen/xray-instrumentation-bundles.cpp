// Test xray instrumentation bundles with __xray_customevent and
// __xray_typedevent builtins, matching OGCG behavior.

// --- custom bundle: only customevent, no typedevent ---
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=custom \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,CUSTOM,NOTYPED %s
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=custom \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,CUSTOM,NOTYPED %s

// --- typed bundle: only typedevent, no customevent ---
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=typed \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,NOCUSTOM,TYPED %s
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=typed \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,NOCUSTOM,TYPED %s

// --- custom,typed bundle: both ---
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=custom,typed \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,CUSTOM,TYPED %s
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=custom,typed \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,CUSTOM,TYPED %s

// --- none bundle: neither ---
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=none \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -fclangir -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,NOCUSTOM,NOTYPED %s
// RUN: %clang_cc1 -fxray-instrument -fxray-instrumentation-bundle=none \
// RUN:     -x c++ -std=c++11 -triple x86_64-unknown-unknown \
// RUN:     -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes CHECK,NOCUSTOM,NOTYPED %s

// CHECK: define{{.*}} void @_Z16alwaysInstrumentv()
[[clang::xray_always_instrument]] void alwaysInstrument() {
  static constexpr char kPhase[] = "always";
  __xray_customevent(kPhase, 6);
  __xray_typedevent(1, kPhase, 6);
  // CUSTOM: call void @llvm.xray.customevent(ptr {{.*}}, i64 6)
  // NOCUSTOM-NOT: call void @llvm.xray.customevent(
  // TYPED: call void @llvm.xray.typedevent(i64 {{.*}}, ptr{{.*}}, i64 6)
  // NOTYPED-NOT: call void @llvm.xray.typedevent(
}
