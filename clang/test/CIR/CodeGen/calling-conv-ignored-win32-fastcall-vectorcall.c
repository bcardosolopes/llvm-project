// RUN: %clang_cc1 -triple i686-windows-msvc -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i686-windows-msvc -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i686-windows-msvc -fclangir -emit-mlir=cir-abi -o - %s -fdefault-calling-conv=vectorcall | FileCheck %s --check-prefix=CIR-VEC
// RUN: %clang_cc1 -triple i686-windows-msvc -fclangir -emit-llvm -o - %s -fdefault-calling-conv=vectorcall | FileCheck %s --check-prefix=LLVM-VEC

void foo_default(const char *lpString1, const char *lpString2);
void __stdcall foo_std(const char *lpString1, const char *lpString2);
void __fastcall foo_fast(const char *lpString1, const char *lpString2);
void __vectorcall foo_vector(const char *lpString1, const char *lpString2);

void __cdecl bar(void) {
  foo_default(0, 0);
  foo_std(0, 0);
  foo_fast(0, 0);
  foo_vector(0, 0);
}

// CIR-LABEL: cir.func no_inline optnone dso_local @bar()
// CIR: cir.call @"\01@foo_fast@8"(
// CIR: cir.call @"\01foo_vector@@8"(
// CIR-DAG: cir.func private dso_local @"\01@foo_fast@8"(!cir.ptr<!s8i> {cir.inreg, cir.noundef}, !cir.ptr<!s8i> {cir.inreg, cir.noundef}) cc(x86_fastcall)
// CIR-DAG: cir.func private dso_local @"\01foo_vector@@8"(!cir.ptr<!s8i> {cir.inreg, cir.noundef}, !cir.ptr<!s8i> {cir.inreg, cir.noundef}) cc(x86_vectorcall)

// LLVM-LABEL: define dso_local void @bar()
// LLVM: call x86_fastcallcc void @"\01@foo_fast@8"(ptr inreg noundef null, ptr inreg noundef null)
// LLVM: call x86_vectorcallcc void @"\01foo_vector@@8"(ptr inreg noundef null, ptr inreg noundef null)
// LLVM-DAG: declare dso_local x86_fastcallcc void @"\01@foo_fast@8"(ptr inreg noundef, ptr inreg noundef)
// LLVM-DAG: declare dso_local x86_vectorcallcc void @"\01foo_vector@@8"(ptr inreg noundef, ptr inreg noundef)

// CIR-VEC-LABEL: cir.func no_inline optnone dso_local @bar()
// CIR-VEC: cir.call @"\01foo_default@@8"(
// CIR-VEC-DAG: cir.func private dso_local @"\01foo_default@@8"(!cir.ptr<!s8i> {cir.inreg, cir.noundef}, !cir.ptr<!s8i> {cir.inreg, cir.noundef}) cc(x86_vectorcall)

// LLVM-VEC-LABEL: define dso_local void @bar()
// LLVM-VEC: call x86_vectorcallcc void @"\01foo_default@@8"(ptr inreg noundef null, ptr inreg noundef null)
// LLVM-VEC: call x86_fastcallcc void @"\01@foo_fast@8"(ptr inreg noundef null, ptr inreg noundef null)
// LLVM-VEC: call x86_vectorcallcc void @"\01foo_vector@@8"(ptr inreg noundef null, ptr inreg noundef null)
// LLVM-VEC-DAG: declare dso_local x86_vectorcallcc void @"\01foo_default@@8"(ptr inreg noundef, ptr inreg noundef)
// LLVM-VEC-DAG: declare dso_local x86_fastcallcc void @"\01@foo_fast@8"(ptr inreg noundef, ptr inreg noundef)
// LLVM-VEC-DAG: declare dso_local x86_vectorcallcc void @"\01foo_vector@@8"(ptr inreg noundef, ptr inreg noundef)
