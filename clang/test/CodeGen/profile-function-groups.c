// RUN: %clang -fprofile-generate -fprofile-function-groups=3 -fprofile-selected-function-group=0 -emit-llvm -S %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" --implicit-check-not="@__profc_boo" --implicit-check-not="@__profd_boo" --check-prefixes=CHECK,SELECT0
// RUN: %clang -fprofile-generate -fprofile-function-groups=3 -fprofile-selected-function-group=1 -emit-llvm -S %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_hoo" --implicit-check-not="@__profd_hoo" --implicit-check-not="@__profc_boo" --implicit-check-not="@__profd_boo" --check-prefixes=CHECK,SELECT1
// RUN: %clang -fprofile-generate -fprofile-function-groups=3 -fprofile-selected-function-group=2 -emit-llvm -S %s -o - | FileCheck %s --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_hoo" --implicit-check-not="@__profd_hoo" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" --check-prefixes=CHECK,SELECT2
// RUN: %if cir-support %{ %clang -fclangir -fprofile-generate -fprofile-function-groups=3 -fprofile-selected-function-group=0 -emit-llvm -S %s -o - | FileCheck %s --check-prefixes=CIR-CHECK,CIR-SELECT0 --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" --implicit-check-not="@__profc_boo" --implicit-check-not="@__profd_boo" %}
// RUN: %if cir-support %{ %clang -fclangir -fprofile-generate -fprofile-function-groups=3 -fprofile-selected-function-group=1 -emit-llvm -S %s -o - | FileCheck %s --check-prefixes=CIR-CHECK,CIR-SELECT1 --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_hoo" --implicit-check-not="@__profd_hoo" --implicit-check-not="@__profc_boo" --implicit-check-not="@__profd_boo" %}
// RUN: %if cir-support %{ %clang -fclangir -fprofile-generate -fprofile-function-groups=3 -fprofile-selected-function-group=2 -emit-llvm -S %s -o - | FileCheck %s --check-prefixes=CIR-CHECK,CIR-SELECT2 --implicit-check-not="; {{.* (noprofile|skipprofile)}}" --implicit-check-not="@__profc_hoo" --implicit-check-not="@__profd_hoo" --implicit-check-not="@__profc_goo" --implicit-check-not="@__profd_goo" %}

// SELECT0: @__profc_hoo =
// SELECT1: @__profc_goo =
// SELECT2: @__profc_boo =
// CIR-SELECT0: @__profc_hoo =
// CIR-SELECT1: @__profc_goo =
// CIR-SELECT2: @__profc_boo =

// Group 0

// SELECT1: skipprofile
// SELECT2: skipprofile
// CHECK: define {{.*}} @hoo()
// CIR-SELECT1: skipprofile
// CIR-SELECT2: skipprofile
// CIR-CHECK: define {{.*}} @hoo()
void hoo() {}

// Group 1
// SELECT0: skipprofile

// SELECT2: skipprofile
// CHECK: define {{.*}} @goo()
// CIR-SELECT0: skipprofile
// CIR-SELECT2: skipprofile
// CIR-CHECK: define {{.*}} @goo()
void goo() {}

// Group 2
// SELECT0: skipprofile
// SELECT1: skipprofile

// CHECK: define {{.*}} @boo()
// CIR-SELECT0: skipprofile
// CIR-SELECT1: skipprofile
// CIR-CHECK: define {{.*}} @boo()
void boo() {}
