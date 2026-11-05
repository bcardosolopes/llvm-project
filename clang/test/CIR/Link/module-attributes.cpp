// Module attributes merge by what they say, not by whichever input came first.
//
// A module attribute is a statement about one TU. cir-link took the first
// input's copy of each, which is right only where every TU says the same:
//
//  - -fwrapv-pointer forbids inbounds on the GEPs of member access and array
//    decay, and is recorded on the module. One TU built with it and one without
//    got the same answer, whichever came first.
//  - cir.lang chooses the ABI rules (C has no strict return). A C TU merged
//    after a C++ one had its functions lowered as C++, and the other way round.
//  - Top-level asm, the frame-pointer policy and the "module contains X"
//    markers came from the first input alone.
//
// And an attribute nobody has a rule for, that two inputs disagree about, now
// stops the link instead of quietly taking the first input's value.
//
// The inputs are fat objects built as the Android NDK builds them, so the
// calling convention is still to be lowered, by cir-link.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -fomit-frame-pointer \
// RUN:     -c %t/strict.cpp -o %t/strict.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -fwrapv-pointer \
// RUN:     -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
// RUN:     -c %t/wrap.cpp -o %t/wrap.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -fclangir-fat-objects \
// RUN: -O1 -fomit-frame-pointer \
// RUN:     -c %t/plain.c -o %t/plain.o
//
// The precondition, on the CIR clang produces for each: only wrap.cpp has a
// frame-pointer policy and defined pointer overflow, and plain.c is C.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 -fwrapv-pointer -fno-omit-frame-pointer \
// RUN:     -mno-omit-leaf-frame-pointer %t/wrap.cpp -o - \
// RUN:     | FileCheck %s --check-prefix=PRE-WRAP
// PRE-WRAP: module {{.*}}cir.frame_pointer = 4 : i32{{.*}}cir.pointer_overflow_defined
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 -fomit-frame-pointer %t/strict.cpp -o - \
// RUN:     | FileCheck %s --check-prefix=PRE-STRICT
// PRE-STRICT: module
// PRE-STRICT-NOT: cir.frame_pointer
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 %t/plain.c -o - | FileCheck %s --check-prefix=PRE-C
// PRE-C: module {{.*}}cir.lang = #cir.lang<c>
//
// Both orders, to the same result.
// RUN: cir-link %t/strict.o %t/wrap.o %t/plain.o -o %t/merged1.cir
// RUN: cir-link %t/plain.o %t/wrap.o %t/strict.o -o %t/merged2.cir
// RUN: cir-opt %t/merged1.cir | FileCheck %s --check-prefixes=CIR,NOMOD
// RUN: cir-opt %t/merged2.cir | FileCheck %s --check-prefixes=CIR,NOMOD
//
// The merged module is C++, keeps the strongest frame-pointer policy and all
// the top-level asm, and has no module-wide pointer-overflow rule. The C
// function says it is C, and only wrap.cpp's member access is marked.
// CIR: module {{.*}}cir.frame_pointer = 4 : i32
// CIR-SAME: cir.lang = #cir.lang<cxx>
// CIR-SAME: cir.module_asm = [".globl {{[a-z]+}}_marker", ".globl {{[a-z]+}}_marker"]
// NOMOD-NOT: module {{.*}}cir.pointer_overflow_defined
// CIR-DAG: cir.func {{.*}}@c_ret({{.*}} attributes {cir.lang = #cir.lang<c>}
// CIR-DAG: cir.func {{.*}}@_Z7cxx_reti({{.*}}) -> {{.*}} extra(#{{[a-z_0-9]+}}) {
// CIR-DAG: cir.get_member %{{.*}}[1] {cir.pointer_overflow_defined, name = "b"}
// CIR-DAG: cir.get_member %{{.*}}[1] {name = "b"}
//
// RUN: cir-link %t/strict.o %t/wrap.o %t/plain.o --emit-llvm \
// RUN:     --emit-llvm-text -o %t/merged1.ll
// RUN: cir-link %t/plain.o %t/wrap.o %t/strict.o --emit-llvm \
// RUN:     --emit-llvm-text -o %t/merged2.ll
// RUN: FileCheck --input-file=%t/merged1.ll %s --check-prefix=LL
// RUN: FileCheck --input-file=%t/merged2.ll %s --check-prefix=LL
//
// Each TU's member access keeps its own rule, the C function has no strict
// return where the C++ ones do, all the top-level asm is there, and the
// frame-pointer module flag is wrap.cpp's (All, which LLVM numbers 2).
// LL-DAG: define dso_local i32 @c_ret(i32 noundef %0)
// LL-DAG: define dso_local noundef i32 @_Z7cxx_reti(
// LL-DAG: getelementptr inbounds nuw %struct.S, ptr %{{.*}}, i32 0, i32 1
// LL-DAG: getelementptr %struct.S, ptr %{{.*}}, i32 0, i32 1
// LL-DAG: module asm ".globl strict_marker"
// LL-DAG: module asm ".globl wrap_marker"
// LL-DAG: !{i32 7, !"frame-pointer", i32 2}
//
// cir.virtual_function_elim is set only under -fwhole-program-vtables, which a C
// TU does not get: an input without it has no say, as a module without LLVM's
// "Virtual Function Elim" flag has none.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 -flto=full -fwhole-program-vtables %t/strict.cpp -o %t/wpv.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S -O1 %t/plain.c -o %t/plainc.cir
// RUN: FileCheck --input-file=%t/wpv.cir %s --check-prefix=PRE-VFE
// PRE-VFE: module {{.*}}cir.virtual_function_elim = 0 : i32
// RUN: cir-link %t/wpv.cir %t/plainc.cir -o %t/vfe1.cir
// RUN: cir-link %t/plainc.cir %t/wpv.cir -o %t/vfe2.cir
// RUN: FileCheck --input-file=%t/vfe1.cir %s --check-prefix=VFE
// RUN: FileCheck --input-file=%t/vfe2.cir %s --check-prefix=VFE
// VFE: module {{.*}}cir.virtual_function_elim = 0 : i32
//
// An attribute with no merge rule that the inputs disagree about stops the link.
// RUN: cir-link %t/strict.o -o %t/strict.cir
// RUN: cir-opt %t/strict.cir -o %t/strict.txt.cir
// RUN: sed -E 's/^(module .*attributes \{)/\1cir.not_a_real_setting = 1 : i32, /' \
// RUN:     %t/strict.txt.cir > %t/odd.cir
// RUN: not cir-link %t/wrap.o %t/odd.cir -o %t/odd-merged.cir 2>&1 \
// RUN:     | FileCheck %s --check-prefix=NORULE
// NORULE: odd.cir:0:0: error: module attribute 'cir.not_a_real_setting' is 1 : i32 here but (absent) in earlier inputs, and there is no rule for merging it

//--- strict.cpp
struct S { int a, b; };
asm(".globl strict_marker");
int strict_field(S *s) { return s->b; }
int cxx_ret(int x) { return x; }

//--- wrap.cpp
struct S { int a, b; };
asm(".globl wrap_marker");
int wrap_field(S *s) { return s->b; }

//--- plain.c
int c_ret(int x) { return x; }
