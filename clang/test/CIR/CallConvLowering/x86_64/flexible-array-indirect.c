// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify flexible-array-member records are classified as MEMORY in CIR x86_64
// ABI lowering (indirect byval/sret), matching OGCG.
//
// This covers:
// - direct FAM records,
// - a wrapper record that embeds the FAM,
// - forward declaration path (definition appears after declarations).

struct FAM;
void take_fwd(struct FAM);
struct FAM ret_fwd(void);

struct FAM {
  int n;
  int data[];
};

struct Wrapper {
  double d;
  struct FAM fam;
};

void take_fam(struct FAM);
struct FAM ret_fam(void);
void take_wrap(struct Wrapper);
struct Wrapper ret_wrap(void);

// CIR-LABEL: @call_all(
// CIR: call void @take_fwd(ptr noundef byval(%struct.FAM) align 8
// CIR: call void @take_fam(ptr noundef byval(%struct.FAM) align 8
// CIR: call void @take_wrap(ptr noundef byval(%struct.Wrapper) align 8
// OGCG-LABEL: @call_all(
// OGCG: call void @take_fwd(ptr noundef byval(%struct.FAM) align 8
// OGCG: call void @take_fam(ptr noundef byval(%struct.FAM) align 8
// OGCG: call void @take_wrap(ptr noundef byval(%struct.Wrapper) align 8
void call_all(struct FAM f, struct Wrapper w) {
  take_fwd(f);
  take_fam(f);
  take_wrap(w);
}

// CIR-LABEL: @ret_all(
// CIR: call void @ret_fwd(ptr dead_on_unwind writable sret(%struct.FAM) align 4
// CIR: call void @ret_fam(ptr dead_on_unwind writable sret(%struct.FAM) align 4
// CIR: call void @ret_wrap(ptr dead_on_unwind writable sret(%struct.Wrapper) align 8
// OGCG-LABEL: @ret_all(
// OGCG: call void @ret_fwd(ptr dead_on_unwind writable sret(%struct.FAM) align 4
// OGCG: call void @ret_fam(ptr dead_on_unwind writable sret(%struct.FAM) align 4
// OGCG: call void @ret_wrap(ptr dead_on_unwind writable sret(%struct.Wrapper) align 8
void ret_all(void) {
  (void)ret_fwd();
  (void)ret_fam();
  (void)ret_wrap();
}
