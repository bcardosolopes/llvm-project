// RUN: %clang_cc1 -triple aarch64 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify per-function branch-protection attributes via
// __attribute__((target("branch-protection=..."))) on AArch64.

__attribute__((target("branch-protection=none")))
void none(void) {}
// CHECK: define dso_local void @none() #[[#NONE:]]
// OGCG: define dso_local void @none() #[[#NONE:]]

__attribute__((target("branch-protection=standard")))
void std(void) {}
// CHECK: define dso_local void @std() #[[#STD:]]
// OGCG: define dso_local void @std() #[[#STD:]]

__attribute__((target("branch-protection=bti")))
void btionly(void) {}
// CHECK: define dso_local void @btionly() #[[#BTI:]]
// OGCG: define dso_local void @btionly() #[[#BTI:]]

__attribute__((target("branch-protection=pac-ret")))
void paconly(void) {}
// CHECK: define dso_local void @paconly() #[[#PAC:]]
// OGCG: define dso_local void @paconly() #[[#PAC:]]

__attribute__((target("branch-protection=pac-ret+leaf")))
void leaf(void) {}
// CHECK: define dso_local void @leaf() #[[#PACLEAF:]]
// OGCG: define dso_local void @leaf() #[[#PACLEAF:]]

__attribute__((target("branch-protection=pac-ret+b-key")))
void bkey(void) {}
// CHECK: define dso_local void @bkey() #[[#PACBKEY:]]
// OGCG: define dso_local void @bkey() #[[#PACBKEY:]]

__attribute__((target("branch-protection=pac-ret+pc")))
void pauthlr(void) {}
// CHECK: define dso_local void @pauthlr() #[[#PAUTHLR:]]
// OGCG: define dso_local void @pauthlr() #[[#PAUTHLR:]]

__attribute__((target("branch-protection=gcs")))
void gcs(void) {}
// CHECK: define dso_local void @gcs() #[[#GCS:]]
// OGCG: define dso_local void @gcs() #[[#GCS:]]

// CHECK-DAG: attributes #[[#STD]] = { {{.*}} "branch-target-enforcement" "guarded-control-stack" {{.*}} "sign-return-address"="non-leaf" "sign-return-address-key"="a_key"
// OGCG-DAG: attributes #[[#STD]] = { {{.*}} "branch-target-enforcement" "guarded-control-stack" {{.*}} "sign-return-address"="non-leaf" "sign-return-address-key"="a_key"

// CHECK-DAG: attributes #[[#BTI]] = { {{.*}} "branch-target-enforcement"
// OGCG-DAG: attributes #[[#BTI]] = { {{.*}} "branch-target-enforcement"

// CHECK-DAG: attributes #[[#PAC]] = { {{.*}} "sign-return-address"="non-leaf" "sign-return-address-key"="a_key"
// OGCG-DAG: attributes #[[#PAC]] = { {{.*}} "sign-return-address"="non-leaf" "sign-return-address-key"="a_key"

// CHECK-DAG: attributes #[[#PACLEAF]] = { {{.*}} "sign-return-address"="all" "sign-return-address-key"="a_key"
// OGCG-DAG: attributes #[[#PACLEAF]] = { {{.*}} "sign-return-address"="all" "sign-return-address-key"="a_key"

// CHECK-DAG: attributes #[[#PACBKEY]] = { {{.*}} "sign-return-address"="non-leaf" "sign-return-address-key"="b_key"
// OGCG-DAG: attributes #[[#PACBKEY]] = { {{.*}} "sign-return-address"="non-leaf" "sign-return-address-key"="b_key"

// CHECK-DAG: attributes #[[#PAUTHLR]] = { {{.*}} "branch-protection-pauth-lr" {{.*}}"sign-return-address"="non-leaf" "sign-return-address-key"="a_key"
// OGCG-DAG: attributes #[[#PAUTHLR]] = { {{.*}} "branch-protection-pauth-lr" {{.*}}"sign-return-address"="non-leaf" "sign-return-address-key"="a_key"

// CHECK-DAG: attributes #[[#GCS]] = { {{.*}} "guarded-control-stack"
// OGCG-DAG: attributes #[[#GCS]] = { {{.*}} "guarded-control-stack"
