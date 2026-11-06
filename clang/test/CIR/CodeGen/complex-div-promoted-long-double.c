// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -complex-range=promoted -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=X87-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -complex-range=promoted -emit-llvm %s -o - | FileCheck %s --check-prefix=X87

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -complex-range=promoted -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefixes=FP128-CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -complex-range=promoted -emit-llvm %s -o - | FileCheck %s --check-prefix=FP128

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-64 -complex-range=promoted -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=DBL
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-64 -complex-range=promoted -emit-llvm %s -o - | FileCheck %s --check-prefix=DBL

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-128 -complex-range=promoted -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=FP128-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mlong-double-128 -complex-range=promoted -emit-llvm %s -o - | FileCheck %s --check-prefix=FP128

_Complex double div(_Complex double a, _Complex double b) { return a / b; }

// X87: [[AR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87: [[AI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87: [[BR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87: [[BI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87-DAG: [[ARBR:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AR]], [[BR]]
// X87-DAG: [[AIBI:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AI]], [[BI]]
// X87-DAG: [[BRBR:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[BR]], [[BR]]
// X87-DAG: [[BIBI:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[BI]], [[BI]]
// X87-DAG: [[NUMR:%[a-zA-Z0-9._]+]] = fadd x86_fp80 [[ARBR]], [[AIBI]]
// X87-DAG: [[DEN:%[a-zA-Z0-9._]+]] = fadd x86_fp80 [[BRBR]], [[BIBI]]
// X87-DAG: [[AIBR:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AI]], [[BR]]
// X87-DAG: [[ARBI:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AR]], [[BI]]
// X87-DAG: [[NUMI:%[a-zA-Z0-9._]+]] = fsub x86_fp80 [[AIBR]], [[ARBI]]
// X87-DAG: [[RES_R:%[a-zA-Z0-9._]+]] = fdiv x86_fp80 [[NUMR]], [[DEN]]
// X87-DAG: [[RES_I:%[a-zA-Z0-9._]+]] = fdiv x86_fp80 [[NUMI]], [[DEN]]
// X87: fptrunc x86_fp80 [[RES_R]] to double
// X87: fptrunc x86_fp80 [[RES_I]] to double
// X87-CIR: [[AR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87-CIR: [[AI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87-CIR: [[BR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87-CIR: [[BI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to x86_fp80
// X87-CIR-DAG: [[ARBR:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AR]], [[BR]]
// X87-CIR-DAG: [[AIBI:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AI]], [[BI]]
// X87-CIR-DAG: [[BRBR:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[BR]], [[BR]]
// X87-CIR-DAG: [[BIBI:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[BI]], [[BI]]
// X87-CIR-DAG: [[NUMR:%[a-zA-Z0-9._]+]] = fadd x86_fp80 [[ARBR]], [[AIBI]]
// X87-CIR-DAG: [[DEN:%[a-zA-Z0-9._]+]] = fadd x86_fp80 [[BRBR]], [[BIBI]]
// X87-CIR-DAG: [[AIBR:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AI]], [[BR]]
// X87-CIR-DAG: [[ARBI:%[a-zA-Z0-9._]+]] = fmul x86_fp80 [[AR]], [[BI]]
// X87-CIR-DAG: [[NUMI:%[a-zA-Z0-9._]+]] = fsub x86_fp80 [[AIBR]], [[ARBI]]
// X87-CIR-DAG: [[RES_R:%[a-zA-Z0-9._]+]] = fdiv x86_fp80 [[NUMR]], [[DEN]]
// X87-CIR-DAG: [[RES_I:%[a-zA-Z0-9._]+]] = fdiv x86_fp80 [[NUMI]], [[DEN]]
// X87-CIR: [[TMP:%[a-zA-Z0-9._]+]] = insertvalue { x86_fp80, x86_fp80 } {{.*}}, x86_fp80 [[RES_R]], 0
// X87-CIR: [[AGG:%[a-zA-Z0-9._]+]] = insertvalue { x86_fp80, x86_fp80 } [[TMP]], x86_fp80 [[RES_I]], 1
// X87-CIR: [[EX_R:%[a-zA-Z0-9._]+]] = extractvalue { x86_fp80, x86_fp80 } [[AGG]], 0
// X87-CIR: [[EX_I:%[a-zA-Z0-9._]+]] = extractvalue { x86_fp80, x86_fp80 } [[AGG]], 1
// X87-CIR: fptrunc x86_fp80 [[EX_R]] to double
// X87-CIR: fptrunc x86_fp80 [[EX_I]] to double
// FP128: [[AR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128: [[AI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128: [[BR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128: [[BI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128-DAG: [[ARBR:%[a-zA-Z0-9._]+]] = fmul fp128 [[AR]], [[BR]]
// FP128-DAG: [[AIBI:%[a-zA-Z0-9._]+]] = fmul fp128 [[AI]], [[BI]]
// FP128-DAG: [[BRBR:%[a-zA-Z0-9._]+]] = fmul fp128 [[BR]], [[BR]]
// FP128-DAG: [[BIBI:%[a-zA-Z0-9._]+]] = fmul fp128 [[BI]], [[BI]]
// FP128-DAG: [[NUMR:%[a-zA-Z0-9._]+]] = fadd fp128 [[ARBR]], [[AIBI]]
// FP128-DAG: [[DEN:%[a-zA-Z0-9._]+]] = fadd fp128 [[BRBR]], [[BIBI]]
// FP128-DAG: [[AIBR:%[a-zA-Z0-9._]+]] = fmul fp128 [[AI]], [[BR]]
// FP128-DAG: [[ARBI:%[a-zA-Z0-9._]+]] = fmul fp128 [[AR]], [[BI]]
// FP128-DAG: [[NUMI:%[a-zA-Z0-9._]+]] = fsub fp128 [[AIBR]], [[ARBI]]
// FP128-DAG: [[RES_R:%[a-zA-Z0-9._]+]] = fdiv fp128 [[NUMR]], [[DEN]]
// FP128-DAG: [[RES_I:%[a-zA-Z0-9._]+]] = fdiv fp128 [[NUMI]], [[DEN]]
// FP128: fptrunc fp128 [[RES_R]] to double
// FP128: fptrunc fp128 [[RES_I]] to double
// FP128-CIR: [[AR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128-CIR: [[AI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128-CIR: [[BR:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128-CIR: [[BI:%[a-zA-Z0-9._]+]] = fpext double %{{.*}} to fp128
// FP128-CIR-DAG: [[ARBR:%[a-zA-Z0-9._]+]] = fmul fp128 [[AR]], [[BR]]
// FP128-CIR-DAG: [[AIBI:%[a-zA-Z0-9._]+]] = fmul fp128 [[AI]], [[BI]]
// FP128-CIR-DAG: [[BRBR:%[a-zA-Z0-9._]+]] = fmul fp128 [[BR]], [[BR]]
// FP128-CIR-DAG: [[BIBI:%[a-zA-Z0-9._]+]] = fmul fp128 [[BI]], [[BI]]
// FP128-CIR-DAG: [[NUMR:%[a-zA-Z0-9._]+]] = fadd fp128 [[ARBR]], [[AIBI]]
// FP128-CIR-DAG: [[DEN:%[a-zA-Z0-9._]+]] = fadd fp128 [[BRBR]], [[BIBI]]
// FP128-CIR-DAG: [[AIBR:%[a-zA-Z0-9._]+]] = fmul fp128 [[AI]], [[BR]]
// FP128-CIR-DAG: [[ARBI:%[a-zA-Z0-9._]+]] = fmul fp128 [[AR]], [[BI]]
// FP128-CIR-DAG: [[NUMI:%[a-zA-Z0-9._]+]] = fsub fp128 [[AIBR]], [[ARBI]]
// FP128-CIR-DAG: [[RES_R:%[a-zA-Z0-9._]+]] = fdiv fp128 [[NUMR]], [[DEN]]
// FP128-CIR-DAG: [[RES_I:%[a-zA-Z0-9._]+]] = fdiv fp128 [[NUMI]], [[DEN]]
// FP128-CIR: [[TMP:%[a-zA-Z0-9._]+]] = insertvalue { fp128, fp128 } {{.*}}, fp128 [[RES_R]], 0
// FP128-CIR: [[AGG:%[a-zA-Z0-9._]+]] = insertvalue { fp128, fp128 } [[TMP]], fp128 [[RES_I]], 1
// FP128-CIR: [[EX_R:%[a-zA-Z0-9._]+]] = extractvalue { fp128, fp128 } [[AGG]], 0
// FP128-CIR: [[EX_I:%[a-zA-Z0-9._]+]] = extractvalue { fp128, fp128 } [[AGG]], 1
// FP128-CIR: fptrunc fp128 [[EX_R]] to double
// FP128-CIR: fptrunc fp128 [[EX_I]] to double
// DBL-NOT: fpext
// DBL-NOT: fptrunc
// DBL: call double @llvm.fabs.f64(double
// DBL: call double @llvm.fabs.f64(double
// DBL: fdiv double
