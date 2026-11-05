// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o -  | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -x c++ -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o -  | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s -check-prefix=LLVM
// RUN: %clang_cc1 -x c++ -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s -check-prefix=LLVM

typedef struct {
    int a;
    int b;
} Data;

typedef int (*fun_t)(Data* d);

struct A;
typedef int (*fun_typ)(struct A*);

typedef struct A {
  fun_typ fun;
} A;

// CIR: !rec_A = !cir.record<struct "A" {!cir.ptr<!cir.func<(!cir.ptr<!cir.record<struct "A">>) -> !s32i>>}
A a = {(fun_typ)0};

int extract_a(Data* d) {
    return d->a;
}

// CIR: cir.func {{.*}} {{@.*foo.*}}(%arg0: !cir.ptr<!rec_Data>
// CIR:   [[D_ADDR:%.*]] = cir.alloca !cir.ptr<!rec_Data>, !cir.ptr<!cir.ptr<!rec_Data>>, ["d", init]
// CIR:   [[F_ADDR:%.*]] = cir.alloca !cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>, !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>>, ["f", init]
// CIR:   cir.store{{.*}} %arg0, [[D_ADDR]] : !cir.ptr<!rec_Data>, !cir.ptr<!cir.ptr<!rec_Data>>
// CIR:   [[NULL_FN:%.*]] = cir.const #cir.ptr<null> : !cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>
// CIR:   cir.store{{.*}} [[NULL_FN]], [[F_ADDR]] : !cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>, !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>>
// CIR:   [[EXTRACT_A:%.*]] = cir.get_global {{@.*extract_a.*}} : !cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>
// CIR:   cir.store{{.*}} [[EXTRACT_A]], [[F_ADDR]] : !cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>, !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>>
// CIR:   [[FUN:%.*]] = cir.load{{.*}} [[F_ADDR]] : !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>>, !cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>
// CIR:   [[DATA:%.*]] = cir.load{{.*}} [[D_ADDR]] : !cir.ptr<!cir.ptr<!rec_Data>>, !cir.ptr<!rec_Data>
// CIR:   [[RESULT:%.*]] = cir.call [[FUN]]([[DATA]]) {cir.call_arg_attrs = [{cir.noundef}]{{.*}} : (!cir.ptr<!cir.func<(!cir.ptr<!rec_Data>) -> !s32i>>, !cir.ptr<!rec_Data>) -> !s32i
// CIR:   cir.return [[RESULT]] : !s32i

// LLVM: define dso_local {{(noundef )?}}i32 {{@.*foo.*}}(ptr noundef %0)
// LLVM:   [[D_ADDR:%.*]] = alloca ptr, i64 1
// LLVM:   [[F_ADDR:%.*]] = alloca ptr, i64 1
// LLVM:   store ptr %0, ptr [[D_ADDR]]
// LLVM:   store ptr null, ptr [[F_ADDR]]
// LLVM:   store ptr {{@.*extract_a.*}}, ptr [[F_ADDR]]
// LLVM:   [[FUN:%.*]] = load ptr, ptr [[F_ADDR]]
// LLVM:   [[DATA:%.*]] = load ptr, ptr [[D_ADDR]]
// LLVM:   [[RESULT:%.*]] = call {{(noundef )?}}i32 [[FUN]](ptr noundef [[DATA]])
// LLVM:   ret i32 [[RESULT]]
int foo(Data* d) {
    fun_t f = 0;
    f = extract_a;
    return f(d);
}

// CIR:  cir.func {{.*}} {{@.*bar.*}}()
// CIR:    [[RET:%.*]] = cir.call {{@.*test.*}}() : () -> !cir.ptr<!cir.func<()>>
// CIR:    cir.call [[RET]]() : (!cir.ptr<!cir.func<()>>) -> ()
// CIR:    cir.return
// CIR:  cir.func private {{@.*test.*}}() -> {{.*}}!cir.ptr<!cir.func<()>>

// LLVM: define dso_local void {{@.*bar.*}}()
// LLVM:   [[RET:%.*]] = call {{(noundef )?}}ptr {{@.*test.*}}()
// LLVM:   call void [[RET]]()
// LLVM:   ret void
// LLVM: declare {{(noundef )?}}ptr {{@.*test.*}}()
void (*test(void))(void);
void bar(void) {
  test()();
}
