// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

struct S {
  int x;
  int y;
};

void f1(struct S);
void f2(void) {
  struct S s;
  f1(s);
}

// CIR-LABEL: cir.func{{.*}} @f2(){{.*}} {
// CIR:         %[[COERCED:.+]] = cir.load align(4) %{{.+}} : !cir.ptr<!u64i>, !u64i
// CIR-NEXT:    cir.call @f1(%[[COERCED]]) : (!u64i) -> ()

// LLVM-LABEL: define{{.*}} void @f2(){{.*}}
// LLVM:         %[[COERCED:.+]] = load i64, ptr %{{.+}}, align 4
// LLVM-NEXT:    call void @f1(i64 %[[COERCED]])

// OGCG-LABEL: define{{.*}} void @f2()
// OGCG:         %[[S:.+]] = load i64, ptr %{{.+}}, align 4
// OGCG-NEXT:    call void @f1(i64 %[[S]])

struct S f3(void);
void f4(void) {
  struct S s = f3();
}

// CIR-LABEL: cir.func{{.*}} @f4(){{.*}} {
// CIR:         %[[S:.+]] = cir.call @f3() : () -> !u64i
// CIR:         cir.store align(4) %[[S]], %{{.+}} : !u64i, !cir.ptr<!u64i>

// LLVM-LABEL: define{{.*}} void @f4(){{.*}} {
// LLVM:         %[[S:.+]] = call i64 @f3()
// LLVM-NEXT:    store i64 %[[S]], ptr %{{.+}}, align 4

// OGCG-LABEL: define{{.*}} void @f4() #0 {
// OGCG:         %[[S:.+]] = call i64 @f3()
// OGCG-NEXT:    store i64 %[[S]], ptr %{{.+}}, align 4

struct Big {
  int data[10];
};

void f5(struct Big);
struct Big f6(void);

void f7(void) {
  struct Big b;
  f5(b);
}

// CIR-LABEL: cir.func{{.*}} @f7(){{.*}} {
// CIR:         cir.call @f5(%{{.+}}) : (!cir.ptr<!rec_Big>) -> ()

// LLVM-LABEL: define{{.*}} void @f7(){{.*}} {
// LLVM:         call void @f5(ptr noundef byval(%struct.Big) align 8 %{{.+}})

// OGCG-LABEL: define{{.*}} void @f7() #0 {
// OGCG:         %[[B:.+]] = alloca %struct.Big, align 8
// OGCG-NEXT:    call void @f5(ptr noundef byval(%struct.Big) align 8 %[[B]])

void f8(void) {
  struct Big b = f6();
}

// CIR-LABEL: cir.func{{.*}} @f8(){{.*}} {
// CIR:         cir.call @f6(%{{.+}}) : (!cir.ptr<!rec_Big>) -> ()

// LLVM-LABEL: define{{.*}} void @f8(){{.*}} {
// LLVM:         call void @f6(ptr dead_on_unwind writable sret(%struct.Big) align 4 %{{.+}})

// OGCG-LABEL: define{{.*}} void @f8() #0 {
// OGCG:         %[[B:.+]] = alloca %struct.Big, align 4
// OGCG-NEXT:    call void @f6(ptr dead_on_unwind writable sret(%struct.Big) align 4 %[[B]])

void f9(void) {
  f1(f3());
}

// CIR-LABEL: cir.func{{.*}} @f9(){{.*}} {
// CIR:         %[[SLOT:.+]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["agg.tmp0"] {alignment = 4 : i64}
// CIR-NEXT:    %[[RET:.+]] = cir.call @f3() : () -> !u64i
// CIR:         cir.store align(4) %[[RET]], %{{.+}} : !u64i, !cir.ptr<!u64i>
// CIR:         %[[ARG:.+]] = cir.load align(4) %{{.+}} : !cir.ptr<!u64i>, !u64i
// CIR-NEXT:    cir.call @f1(%[[ARG]]) : (!u64i) -> ()

// LLVM-LABEL: define{{.*}} void @f9(){{.*}} {
// LLVM:         %[[SLOT:.+]] = alloca %struct.S, i64 1, align 4
// LLVM-NEXT:    %[[RET:.+]] = call i64 @f3()
// LLVM-NEXT:    store i64 %[[RET]], ptr %[[SLOT]], align 4
// LLVM:         %[[ARG:.+]] = load i64, ptr %[[SLOT]], align 4
// LLVM-NEXT:    call void @f1(i64 %[[ARG]])

// OGCG-LABEL: define{{.*}} void @f9() #0 {
// OGCG:         %[[SLOT:.+]] = alloca %struct.S, align 4
// OGCG-NEXT:    %[[RET:.+]] = call i64 @f3()
// OGCG-NEXT:    store i64 %[[RET]], ptr %[[SLOT]], align 4
// OGCG-NEXT:    %[[ARG:.+]] = load i64, ptr %[[SLOT]], align 4
// OGCG-NEXT:    call void @f1(i64 %[[ARG]])

__attribute__((pure)) int f10(int);
__attribute__((const)) int f11(int);
int f12(void) {
  return f10(1) + f11(2);
}

// CIR-LABEL: cir.func{{.*}} @f12() -> !s32i{{.*}} {
// CIR:         %[[A:.+]] = cir.const #cir.int<1> : !s32i
// CIR-NEXT:    %{{.+}} = cir.call @f10(%[[A]]) nothrow side_effect(pure) : (!s32i) -> !s32i
// CIR-NEXT:    %[[B:.+]] = cir.const #cir.int<2> : !s32i
// CIR-NEXT:    %{{.+}} = cir.call @f11(%[[B]]) nothrow side_effect(const) : (!s32i) -> !s32i

// LLVM-LABEL: define{{.*}} i32 @f12(){{.*}}
// LLVM:         %{{.+}} = call i32 @f10(i32 noundef 1) #[[ATTR0:.+]]
// LLVM-NEXT:    %{{.+}} = call i32 @f11(i32 noundef 2) #[[ATTR1:.+]]

// OGCG-LABEL: define{{.*}} i32 @f12()
// OGCG:         %{{.+}} = call i32 @f10(i32 noundef 1) #[[ATTR0:.+]]
// OGCG-NEXT:    %{{.+}} = call i32 @f11(i32 noundef 2) #[[ATTR1:.+]]

// Test const/pure on function definitions (not just call sites).
__attribute__((const)) int const_def(int x) { return x * x; }
__attribute__((pure)) int pure_def(int x) { return x * x; }

// CIR: cir.func{{.*}} @const_def
// CIR: cir.func{{.*}} @pure_def

// Function definitions capture attribute group numbers.
// LLVM: define{{.*}} i32 @const_def(i32 noundef %{{.*}}) #[[CONST_DEF_ATTR:[0-9]+]]
// LLVM: define{{.*}} i32 @pure_def(i32 noundef %{{.*}}) #[[PURE_DEF_ATTR:[0-9]+]]
// OGCG: define{{.*}} i32 @const_def(i32 noundef %{{.*}}) #[[OGCG_CONST_DEF_ATTR:[0-9]+]]
// OGCG: define{{.*}} i32 @pure_def(i32 noundef %{{.*}}) #[[OGCG_PURE_DEF_ATTR:[0-9]+]]

// All attribute checks in output order (attributes #N are emitted numerically).
// Function definition attrs come before call-site attrs.
// LLVM: attributes #[[CONST_DEF_ATTR]] = {{{.*}}willreturn memory(none){{.*}}}
// LLVM: attributes #[[PURE_DEF_ATTR]] = {{{.*}}willreturn memory(read){{.*}}}
// LLVM: attributes #[[ATTR0]] = { nounwind willreturn memory(read) }
// LLVM: attributes #[[ATTR1]] = { nounwind willreturn memory(none) }

// OGCG: attributes #[[OGCG_CONST_DEF_ATTR]] = {{{.*}}willreturn memory(none){{.*}}}
// OGCG: attributes #[[OGCG_PURE_DEF_ATTR]] = {{{.*}}willreturn memory(read){{.*}}}
// OGCG: attributes #[[ATTR0]] = { nounwind willreturn memory(read) }
// OGCG: attributes #[[ATTR1]] = { nounwind willreturn memory(none) }
