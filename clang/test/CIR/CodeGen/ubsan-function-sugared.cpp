// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s -fsanitize=function -fno-sanitize-recover=all | FileCheck %s --check-prefix=OGCG-X64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s -fclangir -fsanitize=function -fno-sanitize-recover=all | FileCheck %s --check-prefixes=ABORT,ABORT-X64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-cir -o - %s -fclangir -fsanitize=function -fno-sanitize-recover=all | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-cir -o %t.abort.cir %s -fclangir -fsanitize=function -fno-sanitize-recover=all
// RUN: cir-translate -cir-to-llvmir %t.abort.cir | FileCheck %s --check-prefixes=ABORT,ABORT-X64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s -fclangir -fsanitize=function -fsanitize-recover=function | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-cir -o %t.recover.cir %s -fclangir -fsanitize=function -fsanitize-recover=function
// RUN: cir-translate -cir-to-llvmir %t.recover.cir | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s -fclangir -fsanitize=function -fsanitize-trap=function | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-cir -o %t.trap.cir %s -fclangir -fsanitize=function -fsanitize-trap=function
// RUN: cir-translate -cir-to-llvmir %t.trap.cir | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple arm-none-eabi -emit-llvm -o - %s -fsanitize=function -fno-sanitize-recover=all | FileCheck %s --check-prefix=OGCG-ARM
// RUN: %clang_cc1 -triple arm-none-eabi -emit-cir -o - %s -fclangir -fsanitize=function -fno-sanitize-recover=all | FileCheck %s --check-prefix=ARM-CIR
// RUN: %clang_cc1 -triple arm-none-eabi -emit-cir -o %t.arm.abort.cir %s -fclangir -fsanitize=function -fno-sanitize-recover=all
// RUN: %clang_cc1 -triple arm-none-eabi -emit-llvm -o - %s -fclangir -fsanitize=function -fno-sanitize-recover=all | FileCheck %s --check-prefixes=ABORT,ARM
// RUN: cir-translate -cir-to-llvmir %t.arm.abort.cir | FileCheck %s --check-prefixes=ABORT,ARM

auto fun() {}

void caller() {
  auto a = fun;
  a();
}

// CIR: cir.func no_inline optnone dso_local @_Z3funv() extra(#fn_attr) attributes {cir.func_sanitize = [-1056584962 : i32, 905068220 : i32]}
// CIR: cir.func private @__cir_ubsan_function_type_mismatch_handler(!u64i)
// CIR: cir.func no_inline optnone dso_local @_Z6callerv() extra(#fn_attr) attributes {cir.func_sanitize = [-1056584962 : i32, 905068220 : i32]}
// CIR: cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!rec_anon_struct>, !s64i) -> !cir.ptr<!rec_anon_struct> {cir.nosanitize}
// CIR: cir.get_member %{{.*}}[0] {cir.nosanitize, name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!u32i>
// CIR: cir.cmp(eq, %{{.*}}, %{{.*}}) : !u32i, !cir.bool {cir.nosanitize}
// CIR: cir.get_member %{{.*}}[1] {cir.nosanitize, name = ""} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!u32i>
// CIR: cir.if %{{.*}} {
// CIR: cir.call @__cir_ubsan_function_type_mismatch_handler(%{{.*}}) {cir.nosanitize, cir.ubsan.handler_kind = "abort", cir.ubsan.handler_name = "function_type_mismatch"
// CIR: cir.unreachable {cir.nosanitize}
// CIR: } {cir.branch_weights = [1048575 : i32, 1 : i32], cir.nosanitize}

// ARM-CIR: cir.func private @__cir_ubsan_function_type_mismatch_handler(!u32i)
// ARM-CIR: cir.call @__cir_ubsan_function_type_mismatch_handler(%{{.*}}) {cir.nosanitize, cir.ubsan.handler_kind = "abort", cir.ubsan.handler_name = "function_type_mismatch"
// ARM-CIR-SAME: : (!u32i) -> ()

// OGCG-X64: getelementptr <{ i32, i32 }>, ptr {{.*}}, i32 -1, i32 0{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// OGCG-X64: getelementptr <{ i32, i32 }>, ptr {{.*}}, i32 -1, i32 1{{.*}}!nosanitize ![[NOSAN]]
// OGCG-ARM: ptrtoint ptr {{.*}} to i32{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// OGCG-ARM: and i32 {{.*}}, -2{{.*}}!nosanitize ![[NOSAN]]
// OGCG-ARM: inttoptr i32 {{.*}} to ptr{{.*}}!nosanitize ![[NOSAN]]
// OGCG-ARM: getelementptr <{ i32, i32 }>, ptr {{.*}}, i32 -1, i32 0{{.*}}!nosanitize ![[NOSAN]]
// OGCG-ARM: getelementptr <{ i32, i32 }>, ptr {{.*}}, i32 -1, i32 1{{.*}}!nosanitize ![[NOSAN]]

// ABORT-X64: target triple = "x86_64-unknown-linux-gnu"
// ABORT: define{{.*}} void @_Z3funv() #0 {{.*}}!func_sanitize ![[FUNCSAN:[0-9]+]]
// ABORT: define{{.*}} void @_Z6callerv() #0 {{.*}}!func_sanitize ![[FUNCSAN]]
// ABORT-X64: getelementptr <{ i32, i32 }>, ptr {{.*}}, i64 -1{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// ARM: ptrtoint ptr {{.*}} to i32{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// ARM: and i32 {{.*}}, -2{{.*}}!nosanitize ![[NOSAN]]
// ARM: inttoptr i32 {{.*}} to ptr{{.*}}!nosanitize ![[NOSAN]]
// ARM: getelementptr <{ i32, i32 }>, ptr {{.*}}, i32 -1{{.*}}!nosanitize ![[NOSAN]]
// ABORT: getelementptr inbounds nuw <{ i32, i32 }>, ptr {{.*}}, i32 0, i32 0{{.*}}!nosanitize ![[NOSAN]]
// ABORT: load i32, ptr {{.*}}, align {{.*}}{{.*}}!nosanitize ![[NOSAN]]
// ABORT: icmp eq i32 {{.*}}, -1056584962{{.*}}!nosanitize ![[NOSAN]]
// ABORT: br i1 {{.*}}, label %{{[^,]+}}, label %{{[^,]+}}{{.*}}!nosanitize ![[NOSAN]]
// ABORT: getelementptr inbounds nuw <{ i32, i32 }>, ptr {{.*}}, i32 0, i32 1{{.*}}!nosanitize ![[NOSAN]]
// ABORT: load i32, ptr {{.*}}, align {{.*}}{{.*}}!nosanitize ![[NOSAN]]
// ABORT: icmp eq i32 {{.*}}, 905068220{{.*}}!nosanitize ![[NOSAN]]
// ABORT: br i1 {{.*}}, label %{{[^,]+}}, label %{{[^,]+}}{{.*}}!prof ![[BW:[0-9]+]], !nosanitize ![[NOSAN]]
// ABORT-X64: ptrtoint ptr {{.*}} to i64{{.*}}!nosanitize ![[NOSAN]]
// ABORT-X64: call void @__ubsan_handle_function_type_mismatch_abort(ptr @{{.*}}, i64 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NOSAN]]
// ARM: call void @__ubsan_handle_function_type_mismatch_abort(ptr @{{.*}}, i32 %{{.*}}) #[[ABORT_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NOSAN]]
// ABORT-NEXT: unreachable{{.*}}!nosanitize ![[NOSAN]]
// ABORT: br label %{{[^,]+}}{{.*}}!nosanitize ![[NOSAN]]
// ABORT: call void %{{.*}}()
// ABORT: attributes #[[ABORT_CALLATTR]] = { nomerge noreturn nounwind }
// ABORT: ![[FUNCSAN]] = !{i32 -1056584962, i32 905068220}
// ABORT: ![[BW]] = !{!"branch_weights", i32 1048575, i32 1}

// RECOVER: define{{.*}} void @_Z3funv() #0 {{.*}}!func_sanitize ![[FUNCSAN:[0-9]+]]
// RECOVER: define{{.*}} void @_Z6callerv() #0 {{.*}}!func_sanitize ![[FUNCSAN]]
// RECOVER: icmp eq i32 {{.*}}, -1056584962{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// RECOVER: br i1 {{.*}}, label %{{[^,]+}}, label %{{[^,]+}}{{.*}}!nosanitize ![[NOSAN]]
// RECOVER: icmp eq i32 {{.*}}, 905068220{{.*}}!nosanitize ![[NOSAN]]
// RECOVER: br i1 {{.*}}, label %{{[^,]+}}, label %{{[^,]+}}{{.*}}!prof ![[BW:[0-9]+]], !nosanitize ![[NOSAN]]
// RECOVER: ptrtoint ptr {{.*}} to i64{{.*}}!nosanitize ![[NOSAN]]
// RECOVER: call void @__ubsan_handle_function_type_mismatch(ptr @{{.*}}, i64 %{{.*}}) #[[RECOVER_CALLATTR:[0-9]+]]{{.*}}!nosanitize ![[NOSAN]]
// RECOVER-NEXT: br label %{{[^,]+}}{{.*}}!nosanitize ![[NOSAN]]
// RECOVER: attributes #[[RECOVER_CALLATTR]] = { nomerge nounwind }
// RECOVER: ![[FUNCSAN]] = !{i32 -1056584962, i32 905068220}

// TRAP: define{{.*}} void @_Z3funv() #0 {{.*}}!func_sanitize ![[FUNCSAN:[0-9]+]]
// TRAP: define{{.*}} void @_Z6callerv() #0 {{.*}}!func_sanitize ![[FUNCSAN]]
// TRAP: icmp eq i32 {{.*}}, -1056584962{{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// TRAP: br i1 {{.*}}, label %{{[^,]+}}, label %{{[^,]+}}{{.*}}!nosanitize ![[NOSAN]]
// TRAP: icmp eq i32 {{.*}}, 905068220{{.*}}!nosanitize ![[NOSAN]]
// TRAP: br i1 {{.*}}, label %{{[^,]+}}, label %{{[^,]+}}{{.*}}!prof ![[BW:[0-9]+]], !nosanitize ![[NOSAN]]
// TRAP: call void @llvm.ubsantrap(i8 6){{.*}}!nosanitize ![[NOSAN]]
// TRAP-NEXT: unreachable{{.*}}!nosanitize ![[NOSAN]]
// TRAP: ![[FUNCSAN]] = !{i32 -1056584962, i32 905068220}
