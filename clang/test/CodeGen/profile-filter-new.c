// RUN: %clang_cc1 -fprofile-instrument=llvm -emit-llvm %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}"
// RUN: %if cir-support %{ %clang_cc1 -fprofile-instrument=llvm -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix CIR-CHECK --implicit-check-not="; {{.* (noprofile|skipprofile)}}" %}

// RUN: echo -e "[llvm]\nfunction:foo=skip" > %t0.list
// RUN: %clang_cc1 -fprofile-instrument=llvm -fprofile-list=%t0.list -emit-llvm %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_foo" --implicit-check-not="@__profd_foo" --check-prefixes=CHECK,SKIP-FOO
// RUN: %if cir-support %{ %clang_cc1 -fprofile-instrument=llvm -fclangir -fprofile-list=%t0.list -emit-llvm %s -o - | FileCheck %s --check-prefixes=CIR-CHECK,CIR-SKIP-FOO --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_foo" --implicit-check-not="@__profd_foo" %}

// RUN: echo -e "[csllvm]\nfunction:bar=forbid" > %t1.list
// RUN: %clang_cc1 -fprofile-instrument=csllvm -fprofile-list=%t1.list -emit-llvm %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --check-prefixes=CHECK,FORBID-BAR
// RUN: %clang_cc1 -fprofile-instrument=llvm -fprofile-list=%t1.list -emit-llvm %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}"

// RUN: echo -e "[llvm]\ndefault:forbid\nfunction:foo=allow" > %t2.list
// RUN: %clang_cc1 -fprofile-instrument=llvm -fprofile-list=%t2.list -emit-llvm %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_bar" --implicit-check-not="@__profd_bar" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" --check-prefixes=CHECK,FORBID
// RUN: %if cir-support %{ %clang_cc1 -fprofile-instrument=llvm -fclangir -fprofile-list=%t2.list -emit-llvm %s -o - | FileCheck %s --check-prefixes=CIR-CHECK,CIR-FORBID --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_bar" --implicit-check-not="@__profd_bar" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" %}

// RUN: echo "[llvm]" > %t2.list
// RUN: echo "source:%s=forbid" | sed -e 's/\\/\\\\/g' >> %t2.list
// RUN: echo "function:foo=allow" >> %t2.list
// RUN: %clang_cc1 -fprofile-instrument=llvm -fprofile-list=%t2.list -emit-llvm %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_bar" --implicit-check-not="@__profd_bar" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" --check-prefixes=CHECK,FORBID
// RUN: %if cir-support %{ %clang_cc1 -fprofile-instrument=llvm -fclangir -fprofile-list=%t2.list -emit-llvm %s -o - | FileCheck %s --check-prefixes=CIR-CHECK,CIR-FORBID --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_bar" --implicit-check-not="@__profd_bar" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" %}

// SKIP-FOO: @__profc_bar =
// SKIP-FOO: @__profc_goo =
// FORBID: @__profc_foo =
// CIR-SKIP-FOO: @__profc_bar =
// CIR-SKIP-FOO: @__profc_goo =
// CIR-FORBID: @__profc_foo =

// SKIP-FOO: skipprofile
// CIR-SKIP-FOO: skipprofile
// CHECK-LABEL: define {{.*}} @foo
// CIR-CHECK-LABEL: define {{.*}} @foo
int foo(int a) { return 4 * a + 1; }

// FORBID-BAR: noprofile
// FORBID: noprofile
// CIR-FORBID: noprofile
// CHECK-LABEL: define {{.*}} @bar
// CIR-CHECK-LABEL: define {{.*}} @bar
int bar(int a) { return 4 * a + 2; }

// FORBID: noprofile
// CIR-FORBID: noprofile
// CHECK-LABEL: define {{.*}} @goo
// CIR-CHECK-LABEL: define {{.*}} @goo
int goo(int a) { return 4 * a + 3; }
