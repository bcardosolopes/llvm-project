// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Synthesized copy ctor memcpy for POD blocks should have alignment

struct NonPOD {
    NonPOD();
    NonPOD(const NonPOD&);
    ~NonPOD();
};

// Basic struct: two blocks of 4 ints (16 bytes each, align 4)
struct Basic {
    int a, b, c, d;
    NonPOD np;
    int w, x, y, z;
};

Basic copy_basic(const Basic& src) { return src; }

// CIR-LABEL: define {{.*}} @_ZN5BasicC2ERKS_(
// CIR: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// CIR: call void @_ZN6NonPODC1ERKS_
// CIR: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// CIR: ret void

// OGCG-LABEL: define {{.*}} @_ZN5BasicC2ERKS_(
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// OGCG: call void @_ZN6NonPODC1ERKS_
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 4 %{{.*}}, ptr align 4 %{{.*}}, i64 16, i1 false)
// OGCG: ret void

// Packed struct: align 1
struct __attribute__((packed)) PackedMembers {
    char c;
    NonPOD np;
    int w, x, y, z;
};

PackedMembers copy_packed(const PackedMembers& src) { return src; }

// CIR-LABEL: define {{.*}} @_ZN13PackedMembersC2ERKS_(
// CIR: call void @_ZN6NonPODC1ERKS_
// CIR: call void @llvm.memcpy{{.*}}(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 16, i1 false)
// CIR: ret void

// OGCG-LABEL: define {{.*}} @_ZN13PackedMembersC2ERKS_(
// OGCG: call void @_ZN6NonPODC1ERKS_
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 1 %{{.*}}, ptr align 1 %{{.*}}, i64 16, i1 false)
// OGCG: ret void

// Reference members: two blocks of 2 references (16 bytes each, align 8)
struct ReferenceMember {
    ReferenceMember(int &a, int &b, int &c, int &d)
        : a(a), b(b), c(c), d(d) {}
    int &a;
    int &b;
    NonPOD np;
    int &c;
    int &d;
};

ReferenceMember copy_refs(const ReferenceMember& src) { return src; }

// CIR-LABEL: define {{.*}} @_ZN15ReferenceMemberC2ERKS_(
// CIR: call void @llvm.memcpy{{.*}}(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
// CIR: call void @_ZN6NonPODC1ERKS_
// CIR: call void @llvm.memcpy{{.*}}(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
// CIR: ret void

// OGCG-LABEL: define {{.*}} @_ZN15ReferenceMemberC2ERKS_(
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
// OGCG: call void @_ZN6NonPODC1ERKS_
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
// OGCG: ret void
