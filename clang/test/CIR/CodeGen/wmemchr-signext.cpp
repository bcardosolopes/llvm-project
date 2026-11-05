// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// wchar_t and char32_t parameters/return values should have
// signext/zeroext matching OGCG. CIR's type system maps these to
// !cir.int<s,32> / !cir.int<u,32>, indistinguishable from int/unsigned,
// so CIR codegen marks them with cir.promotable for the target lowering
// pass to emit the correct ABI attributes.

extern "C" wchar_t *wmemchr(const wchar_t *, wchar_t, unsigned long);

void test_wmemchr(const wchar_t *WC) {
  wmemchr(WC, (int)'a', 10);
}
// CIR: call ptr @wmemchr({{.*}}i32 noundef signext 97{{.*}})
// OGCG: call ptr @wmemchr({{.*}}i32 noundef signext 97{{.*}})

wchar_t identity_wchar(wchar_t c) { return c; }
// CIR-LABEL: define {{.*}} signext i32 @_Z14identity_wcharw(i32 noundef signext
// OGCG-LABEL: define {{.*}} signext i32 @_Z14identity_wcharw(i32 noundef signext

char32_t identity_char32(char32_t c) { return c; }
// CIR-LABEL: define {{.*}} zeroext i32 @_Z15identity_char32Di(i32 noundef zeroext
// OGCG-LABEL: define {{.*}} zeroext i32 @_Z15identity_char32Di(i32 noundef zeroext

// Verify that plain 'int' is NOT affected.
int identity_int(int c) { return c; }
// CIR-LABEL: define {{.*}} i32 @_Z12identity_inti(i32 noundef
// OGCG-LABEL: define {{.*}} i32 @_Z12identity_inti(i32 noundef
