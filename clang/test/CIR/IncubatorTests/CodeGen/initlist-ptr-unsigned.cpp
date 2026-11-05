// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

namespace std {
template <class b> class initializer_list {
  const b *c;
  unsigned long len;
};
template <class b>
void f(initializer_list<b>) {;}
void test() {
  f({7});
}
} // namespace std

// CIR: [[INITLIST_TYPE:!.*]] = !cir.record<class "std::initializer_list<int>" {!cir.ptr<!s32i>, !u64i}>

// CIR: cir.func {{.*}} @_ZSt4testv()
// CIR: cir.scope {
// CIR: [[LIST_PTR:%.*]] = cir.alloca [[INITLIST_TYPE]], !cir.ptr<[[INITLIST_TYPE]]>,
// CIR: [[ARRAY:%.*]] = cir.alloca !cir.array<!s32i x 1>, !cir.ptr<!cir.array<!s32i x 1>>,
// CIR: [[FIRST_ELEM:%.*]] = cir.cast array_to_ptrdecay [[ARRAY]] : !cir.ptr<!cir.array<!s32i x 1>> -> !cir.ptr<!s32i>
// CIR: [[SEVEN:%.*]] = cir.const #cir.int<7> : !s32i
// CIR: cir.store{{.*}} [[SEVEN]], [[FIRST_ELEM]] : !s32i, !cir.ptr<!s32i>
// CIR: [[FLD_C:%.*]] = cir.get_member [[LIST_PTR]][0] {name = "c"} : !cir.ptr<[[INITLIST_TYPE]]> -> !cir.ptr<!cir.ptr<!s32i>>
// CIR: [[ARRAY_PTR:%.*]] = cir.cast bitcast [[FLD_C]] : !cir.ptr<!cir.ptr<!s32i>> -> !cir.ptr<!cir.ptr<!cir.array<!s32i x 1>>>
// CIR: cir.store{{.*}} [[ARRAY]], [[ARRAY_PTR]] : !cir.ptr<!cir.array<!s32i x 1>>, !cir.ptr<!cir.ptr<!cir.array<!s32i x 1>>>
// CIR: [[LENGTH_ONE:%.*]] = cir.const #cir.int<1>
// CIR: [[FLD_LEN:%.*]] = cir.get_member [[LIST_PTR]][1] {name = "len"} : !cir.ptr<[[INITLIST_TYPE]]> -> !cir.ptr<!u64i>
// CIR: cir.store{{.*}} [[LENGTH_ONE]], [[FLD_LEN]] : !u64i, !cir.ptr<!u64i>
// CIR: [[BITCAST:%.*]] = cir.cast bitcast [[LIST_PTR]] : !cir.ptr<[[INITLIST_TYPE]]> -> !cir.ptr<!cir.array<!u64i x 2>>
// CIR: [[FLAT_ARG:%.*]] = cir.load align(8) [[BITCAST]] : !cir.ptr<!cir.array<!u64i x 2>>, !cir.array<!u64i x 2>
// CIR: cir.call @_ZSt1fIiEvSt16initializer_listIT_E([[FLAT_ARG]]) : (!cir.array<!u64i x 2>) -> ()
// CIR: }
// CIR: cir.return
// CIR: }
// CIR: cir.func {{.*}} @_ZSt1fIiEvSt16initializer_listIT_E(%arg0: !cir.array<!u64i x 2>
// CIR: [[REG0:%.*]] = cir.alloca [[INITLIST_TYPE]], !cir.ptr<[[INITLIST_TYPE]]>,
// CIR: [[REG1:%.*]] = cir.cast bitcast [[REG0]] : !cir.ptr<[[INITLIST_TYPE]]> -> !cir.ptr<!cir.array<!u64i x 2>>
// CIR: cir.store{{.*}} %arg0, [[REG1]] : !cir.array<!u64i x 2>, !cir.ptr<!cir.array<!u64i x 2>>
// CIR: cir.return

// LLVM: %"class.std::initializer_list<int>" = type { ptr, i64 }

// LLVM: define dso_local void @_ZSt4testv()
// LLVM:  [[INIT_STRUCT:%.*]] = alloca %"class.std::initializer_list<int>", i64 1, align 8
// LLVM:  [[ELEM_ARRAY:%.*]] = alloca [1 x i32], i64 1, align 4
// LLVM:  [[PTR_FIRST_ELEM:%.*]] = getelementptr inbounds nuw i32, ptr [[ELEM_ARRAY]], i32 0
// LLVM:  store i32 7, ptr [[PTR_FIRST_ELEM]], align 4
// LLVM:  [[ELEM_ARRAY_PTR:%.*]] = getelementptr inbounds nuw %"class.std::initializer_list<int>", ptr [[INIT_STRUCT]], i32 0, i32 0
// LLVM:  store ptr [[ELEM_ARRAY]], ptr [[ELEM_ARRAY_PTR]], align 8
// LLVM:  [[INIT_LEN_FLD:%.*]] = getelementptr inbounds nuw %"class.std::initializer_list<int>", ptr [[INIT_STRUCT]], i32 0, i32 1
// LLVM:  store i64 1, ptr [[INIT_LEN_FLD]], align 8
// LLVM:  [[ARG2PASS:%.*]] = load [2 x i64], ptr [[INIT_STRUCT]], align 8
// LLVM:  call void @_ZSt1fIiEvSt16initializer_listIT_E([2 x i64] [[ARG2PASS]])
// LLVM:  ret void

// LLVM: define linkonce_odr void @_ZSt1fIiEvSt16initializer_listIT_E([2 x i64] [[ARG:%.*]])
// LLVM:  [[LOCAL:%.*]] = alloca %"class.std::initializer_list<int>", i64 1, align 8
// LLVM:  store [2 x i64] [[ARG]], ptr [[LOCAL]], align 8
