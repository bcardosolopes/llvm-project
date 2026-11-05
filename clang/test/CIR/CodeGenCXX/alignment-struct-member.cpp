// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin10 -fclangir -emit-llvm -o %t.cir.ll
// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin10 -emit-llvm -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that memcpy source alignment for struct member accesses accounts for
// the actual pointer alignment and member offset, matching OGCG behavior.
// Specifically, when accessing bArray (at offset 16 in B's non-virtual part)
// through a B* pointer or via a derived-to-base cast, the alignment should
// reflect getNonVirtualAlignment (8) rather than the full struct alignment (16).

namespace test1 {
  struct Array { int elts[4]; };
  struct A { __attribute__((aligned(16))) Array aArray; };
  struct B : virtual A {
    void *bPointer; // puts bArray at offset 16 within B's non-virtual part
    Array bArray;
  };
  struct C : virtual A {
    void *cPointer1;
    void *cPointer2;
  };
  struct D : C, B {};
  typedef __attribute__((aligned(64))) Array AlignedArray;

  // Accessing bArray via B* pointer: alignment should be 8 (not 16).
  // B* has assumed alignment = getNonVirtualAlignment(B) = 8.
  // bArray is at offset 16 in B, so gcd(8, 16) = 8.
  // CIR-LABEL: @_ZN5test11dEPNS_1BE
  // CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 8 {{%.*}}, i64 16, i1 false)
  // OGCG-LABEL: @_ZN5test11dEPNS_1BE
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 8 {{%.*}}, i64 16, i1 false)
  void d(B *b) { AlignedArray result = b->bArray; }

  // Accessing bArray on local D via derived-to-base cast: alignment should be 8.
  // D has align 16. B subobject is at offset 24 in D.
  // So B subobject alignment = gcd(16, 24) = 8.
  // bArray is at offset 16 in B, so gcd(8, 16) = 8.
  // CIR-LABEL: @_ZN5test11gEv
  // CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 8 {{%.*}}, i64 16, i1 false)
  // OGCG-LABEL: @_ZN5test11gEv
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 8 {{%.*}}, i64 16, i1 false)
  void g() { D d; AlignedArray result = d.bArray; }

  // Accessing bArray via B& reference: alignment should be 8.
  // CIR-LABEL: @_ZN5test11cERNS_1BE
  // CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 8 {{%.*}}, i64 16, i1 false)
  // OGCG-LABEL: @_ZN5test11cERNS_1BE
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 8 {{%.*}}, i64 16, i1 false)
  void c(B &b) { AlignedArray result = b.bArray; }

  // Accessing bArray on local B: alignment should be 16.
  // Local B has align 16. bArray is at offset 16, so gcd(16, 16) = 16.
  // CIR-LABEL: @_ZN5test11eEv
  // CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 16 {{%.*}}, i64 16, i1 false)
  // OGCG-LABEL: @_ZN5test11eEv
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 64 {{%.*}}, ptr align 16 {{%.*}}, i64 16, i1 false)
  void e() { B b; AlignedArray result = b.bArray; }
}
