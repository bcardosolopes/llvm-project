// Test -fsanitize-memory-use-after-dtor emits __sanitizer_dtor_callback_fields
// and __sanitizer_dtor_callback_vptr calls through CIR.

// RUN: %clang_cc1 -triple x86_64-pc-linux -std=c++11 -fclangir \
// RUN:   -fsanitize=memory -fsanitize-memory-use-after-dtor \
// RUN:   -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM \
// RUN:   --implicit-check-not="call void @__sanitizer_"

// Simple class with a non-trivial dtor - should poison the field.
struct Simple {
  int x;
  ~Simple() {}
};
Simple s;

// Class with vtable - should poison fields and vptr.
class Polymorphic {
public:
  int a;
  virtual ~Polymorphic() {}
};
Polymorphic p;

// Derived class with virtual base - complete dtor should poison vptr.
class VBase {
public:
  int v;
  virtual ~VBase() {}
};

class Derived : public virtual VBase {
public:
  int d;
  ~Derived() {}
};
Derived dd;

// Class with non-trivial member separating trivial fields.
// Should produce two separate __sanitizer_dtor_callback_fields calls.
template <class T>
class Vector {
public:
  int size;
  ~Vector() { size += 1; }
};

struct Mixed {
  int x;
  Vector<int> v;
  int y;
  ~Mixed() {}
};
Mixed m;

// ---- Simple: base dtor poisons single int field ----
// LLVM-LABEL: define {{.*}} @_ZN6SimpleD2Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN:[0-9]+]]
// LLVM: ret void

// LLVM-LABEL: define {{.*}} @_ZN6SimpleD1Ev(
// LLVM: call void {{.*}}@_ZN6SimpleD2Ev(
// LLVM: ret void

// ---- Polymorphic: base dtor poisons field and vptr ----
// LLVM-LABEL: define {{.*}} @_ZN11PolymorphicD2Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN]]
// LLVM: call void @__sanitizer_dtor_callback_vptr(ptr %{{.*}}){{.*}}!nosanitize ![[NOSAN]]
// LLVM: ret void

// ---- Derived: base dtor poisons its own field ----
// LLVM-LABEL: define {{.*}} @_ZN7DerivedD2Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN]]
// LLVM: ret void

// ---- VBase: base dtor poisons field and vptr ----
// LLVM-LABEL: define {{.*}} @_ZN5VBaseD2Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN]]
// LLVM: call void @__sanitizer_dtor_callback_vptr(ptr %{{.*}}){{.*}}!nosanitize ![[NOSAN]]
// LLVM: ret void

// ---- Derived: complete dtor destructs VBase then poisons vptr ----
// LLVM-LABEL: define {{.*}} @_ZN7DerivedD1Ev(
// LLVM: call void {{.*}}@_ZN7DerivedD2Ev(
// LLVM: call void {{.*}}@_ZN5VBaseD2Ev(
// LLVM: call void @__sanitizer_dtor_callback_vptr(ptr %{{.*}}){{.*}}!nosanitize ![[NOSAN]]
// LLVM: ret void

// ---- Vector<int>: base dtor poisons its field ----
// LLVM-LABEL: define {{.*}} @_ZN6VectorIiED2Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN]]
// LLVM: ret void

// ---- Mixed: field batching - poison y, call Vector dtor, poison x ----
// (Cleanups are popped in reverse construction order)
// LLVM-LABEL: define {{.*}} @_ZN5MixedD2Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN]]
// LLVM: call void {{.*}}@_ZN6VectorIiED1Ev(
// LLVM: call void @__sanitizer_dtor_callback_fields(ptr %{{.*}}, i64 4){{.*}}!nosanitize ![[NOSAN]]
// LLVM: ret void
