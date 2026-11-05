// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fxray-instrument -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fxray-instrument -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fxray-instrument -fxray-ignore-loops -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LOOPS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fxray-instrument -fxray-ignore-loops -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-LOOPS

// Verify that XRay function attributes are correctly propagated.

[[clang::xray_always_instrument]] void always_inst() {}
// LLVM: define {{.*}}void @_Z11always_instv() {{.*}}#[[ALWAYS:[0-9]+]]
// OGCG: define {{.*}}void @_Z11always_instv() {{.*}}#[[ALWAYS:[0-9]+]]

[[clang::xray_never_instrument]] void never_inst() {}
// LLVM: define {{.*}}void @_Z10never_instv() {{.*}}#[[NEVER:[0-9]+]]
// OGCG: define {{.*}}void @_Z10never_instv() {{.*}}#[[NEVER:[0-9]+]]

void default_inst() {}
// LLVM: define {{.*}}void @_Z12default_instv() {{.*}}#[[DEFAULT:[0-9]+]]
// OGCG: define {{.*}}void @_Z12default_instv() {{.*}}#[[DEFAULT:[0-9]+]]

[[clang::xray_always_instrument, clang::xray_log_args(1)]]
void log_args(int a) {}
// LLVM: define {{.*}}void @_Z8log_argsi({{.*}}) {{.*}}#[[LOGARGS:[0-9]+]]
// OGCG: define {{.*}}void @_Z8log_argsi({{.*}}) {{.*}}#[[LOGARGS:[0-9]+]]

// LLVM: attributes #[[ALWAYS]] = {{.*}}"function-instrument"="xray-always"
// OGCG: attributes #[[ALWAYS]] = {{.*}}"function-instrument"="xray-always"

// LLVM: attributes #[[NEVER]] = {{.*}}"function-instrument"="xray-never"
// OGCG: attributes #[[NEVER]] = {{.*}}"function-instrument"="xray-never"

// LLVM: attributes #[[DEFAULT]] = {{.*}}"xray-instruction-threshold"="200"
// OGCG: attributes #[[DEFAULT]] = {{.*}}"xray-instruction-threshold"="200"

// LLVM: attributes #[[LOGARGS]] = {{.*}}"xray-log-args"="1"
// OGCG: attributes #[[LOGARGS]] = {{.*}}"xray-log-args"="1"

// Check xray-ignore-loops is propagated.
// LOOPS: attributes {{.*}}"xray-ignore-loops"
// OGCG-LOOPS: attributes {{.*}}"xray-ignore-loops"
