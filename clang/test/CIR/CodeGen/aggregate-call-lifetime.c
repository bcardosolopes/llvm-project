// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O1 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct S {
  int ns[40];
};

struct S foo(void);
struct S foo_int(int);

__attribute__((optnone))
struct S bar(void) {
  struct S r;
  r = foo();
  r = foo();
  r = foo();
  return r;
}

__attribute__((optnone))
struct S baz(int i, volatile int *j) {
  struct S r;

  do {
    r = foo_int(({
      if (*j)
        break;
      i++;
    }));

    r = foo_int(i++);
  } while (1);

  return r;
}

// CIR-LABEL: cir.func no_inline optnone dso_local @bar(
// CIR:   %[[TMP0:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR:   %[[TMP1:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR:   %[[TMP2:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR:   cir.lifetime.start %[[TMP0]] : !cir.ptr<!rec_S>
// CIR:   cir.call @foo(%[[TMP0]]) : (!cir.ptr<!rec_S>) -> ()
// CIR:   cir.copy %[[TMP0]]
// CIR:   cir.lifetime.end %[[TMP0]] : !cir.ptr<!rec_S>
// CIR:   cir.lifetime.start %[[TMP1]] : !cir.ptr<!rec_S>
// CIR:   cir.call @foo(%[[TMP1]]) : (!cir.ptr<!rec_S>) -> ()
// CIR:   cir.copy %[[TMP1]]
// CIR:   cir.lifetime.end %[[TMP1]] : !cir.ptr<!rec_S>
// CIR:   cir.lifetime.start %[[TMP2]] : !cir.ptr<!rec_S>
// CIR:   cir.call @foo(%[[TMP2]]) : (!cir.ptr<!rec_S>) -> ()
// CIR:   cir.copy %[[TMP2]]
// CIR:   cir.lifetime.end %[[TMP2]] : !cir.ptr<!rec_S>

// CIR-LABEL: cir.func no_inline optnone dso_local @baz(
// CIR:       %[[TMP0:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR:       %[[TMP1:.*]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR:       cir.lifetime.start %[[TMP0]] : !cir.ptr<!rec_S>
// CIR:       cir.if
// CIR:         cir.lifetime.end %[[TMP0]] : !cir.ptr<!rec_S>
// CIR:         cir.break
// CIR:       cir.call @foo_int(%[[TMP0]]
// CIR:       cir.copy %[[TMP0]]
// CIR:       cir.lifetime.end %[[TMP0]] : !cir.ptr<!rec_S>
// CIR:       cir.lifetime.start %[[TMP1]] : !cir.ptr<!rec_S>
// CIR:       cir.call @foo_int(%[[TMP1]]
// CIR:       cir.copy %[[TMP1]]
// CIR:       cir.lifetime.end %[[TMP1]] : !cir.ptr<!rec_S>

// LLVM-LABEL: define dso_local void @bar(
// LLVM:   %[[TMP0:.*]] = alloca %struct.S
// LLVM:   %[[TMP1:.*]] = alloca %struct.S
// LLVM:   %[[TMP2:.*]] = alloca %struct.S
// LLVM:   call void @llvm.lifetime.start.p0(ptr %[[TMP0]])
// LLVM:   call void @foo(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP0]])
// LLVM:   call void @llvm.memcpy
// LLVM:   call void @llvm.lifetime.end.p0(ptr %[[TMP0]])
// LLVM:   call void @llvm.lifetime.start.p0(ptr %[[TMP1]])
// LLVM:   call void @foo(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP1]])
// LLVM:   call void @llvm.memcpy
// LLVM:   call void @llvm.lifetime.end.p0(ptr %[[TMP1]])
// LLVM:   call void @llvm.lifetime.start.p0(ptr %[[TMP2]])
// LLVM:   call void @foo(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP2]])
// LLVM:   call void @llvm.memcpy
// LLVM:   call void @llvm.lifetime.end.p0(ptr %[[TMP2]])

// LLVM-LABEL: define dso_local void @baz(
// LLVM:       %[[TMP0:.*]] = alloca %struct.S
// LLVM:       %[[TMP1:.*]] = alloca %struct.S
// LLVM:       call void @llvm.lifetime.start.p0(ptr %[[TMP0]])
// LLVM:       br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
// LLVM:       call void @llvm.lifetime.end.p0(ptr %[[TMP0]])
// LLVM:       call void @foo_int(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP0]]
// LLVM:       call void @llvm.memcpy
// LLVM:       call void @llvm.lifetime.end.p0(ptr %[[TMP0]])
// LLVM:       call void @llvm.lifetime.start.p0(ptr %[[TMP1]])
// LLVM:       call void @foo_int(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP1]]
// LLVM:       call void @llvm.memcpy
// LLVM:       call void @llvm.lifetime.end.p0(ptr %[[TMP1]])

// OGCG-LABEL: define dso_local void @bar(
// OGCG:   %[[TMP0:.*]] = alloca %struct.S
// OGCG:   %[[TMP1:.*]] = alloca %struct.S
// OGCG:   %[[TMP2:.*]] = alloca %struct.S
// OGCG:   call void @llvm.lifetime.start.p0(ptr %[[TMP0]])
// OGCG:   call void @foo(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP0]])
// OGCG:   call void @llvm.memcpy
// OGCG:   call void @llvm.lifetime.end.p0(ptr %[[TMP0]])
// OGCG:   call void @llvm.lifetime.start.p0(ptr %[[TMP1]])
// OGCG:   call void @foo(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP1]])
// OGCG:   call void @llvm.memcpy
// OGCG:   call void @llvm.lifetime.end.p0(ptr %[[TMP1]])
// OGCG:   call void @llvm.lifetime.start.p0(ptr %[[TMP2]])
// OGCG:   call void @foo(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP2]])
// OGCG:   call void @llvm.memcpy
// OGCG:   call void @llvm.lifetime.end.p0(ptr %[[TMP2]])

// OGCG-LABEL: define dso_local void @baz(
// OGCG:       %[[TMP0:.*]] = alloca %struct.S
// OGCG:       %[[TMP1:.*]] = alloca %struct.S
// OGCG:       call void @llvm.lifetime.start.p0(ptr %[[TMP0]])
// OGCG:       br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
// OGCG:       call void @llvm.lifetime.end.p0(ptr %[[TMP0]])
// OGCG:       call void @foo_int(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP0]]
// OGCG:       call void @llvm.memcpy
// OGCG:       call void @llvm.lifetime.end.p0(ptr %[[TMP0]])
// OGCG:       call void @llvm.lifetime.start.p0(ptr %[[TMP1]])
// OGCG:       call void @foo_int(ptr dead_on_unwind writable sret(%struct.S) align 4 %[[TMP1]]
// OGCG:       call void @llvm.memcpy
// OGCG:       call void @llvm.lifetime.end.p0(ptr %[[TMP1]])
