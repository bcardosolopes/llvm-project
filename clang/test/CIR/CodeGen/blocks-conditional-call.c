// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

void (^b)() = ^{};

int main(void) {
  (b ?: ^{})();
}

// CIR-LABEL: cir.func no_inline optnone @main()
// CIR: [[GB:%.*]] = cir.get_global @b : !cir.ptr<!cir.ptr<!void>>
// CIR: [[B:%.*]] = cir.load align(8) [[GB]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CIR: [[COND:%.*]] = cir.cast ptr_to_bool [[B]] : !cir.ptr<!void> -> !cir.bool
// CIR: [[CHOICE:%.*]] = cir.ternary([[COND]], true {
// CIR:   cir.yield [[B]] : !cir.ptr<!void>
// CIR: }, false {
// CIR:   [[LIT:%.*]] = cir.const #cir.global_view<@__main_block_invoke.literal> : !cir.ptr<!rec_anon_struct
// CIR:   [[CAST:%.*]] = cir.cast bitcast [[LIT]] : !cir.ptr<!rec_anon_struct
// CIR-SAME: -> !cir.ptr<!void>
// CIR:   cir.yield [[CAST]] : !cir.ptr<!void>
// CIR: }) : (!cir.bool) -> !cir.ptr<!void>
// CIR: [[HDR:%.*]] = cir.cast bitcast [[CHOICE]] : !cir.ptr<!void> -> !cir.ptr<!rec_anon_struct
// CIR: [[INVOKE_SLOT:%.*]] = cir.get_member [[HDR]][3] {name = ""}
// CIR: [[RAW:%.*]] = cir.load align(8) [[INVOKE_SLOT]]
// CIR: [[FN:%.*]] = cir.cast bitcast [[RAW]] : !cir.ptr<!void> -> !cir.ptr<!cir.func<(!cir.ptr<!void>)>>
// CIR: cir.call [[FN]]([[CHOICE]]) {cir.call_arg_attrs = [{cir.noundef}]}

// OGCG-LABEL: define{{.*}} i32 @main(
// OGCG: [[ZERO:%.*]] = load ptr, ptr @b
// OGCG-NEXT: [[TB:%.*]] = icmp ne ptr [[ZERO]], null
// OGCG-NEXT: br i1 [[TB]], label %{{.*}}, label %{{.*}}
// OGCG: br label %{{.*}}
// OGCG: br label %{{.*}}
// OGCG: [[PHI:%.*]] = phi ptr [ [[ZERO]], %{{.*}} ], [ @{{.*}}, %{{.*}} ]
// OGCG: getelementptr inbounds nuw %struct.__block_literal_generic, ptr [[PHI]], i32 0, i32 3
// OGCG: load ptr, ptr %{{.*}}, align 8
// OGCG: call void %{{.*}}(ptr noundef [[PHI]])
