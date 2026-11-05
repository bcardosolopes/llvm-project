// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm -O0 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

void test_delete_array(int *ptr) {
  delete[] ptr;
}

// LLVM: [[PTR:%[0-9]+]] = load ptr, ptr %{{[0-9]+}}, align 8
// LLVM: [[ISNULL:%[0-9]+]] = icmp ne ptr [[PTR]], null
// LLVM: br i1 [[ISNULL]], label %[[NOTNULL:[0-9]+]], label %{{[0-9]+}}
// LLVM: [[NOTNULL]]:
// LLVM-NEXT: call void @_ZdaPv(ptr noundef [[PTR]])


int *newmem();
struct cls {
  ~cls();
};
cls::~cls() { delete[] newmem(); }

// LLVM: [[NEWMEM:%[0-9]+]] = call noundef ptr @_Z6newmemv()
// LLVM: [[ISNULL2:%[0-9]+]] = icmp ne ptr [[NEWMEM]], null
// LLVM: br i1 [[ISNULL2]], label %[[NOTNULL2:[0-9]+]], label %{{[0-9]+}}
// LLVM: [[NOTNULL2]]:
// LLVM-NEXT: call void @_ZdaPv(ptr noundef [[NEWMEM]])
