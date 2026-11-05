// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Ensure #pragma clang section attributes are preserved by CIR codegen for
// globals/static locals (bss/data/rodata/relro) and functions (text), while
// keeping SectionAttr precedence.

#pragma clang section bss=".mybss" data=".mydata" rodata=".myro" relro=".myrelro" text=".mytext"
int g_bss;
int g_data = 1;
const int g_ro = 2;
void pragma_text_fn(void) {}

int locals(void) {
  static int l_bss;
  static int l_data = 3;
  static const int l_ro = 4;
  return l_bss + l_data + l_ro;
}

__attribute__((section(".explicit_data"))) int g_explicit = 5;
void explicit_text_fn(void) __attribute__((section(".explicit_text")));
void explicit_text_fn(void) {}

#pragma clang section bss="" data="" rodata="" relro="" text=""

// CIR-DAG: cir.global external @g_bss = #cir.int<0> : !s32i {alignment = 4 : i64, cir.pragma_clang_bss_section = ".mybss", cir.pragma_clang_data_section = ".mydata", cir.pragma_clang_relro_section = ".myrelro", cir.pragma_clang_rodata_section = ".myro"}
// CIR-DAG: cir.global external @g_data = #cir.int<1> : !s32i {alignment = 4 : i64, cir.pragma_clang_bss_section = ".mybss", cir.pragma_clang_data_section = ".mydata", cir.pragma_clang_relro_section = ".myrelro", cir.pragma_clang_rodata_section = ".myro"}
// CIR-DAG: cir.global constant external @g_ro = #cir.int<2> : !s32i {alignment = 4 : i64, cir.pragma_clang_bss_section = ".mybss", cir.pragma_clang_data_section = ".mydata", cir.pragma_clang_relro_section = ".myrelro", cir.pragma_clang_rodata_section = ".myro"}
// CIR-DAG: cir.global "private" internal {{.*}}@locals.l_bss = #cir.int<0> : !s32i {alignment = 4 : i64, cir.pragma_clang_bss_section = ".mybss", cir.pragma_clang_data_section = ".mydata", cir.pragma_clang_relro_section = ".myrelro", cir.pragma_clang_rodata_section = ".myro"}
// CIR-DAG: cir.global "private" internal {{.*}}@locals.l_data = #cir.int<3> : !s32i {alignment = 4 : i64, cir.pragma_clang_bss_section = ".mybss", cir.pragma_clang_data_section = ".mydata", cir.pragma_clang_relro_section = ".myrelro", cir.pragma_clang_rodata_section = ".myro"}
// CIR-DAG: cir.global "private" constant internal {{.*}}@locals.l_ro = #cir.int<4> : !s32i {alignment = 4 : i64, cir.pragma_clang_bss_section = ".mybss", cir.pragma_clang_data_section = ".mydata", cir.pragma_clang_relro_section = ".myrelro", cir.pragma_clang_rodata_section = ".myro"}
// CIR-DAG: cir.global external @g_explicit = #cir.int<5> : !s32i {alignment = 4 : i64, section = ".explicit_data"}
// CIR-DAG: cir.func no_inline optnone dso_local @pragma_text_fn() section(".mytext")
// CIR-DAG: cir.func no_inline optnone dso_local @locals() -> !s32i section(".mytext")
// CIR-DAG: cir.func no_inline optnone dso_local @explicit_text_fn() section(".explicit_text")

// LLVM-DAG: @g_data = global i32 1, align 4 #0
// LLVM-DAG: @g_ro = constant i32 2, align 4 #0
// LLVM-DAG: @locals.l_bss = internal global i32 0, align 4 #0
// LLVM-DAG: @locals.l_data = internal global i32 3, align 4 #0
// LLVM-DAG: @locals.l_ro = internal constant i32 4, align 4 #0
// LLVM-DAG: @g_bss = global i32 0, align 4 #0
// LLVM-DAG: @g_explicit = global i32 5, section ".explicit_data", align 4
// LLVM: define dso_local void @pragma_text_fn() #1 section ".mytext"
// LLVM: define dso_local i32 @locals() #1 section ".mytext"
// LLVM: define dso_local void @explicit_text_fn() #1 section ".explicit_text"
// LLVM: attributes #0 = { "bss-section"=".mybss" "data-section"=".mydata" "relro-section"=".myrelro" "rodata-section"=".myro" }

// OGCG-DAG: @g_data = global i32 1, align 4 #0
// OGCG-DAG: @g_ro = constant i32 2, align 4 #0
// OGCG-DAG: @locals.l_bss = internal global i32 0, align 4 #0
// OGCG-DAG: @locals.l_data = internal global i32 3, align 4 #0
// OGCG-DAG: @locals.l_ro = internal constant i32 4, align 4 #0
// OGCG-DAG: @g_bss = global i32 0, align 4 #0
// OGCG-DAG: @g_explicit = global i32 5, section ".explicit_data", align 4
// OGCG: define dso_local void @pragma_text_fn() #1 section ".mytext"
// OGCG: define dso_local i32 @locals() #1 section ".mytext"
// OGCG: define dso_local void @explicit_text_fn() #1 section ".explicit_text"
// OGCG: attributes #0 = { "bss-section"=".mybss" "data-section"=".mydata" "relro-section"=".myrelro" "rodata-section"=".myro" }
