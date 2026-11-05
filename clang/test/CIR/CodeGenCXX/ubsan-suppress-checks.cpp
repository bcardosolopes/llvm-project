// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-cir \
// RUN:   -fsanitize=alignment -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment -o - %s | FileCheck %s --check-prefix=LLVM

// direct-CIR should suppress redundant alignment checks when the
// storage alignment is already known, matching OGCG.

// CIR-LABEL: cir.func {{.*}} @_Z22load_non_null_pointersv()
// CIR-NOT: cir.call @__cir_ubsan_type_mismatch_handler
// CIR: cir.return
// LLVM-LABEL: define {{.*}} @_Z22load_non_null_pointersv()
// LLVM-NOT: call void @__ubsan_handle_type_mismatch_v1_abort
// LLVM: ret void
void load_non_null_pointers() {
  int arr[1];
  arr[0] = arr[0];
}

// CIR-LABEL: cir.func {{.*}} @_Z31use_us16_aligned_array_elementsv()
// CIR-NOT: cir.call @__cir_ubsan_type_mismatch_handler
// CIR: cir.call @_ZZ31use_us16_aligned_array_elementsvENK3$_0clERA3_Kt
// CIR: cir.return
// LLVM-LABEL: define {{.*}} @_Z31use_us16_aligned_array_elementsv()
// LLVM-NOT: call void @__ubsan_handle_type_mismatch_v1_abort
// LLVM: call void @"_ZZ31use_us16_aligned_array_elementsvENK3$_0clERA3_Kt"
// LLVM: ret void
void use_us16_aligned_array_elements() {
  static const unsigned short Arr[] = {0, 1, 2};
  auto use_array = [](const unsigned short (&X)[3]) -> void {};
  use_array(Arr);
}

struct A {
  int foo;

  int load_member() { return foo; }

  // CIR-LABEL: cir.func {{.*}} @_ZN1A11call_methodEv(
  // CIR: cir.call @__cir_ubsan_type_mismatch_handler
  // CIR-NOT: cir.call @__cir_ubsan_type_mismatch_handler
  // CIR: %{{.*}} = cir.call @_ZN1A11load_memberEv
  // CIR: cir.return
  // LLVM-LABEL: define {{.*}} @_ZN1A11call_methodEv(
  // LLVM: call void @__ubsan_handle_type_mismatch_v1_abort
  // LLVM-NOT: call void @__ubsan_handle_type_mismatch_v1_abort
  // LLVM: %{{.*}} = call noundef i32 @_ZN1A11load_memberEv
  // LLVM: ret i32
  int call_method() { return load_member(); }
};

void force_irgen(A *a) {
  load_non_null_pointers();
  use_us16_aligned_array_elements();
  (void)a->call_method();
}
