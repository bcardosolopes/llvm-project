// RUN: %clang_cc1 -std=c++17 -mconstructor-aliases -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

int strlen(char const *);

struct String {
  long size;
  long capacity;

  String() : size{0}, capacity{0} {}
  String(char const *s) : size{strlen(s)}, capacity{size} {}

  // StringView's trivial move assignment is lowered directly to cir.copy.
  // StringView::StringView(String const&) is checked at the end of this file,
  // where it is emitted.
};

struct StringView {
  long size;

  StringView(const String &s) : size{s.size} {}
  StringView() : size{0} {}
};

int main() {
  StringView sv;
  {
    String s = "Hi";
    sv = s;
  }
}

// CHECK: cir.func {{.*}} @main() -> (!s32i {cir.noundef})
// CHECK:     %0 = cir.alloca !rec_StringView, !cir.ptr<!rec_StringView>, ["sv", init] {alignment = 8 : i64}
// CHECK:     %1 = cir.const #cir.int<0> : !s32i
// Neither String nor StringView has a virtual base, so their complete-object
// constructors are folded into the base ones and the calls target C2.
// CHECK:     cir.call @_ZN10StringViewC2Ev(%0) : (!cir.ptr<!rec_StringView>) -> ()
// CHECK:     cir.scope {
// CHECK:       cir.alloca !rec_String, !cir.ptr<!rec_String>, ["s", init] {alignment = 8 : i64}
// CHECK:       cir.alloca !rec_StringView, !cir.ptr<!rec_StringView>, ["ref.tmp0"] {alignment = 8 : i64}
// CHECK:       cir.get_global @".str" : !cir.ptr<!cir.array<!s8i x 3>>
// CHECK:       cir.cast array_to_ptrdecay %{{.*}} : !cir.ptr<!cir.array<!s8i x 3>> -> !cir.ptr<!s8i>
// CHECK:       cir.call @_ZN6StringC2EPKc({{.*}}) : (!cir.ptr<!rec_String>, !cir.ptr<!s8i>) -> ()
// CHECK:       cir.call @_ZN10StringViewC2ERK6String({{.*}}) : (!cir.ptr<!rec_StringView>, !cir.ptr<!rec_String>) -> ()
// CHECK:       cir.copy %{{.*}} to %0 {dst_alignment = 8 : i64, src_alignment = 8 : i64} : !cir.ptr<!rec_StringView>
// CHECK:     }
// CHECK:     cir.return %1 : !s32i
// CHECK: }

struct HasNonTrivialAssignOp {
  HasNonTrivialAssignOp &operator=(const HasNonTrivialAssignOp &);
};

struct ContainsNonTrivial {
  HasNonTrivialAssignOp start;
  int i;
  int *j;
  HasNonTrivialAssignOp middle;
  int k : 4;
  int l : 4;
  int m : 4;
  HasNonTrivialAssignOp end;
  ContainsNonTrivial &operator=(const ContainsNonTrivial &);
};

// CHECK-LABEL: cir.func {{.*}} @_ZN18ContainsNonTrivialaSERKS_(
// CHECK-SAME:    special_member<#cir.cxx_assign<!rec_ContainsNonTrivial, copy>>
// CHECK-NEXT:    %[[#THIS:]] = cir.alloca !cir.ptr<!rec_ContainsNonTrivial>
// CHECK-NEXT:    %[[#OTHER:]] = cir.alloca !cir.ptr<!rec_ContainsNonTrivial>
// CHECK-NEXT:    cir.store{{.*}} %arg0, %[[#THIS]]
// CHECK-NEXT:    cir.store{{.*}} %arg1, %[[#OTHER]]
// CHECK-NEXT:    %[[#THIS_LOAD:]] = cir.load{{.*}} deref %[[#THIS]]
// CHECK-NEXT:    %[[#THIS_START:]] = cir.cast bitcast %[[#THIS_LOAD]] : !cir.ptr<!rec_ContainsNonTrivial> -> !cir.ptr<!rec_HasNonTrivialAssignOp>
// CHECK-NEXT:    %[[#OTHER_LOAD:]] = cir.load{{.*}} %[[#OTHER]]
// CHECK-NEXT:    %[[#OTHER_START:]] = cir.cast bitcast %[[#OTHER_LOAD]] : !cir.ptr<!rec_ContainsNonTrivial> -> !cir.ptr<!rec_HasNonTrivialAssignOp>
// CHECK-NEXT:    cir.call @_ZN21HasNonTrivialAssignOpaSERKS_(%[[#THIS_START]], %[[#OTHER_START]])
// CHECK:         %[[#THIS_I:]] = cir.get_member %[[#THIS_LOAD]][1] {name = "i"}
// CHECK-NEXT:    %[[#OTHER_COPY:]] = cir.load{{.*}} %[[#OTHER]]
// CHECK-NEXT:    %[[#OTHER_I:]] = cir.get_member %[[#OTHER_COPY]][1] {name = "i"}
// CHECK-NEXT:    %[[#COPY_SIZE:]] = cir.const #cir.int<12> : !u64i
// CHECK-NEXT:    %{{.*}} = cir.cast bitcast %[[#THIS_I]] : !cir.ptr<!s32i> -> !cir.ptr<!void>
// CHECK-NEXT:    %{{.*}} = cir.cast bitcast %[[#OTHER_I]] : !cir.ptr<!s32i> -> !cir.ptr<!void>
// CHECK-NEXT:    cir.libc.memcpy %[[#COPY_SIZE]] bytes from %{{.*}} to %{{.*}}
// CHECK:         %[[#THIS_MIDDLE:]] = cir.cast bitcast %{{.*}} : !cir.ptr<!u8i> -> !cir.ptr<!rec_HasNonTrivialAssignOp>
// CHECK:         %[[#OTHER_MIDDLE:]] = cir.cast bitcast %{{.*}} : !cir.ptr<!u8i> -> !cir.ptr<!rec_HasNonTrivialAssignOp>
// CHECK-NEXT:    cir.call @_ZN21HasNonTrivialAssignOpaSERKS_(%[[#THIS_MIDDLE]], %[[#OTHER_MIDDLE]])
// CHECK:         %[[#THIS_K:]] = cir.get_member %[[#THIS_LOAD]][4] {name = "k"}
// CHECK-NEXT:    %[[#OTHER_BITS:]] = cir.load{{.*}} %[[#OTHER]]
// CHECK-NEXT:    %[[#OTHER_K:]] = cir.get_member %[[#OTHER_BITS]][4] {name = "k"}
// CHECK-NEXT:    %[[#BITS_SIZE:]] = cir.const #cir.int<2> : !u64i
// CHECK-NEXT:    %{{.*}} = cir.cast bitcast %[[#THIS_K]] : !cir.ptr<!u16i> -> !cir.ptr<!void>
// CHECK-NEXT:    %{{.*}} = cir.cast bitcast %[[#OTHER_K]] : !cir.ptr<!u16i> -> !cir.ptr<!void>
// CHECK-NEXT:    cir.libc.memcpy %[[#BITS_SIZE]] bytes from %{{.*}} to %{{.*}}
// CHECK:         %[[#THIS_END:]] = cir.cast bitcast %{{.*}} : !cir.ptr<!u8i> -> !cir.ptr<!rec_HasNonTrivialAssignOp>
// CHECK:         %[[#OTHER_END:]] = cir.cast bitcast %{{.*}} : !cir.ptr<!u8i> -> !cir.ptr<!rec_HasNonTrivialAssignOp>
// CHECK-NEXT:    cir.call @_ZN21HasNonTrivialAssignOpaSERKS_(%[[#THIS_END]], %[[#OTHER_END]])
// CHECK-NEXT:    cir.return %[[#THIS_LOAD]]
// CHECK-NEXT:  }
ContainsNonTrivial &
ContainsNonTrivial::operator=(const ContainsNonTrivial &) = default;

struct Trivial {
  int i;
  int *j;
  double k;
  int l[3];
};

// Defaulted copy assignment is lowered directly to cir.copy for fully-trivial
// records.
// CHECK-LABEL: cir.func {{.*}} @_Z11copyTrivialR7TrivialS0_(
// CHECK:         cir.copy %{{.*}} to %{{.*}} : !cir.ptr<!rec_Trivial>
void copyTrivial(Trivial &a, Trivial &b) {
  a = b;
}

struct ContainsTrivial {
  Trivial t1;
  Trivial t2;
  ContainsTrivial &operator=(const ContainsTrivial &);
};

// Defaulted copy assignment is memcpyized when the whole record is memcpyable.
// CHECK-LABEL: cir.func {{.*}} @_ZN15ContainsTrivialaSERKS_(
// CHECK-SAME:    special_member<#cir.cxx_assign<!rec_ContainsTrivial, copy>>
// CHECK:         %[[#THIS_LOAD:]] = cir.load{{.*}} deref %[[#]]
// CHECK-NEXT:    %[[#THIS_T1:]] = cir.get_member %[[#THIS_LOAD]][0] {name = "t1"}
// CHECK-NEXT:    %[[#OTHER_LOAD:]] = cir.load{{.*}} %[[#]]
// CHECK-NEXT:    %[[#OTHER_T1:]] = cir.get_member %[[#OTHER_LOAD]][0] {name = "t1"}
// CHECK-NEXT:    %[[#MEMCPY_SIZE:]] = cir.const #cir.int<80> : !u64i
// CHECK-NEXT:    %[[#THIS_T1_CAST:]] = cir.cast bitcast %[[#THIS_T1]] : !cir.ptr<!rec_Trivial> -> !cir.ptr<!void>
// CHECK-NEXT:    %[[#OTHER_T1_CAST:]] = cir.cast bitcast %[[#OTHER_T1]] : !cir.ptr<!rec_Trivial> -> !cir.ptr<!void>
// CHECK-NEXT:    cir.libc.memcpy %[[#MEMCPY_SIZE]] bytes from %[[#OTHER_T1_CAST]] to %[[#THIS_T1_CAST]]
ContainsTrivial &ContainsTrivial::operator=(const ContainsTrivial &) = default;

struct ContainsTrivialArray {
  Trivial arr[2];
  ContainsTrivialArray &operator=(const ContainsTrivialArray &);
};

// We should be calling operator= here but don't currently.
// CHECK-LABEL: cir.func {{.*}} @_ZN20ContainsTrivialArrayaSERKS_(
// CHECK-SAME:    special_member<#cir.cxx_assign<!rec_ContainsTrivialArray, copy>>
// CHECK:         %[[#THIS_LOAD:]] = cir.load{{.*}} deref %[[#]]
// CHECK-NEXT:    %[[#THIS_ARR:]] = cir.get_member %[[#THIS_LOAD]][0] {name = "arr"}
// CHECK-NEXT:    %[[#THIS_ARR_CAST:]] = cir.cast bitcast %[[#THIS_ARR]] : !cir.ptr<!cir.array<!rec_Trivial x 2>> -> !cir.ptr<!void>
// CHECK-NEXT:    %[[#OTHER_LOAD:]] = cir.load{{.*}} %[[#]]
// CHECK-NEXT:    %[[#OTHER_ARR:]] = cir.get_member %[[#OTHER_LOAD]][0] {name = "arr"}
// CHECK-NEXT:    %[[#OTHER_ARR_CAST:]] = cir.cast bitcast %[[#OTHER_ARR]] : !cir.ptr<!cir.array<!rec_Trivial x 2>> -> !cir.ptr<!void>
// CHECK-NEXT:    %[[#MEMCPY_SIZE:]] = cir.const #cir.int<80> : !u64i
// CHECK-NEXT:    cir.libc.memcpy %[[#MEMCPY_SIZE]] bytes from %[[#OTHER_ARR_CAST]] to %[[#THIS_ARR_CAST]]
ContainsTrivialArray &
ContainsTrivialArray::operator=(const ContainsTrivialArray &) = default;

// StringView::StringView(String const&), emitted after every user.
//
// CHECK: cir.func {{.*}} @_ZN10StringViewC2ERK6String
// CHECK:   %0 = cir.alloca !cir.ptr<!rec_StringView>, !cir.ptr<!cir.ptr<!rec_StringView>>, ["this", init] {alignment = 8 : i64}
// CHECK:   %1 = cir.alloca !cir.ptr<!rec_String>, !cir.ptr<!cir.ptr<!rec_String>>, ["s", init, const] {alignment = 8 : i64}
// CHECK:   cir.store{{.*}} %arg0, %0 : !cir.ptr<!rec_StringView>
// CHECK:   cir.store{{.*}} %arg1, %1 : !cir.ptr<!rec_String>
// CHECK:   %2 = cir.load{{.*}} %0 : !cir.ptr<!cir.ptr<!rec_StringView>>

// Get address of `this->size`

// CHECK:   %3 = cir.get_member %2[0] {name = "size"}

// Get address of `s`

// CHECK:   %4 = cir.load{{.*}} %1 : !cir.ptr<!cir.ptr<!rec_String>>

// Get the address of s.size

// CHECK:   %5 = cir.get_member %4[0] {name = "size"}

// Load value from s.size and store in this->size

// CHECK:   %6 = cir.load{{.*}} %5 : !cir.ptr<!s64i>, !s64i
// CHECK:   cir.store{{.*}} %6, %3 : !s64i, !cir.ptr<!s64i>
// CHECK:   cir.return
// CHECK: }
