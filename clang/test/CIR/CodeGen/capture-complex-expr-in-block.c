// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

typedef void (^BLOCK)(void);

int main(void) {
  _Complex double c;
  BLOCK b = ^() {
    _Complex double z;
    z = z + c;
  };
  b();
}

// LLVM-LABEL: define internal void @__main_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM: %[[CAPADDR:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, { double, double } }, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[LOCAL:.*]] = load { double, double }, ptr %{{.*}}, align 8
// LLVM: %[[CAP:.*]] = load { double, double }, ptr %[[CAPADDR]], align 8
// LLVM: %[[LREAL:.*]] = extractvalue { double, double } %[[LOCAL]], 0
// LLVM: %[[LIMAG:.*]] = extractvalue { double, double } %[[LOCAL]], 1
// LLVM: %[[CREAL:.*]] = extractvalue { double, double } %[[CAP]], 0
// LLVM: %[[CIMAG:.*]] = extractvalue { double, double } %[[CAP]], 1
// LLVM: %[[ADDR:.*]] = fadd double %[[LREAL]], %[[CREAL]]
// LLVM: %[[ADDI:.*]] = fadd double %[[LIMAG]], %[[CIMAG]]
// LLVM: store { double, double } %{{.*}}, ptr %{{.*}}, align 8
// LLVM-LABEL: define i32 @main()
// LLVM: %[[CAPSLOT:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, { double, double } }, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[CAPVAL:.*]] = load { double, double }, ptr %{{.*}}, align 8
// LLVM: store { double, double } %[[CAPVAL]], ptr %[[CAPSLOT]], align 8

// OGCG-LABEL: define internal void @__main_block_invoke(ptr noundef %{{.*}})
// OGCG: %[[CAPADDR:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, { double, double } }>, ptr %{{.*}}, i32 0, i32 5
// OGCG: %[[REALP:.*]] = getelementptr inbounds nuw { double, double }, ptr %[[CAPADDR]], i32 0, i32 0
// OGCG: %[[IMAGP:.*]] = getelementptr inbounds nuw { double, double }, ptr %[[CAPADDR]], i32 0, i32 1
