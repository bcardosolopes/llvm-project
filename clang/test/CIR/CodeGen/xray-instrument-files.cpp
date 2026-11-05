// Test xray external instrument file support (-fxray-always-instrument,
// -fxray-never-instrument, -fxray-attr-list).

// --- Test 1: -fxray-always-instrument (fun: and src: patterns) ---
// RUN: echo "fun:*foo*" > %t.always-instrument
// RUN: echo "src:*xray-instrument-files.cpp" >> %t.always-instrument
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-always-instrument=%t.always-instrument -emit-llvm -o - %s \
// RUN:     -fclangir -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=ALWAYS
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-always-instrument=%t.always-instrument -emit-llvm -o - %s \
// RUN:     -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=ALWAYS

// --- Test 2: -fxray-never-instrument (fun: pattern) ---
// RUN: echo "fun:*foo*" > %t.never-instrument
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-never-instrument=%t.never-instrument -emit-llvm -o - %s \
// RUN:     -fclangir -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=NEVER
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-never-instrument=%t.never-instrument -emit-llvm -o - %s \
// RUN:     -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=NEVER

// --- Test 3: -fxray-attr-list with [always] section ---
// RUN: echo "[always]" > %t.xray-attrlist
// RUN: echo "fun:*foo*" >> %t.xray-attrlist
// RUN: echo "src:*xray-instrument-files.cpp" >> %t.xray-attrlist
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-attr-list=%t.xray-attrlist -emit-llvm -o - %s \
// RUN:     -fclangir -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=ALWAYS
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-attr-list=%t.xray-attrlist -emit-llvm -o - %s \
// RUN:     -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=ALWAYS

// --- Test 4: -fxray-attr-list with arg1 logging ---
// RUN: echo "[always]" > %t.xray-arg1
// RUN: echo "fun:*foo*=arg1" >> %t.xray-arg1
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-attr-list=%t.xray-arg1 -emit-llvm -o - %s \
// RUN:     -fclangir -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=ARG1
// RUN: %clang_cc1 -fxray-instrument -x c++ -std=c++11 \
// RUN:     -fxray-attr-list=%t.xray-arg1 -emit-llvm -o - %s \
// RUN:     -triple x86_64-unknown-linux-gnu \
// RUN:   | FileCheck %s --check-prefix=ARG1

void foo() {}
[[clang::xray_never_instrument]] void bar() {}
void baz() {}

// Test 1 & 3: Both foo and baz get xray-always (fun: matches foo, src: matches all)
// ALWAYS: define{{.*}} void @_Z3foov() #[[ALWAYSATTR:[0-9]+]]
// ALWAYS: define{{.*}} void @_Z3barv() #[[NEVERATTR:[0-9]+]]
// ALWAYS: define{{.*}} void @_Z3bazv() #[[ALWAYSATTR]]
// ALWAYS-DAG: attributes #[[ALWAYSATTR]] = {{.*}}"function-instrument"="xray-always"
// ALWAYS-DAG: attributes #[[NEVERATTR]] = {{.*}}"function-instrument"="xray-never"

// Test 2: foo gets xray-never (fun: matches), baz gets default threshold
// NEVER: define{{.*}} void @_Z3foov() #[[NEVERATTR2:[0-9]+]]
// NEVER: define{{.*}} void @_Z3barv() #[[NEVERATTR2]]
// NEVER: define{{.*}} void @_Z3bazv() #[[DEFAULTATTR:[0-9]+]]
// NEVER-DAG: attributes #[[NEVERATTR2]] = {{.*}}"function-instrument"="xray-never"
// NEVER-DAG: attributes #[[DEFAULTATTR]] = {{.*}}"xray-instruction-threshold"="200"

// Test 4: foo gets xray-always + xray-log-args, baz gets default
// ARG1: define{{.*}} void @_Z3foov() #[[ARG1ATTR:[0-9]+]]
// ARG1: define{{.*}} void @_Z3barv() #[[NEVERATTR3:[0-9]+]]
// ARG1: define{{.*}} void @_Z3bazv() #[[DEFAULTATTR2:[0-9]+]]
// ARG1-DAG: attributes #[[ARG1ATTR]] = {{.*}}"function-instrument"="xray-always"{{.*}}"xray-log-args"="1"
// ARG1-DAG: attributes #[[NEVERATTR3]] = {{.*}}"function-instrument"="xray-never"
// ARG1-DAG: attributes #[[DEFAULTATTR2]] = {{.*}}"xray-instruction-threshold"="200"
