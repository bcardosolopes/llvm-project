// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Virtual calls with sret return should have 'this' pointer attributes
// on the 'this' parameter (index 1), not on the sret parameter (index 0).

struct Large { int data[100]; };
struct Poly {
  virtual Large get_large() { return Large{}; }
  virtual int get_val() { return 42; }
  virtual ~Poly() {}
};

// Sret virtual call: this attrs should be on param 1, not param 0
Large test_sret_vcall(Poly *p) { return p->get_large(); }

// CIR-LABEL: @_Z15test_sret_vcallP4Poly
// CIR: call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.Large) align 4 %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8)
// OGCG-LABEL: @_Z15test_sret_vcallP4Poly
// OGCG: call void %{{.*}}(ptr {{.*}} %{{.*}}, ptr noundef nonnull align 8 dereferenceable(8)

// Non-sret virtual call: this attrs should be on param 0 (no sret)
int test_no_sret(Poly *p) { return p->get_val(); }

// CIR-LABEL: @_Z12test_no_sretP4Poly
// CIR: call noundef i32 %{{.*}}(ptr noundef nonnull align 8 dereferenceable(8)
// OGCG-LABEL: @_Z12test_no_sretP4Poly
// OGCG: call noundef i32 %{{.*}}(ptr noundef nonnull align 8 dereferenceable(8)
