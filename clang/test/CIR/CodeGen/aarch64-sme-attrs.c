// Test that CIR correctly extracts AArch64 SME attributes from FunctionProtoType
// and forwards them to LLVM IR function declarations/definitions.
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme \
// RUN:   -target-feature +sme2 -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme \
// RUN:   -target-feature +sme2 -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

void streaming_func(void) __arm_streaming;
void streaming_compatible_func(void) __arm_streaming_compatible;
void inout_za_func(void) __arm_inout("za");
void preserves_za_func(void) __arm_preserves("za");
__arm_locally_streaming void locally_streaming_decl(void);

// LLVM-LABEL: @caller
// LLVM: call void @streaming_func()
// LLVM: call void @streaming_compatible_func()
// LLVM: call void @inout_za_func()
// LLVM: call void @preserves_za_func()
// OGCG-LABEL: @caller
// OGCG: call void @streaming_func()
// OGCG: call void @streaming_compatible_func()
// OGCG: call void @inout_za_func()
// OGCG: call void @preserves_za_func()
void caller(void) __arm_inout("za") {
  streaming_func();
  streaming_compatible_func();
  inout_za_func();
  preserves_za_func();
}

// LLVM-LABEL: @call_locally_streaming
// LLVM: call void @locally_streaming_decl() #[[SM_BODY_CALL:[0-9]+]]
// OGCG-LABEL: @call_locally_streaming
// OGCG: call void @locally_streaming_decl() #[[SM_BODY_CALL:[0-9]+]]
void call_locally_streaming(void) {
  locally_streaming_decl();
}

// Test definition-level SME attributes (__arm_locally_streaming, __arm_new).
// These are set on the function definition, not the prototype.

// LLVM-LABEL: @locally_streaming_func
// OGCG-LABEL: @locally_streaming_func
__arm_locally_streaming void locally_streaming_func(void) {}

// LLVM-LABEL: @new_za_func
// OGCG-LABEL: @new_za_func
__arm_new("za") void new_za_func(void) {}

// LLVM-LABEL: @new_zt0_func
// OGCG-LABEL: @new_zt0_func
__arm_new("zt0") void new_zt0_func(void) {}

// LLVM-LABEL: @new_za_zt0_func
// OGCG-LABEL: @new_za_zt0_func
__arm_new("za", "zt0") void new_za_zt0_func(void) {}

// Check function declaration attributes match OGCG.
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_pstate_sm_enabled"{{.*}} }
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_pstate_sm_compatible"{{.*}} }
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_inout_za"{{.*}} }
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_preserves_za"{{.*}} }
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_pstate_sm_body"{{.*}} }
// LLVM-DAG: attributes #[[SM_BODY_CALL]] = { "aarch64_pstate_sm_body" }
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_new_za"{{.*}} }
// LLVM-DAG: attributes {{.*}} = { {{.*}}"aarch64_new_zt0"{{.*}} }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_pstate_sm_enabled"{{.*}} }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_pstate_sm_compatible"{{.*}} }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_inout_za"{{.*}} }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_preserves_za"{{.*}} }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_pstate_sm_body"{{.*}} }
// OGCG-DAG: attributes #[[SM_BODY_CALL]] = { "aarch64_pstate_sm_body" }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_new_za"{{.*}} }
// OGCG-DAG: attributes {{.*}} = { {{.*}}"aarch64_new_zt0"{{.*}} }
