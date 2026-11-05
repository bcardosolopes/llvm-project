// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fms-extensions -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fms-extensions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

void _ReadWriteBarrier(void);
void *_AddressOfReturnAddress(void);
__int64 __mulh(__int64, __int64);
unsigned __int64 __umulh(unsigned __int64, unsigned __int64);
void __prefetch(const void *);
unsigned int __hlt(unsigned int, ...);
unsigned int __sys(int, __int64);
unsigned __int64 __getReg(int);
__int64 _ReadStatusReg(int);
void _WriteStatusReg(int, __int64);

void test_readwritebarrier(void) {
  _ReadWriteBarrier();
}

// CIR: define {{.*}}void @test_readwritebarrier()
// CIR:   fence syncscope("singlethread") seq_cst

// OGCG: define {{.*}}void @test_readwritebarrier()
// OGCG:   fence syncscope("singlethread") seq_cst

void *test_addressofreturnaddress(void) {
  return _AddressOfReturnAddress();
}

// CIR: define {{.*}}ptr @test_addressofreturnaddress()
// CIR:   call ptr @llvm.addressofreturnaddress.p0()

// OGCG: define {{.*}}ptr @test_addressofreturnaddress()
// OGCG:   call ptr @llvm.addressofreturnaddress.p0()

__int64 test_mulh(__int64 a, __int64 b) {
  return __mulh(a, b);
}

// CIR: define {{.*}}i64 @test_mulh(i64 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %{{.*}} = sext i64 %{{.*}} to i128
// CIR:   %{{.*}} = sext i64 %{{.*}} to i128
// CIR:   %{{.*}} = mul {{.*}}i128 %{{.*}}, %{{.*}}
// CIR:   %{{.*}} = ashr i128 %{{.*}}, 64
// CIR:   %{{.*}} = trunc i128 %{{.*}} to i64

// OGCG: define {{.*}}i64 @test_mulh(i64 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = sext i64 %{{.*}} to i128
// OGCG:   %{{.*}} = sext i64 %{{.*}} to i128
// OGCG:   %{{.*}} = mul {{.*}}i128 %{{.*}}, %{{.*}}
// OGCG:   %{{.*}} = ashr i128 %{{.*}}, 64
// OGCG:   %{{.*}} = trunc i128 %{{.*}} to i64

unsigned __int64 test_umulh(unsigned __int64 a, unsigned __int64 b) {
  return __umulh(a, b);
}

// CIR: define {{.*}}i64 @test_umulh(i64 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %{{.*}} = zext i64 %{{.*}} to i128
// CIR:   %{{.*}} = zext i64 %{{.*}} to i128
// CIR:   %{{.*}} = mul {{.*}}i128 %{{.*}}, %{{.*}}
// CIR:   %{{.*}} = lshr i128 %{{.*}}, 64
// CIR:   %{{.*}} = trunc i128 %{{.*}} to i64

// OGCG: define {{.*}}i64 @test_umulh(i64 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = zext i64 %{{.*}} to i128
// OGCG:   %{{.*}} = zext i64 %{{.*}} to i128
// OGCG:   %{{.*}} = mul {{.*}}i128 %{{.*}}, %{{.*}}
// OGCG:   %{{.*}} = lshr i128 %{{.*}}, 64
// OGCG:   %{{.*}} = trunc i128 %{{.*}} to i64

void test_prefetch(void *p) {
  __prefetch(p);
}

// CIR: define {{.*}}void @test_prefetch(ptr {{.*}}%{{.*}})
// CIR:   call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 3, i32 1)

// OGCG: define {{.*}}void @test_prefetch(ptr {{.*}}%{{.*}})
// OGCG:   call void @llvm.prefetch.p0(ptr %{{.*}}, i32 0, i32 3, i32 1)

unsigned int test_hlt(void) {
  return __hlt(42);
}

// CIR: define {{.*}}i32 @test_hlt()
// CIR:   call void @llvm.aarch64.hlt(i32 42)
// CIR:   ret i32 0

// OGCG: define {{.*}}i32 @test_hlt()
// OGCG:   call void @llvm.aarch64.hlt(i32 42)
// OGCG:   ret i32 0

unsigned int test_sys(__int64 val) {
  return __sys(0x5A10, val);
}

// CIR: define {{.*}}i32 @test_sys(i64 {{.*}}%{{.*}})
// CIR:   call void @llvm.write_register.i64(metadata !{{.*}}, i64 %{{.*}})
// CIR:   ret i32 0

// OGCG: define {{.*}}i32 @test_sys(i64 {{.*}}%{{.*}})
// OGCG:   call void @llvm.write_register.i64(metadata !{{.*}}, i64 %{{.*}})
// OGCG:   ret i32 0

unsigned __int64 test_getReg_x0(void) {
  return __getReg(0);
}

// CIR: define {{.*}}i64 @test_getReg_x0()
// CIR:   call i64 @llvm.read_register.i64(metadata !{{.*}})

// OGCG: define {{.*}}i64 @test_getReg_x0()
// OGCG:   call i64 @llvm.read_register.i64(metadata !{{.*}})

unsigned __int64 test_getReg_sp(void) {
  return __getReg(31);
}

// CIR: define {{.*}}i64 @test_getReg_sp()
// CIR:   call i64 @llvm.read_register.i64(metadata !{{.*}})

// OGCG: define {{.*}}i64 @test_getReg_sp()
// OGCG:   call i64 @llvm.read_register.i64(metadata !{{.*}})

__int64 test_read_status_reg(void) {
  return _ReadStatusReg(0x5A10);
}

// CIR: define {{.*}}i64 @test_read_status_reg()
// CIR:   %{{.*}} = call i64 @llvm.read_register.i64(metadata ![[SYSREG:.*]])

// OGCG: define {{.*}}i64 @test_read_status_reg()
// OGCG:   %{{.*}} = call i64 @llvm.read_register.i64(metadata ![[SYSREG:.*]])

void test_write_status_reg(__int64 val) {
  _WriteStatusReg(0x5A10, val);
}

// CIR: define {{.*}}void @test_write_status_reg(i64 {{.*}}%{{.*}})
// CIR:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

// OGCG: define {{.*}}void @test_write_status_reg(i64 {{.*}}%{{.*}})
// OGCG:   call void @llvm.write_register.i64(metadata ![[SYSREG]], i64 %{{.*}})

int _InterlockedAdd(volatile int *, int);
long long _InterlockedAdd64(volatile long long *, long long);
int _InterlockedAdd_acq(volatile int *, int);
int _InterlockedAdd_rel(volatile int *, int);
int _InterlockedAdd_nf(volatile int *, int);

int test_InterlockedAdd(volatile int *Addend, int Value) {
  return _InterlockedAdd(Addend, Value);
}

// CIR: define {{.*}}i32 @test_InterlockedAdd(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// CIR:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} seq_cst
// CIR:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

// OGCG: define {{.*}}i32 @test_InterlockedAdd(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} seq_cst
// OGCG:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

long long test_InterlockedAdd64(volatile long long *Addend, long long Value) {
  return _InterlockedAdd64(Addend, Value);
}

// CIR: define {{.*}}i64 @test_InterlockedAdd64(ptr {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %{{.*}} = atomicrmw add ptr %{{.*}}, i64 %{{.*}} seq_cst
// CIR:   %{{.*}} = add i64 %{{.*}}, %{{.*}}

// OGCG: define {{.*}}i64 @test_InterlockedAdd64(ptr {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = atomicrmw add ptr %{{.*}}, i64 %{{.*}} seq_cst
// OGCG:   %{{.*}} = add i64 %{{.*}}, %{{.*}}

int test_InterlockedAdd_acq(volatile int *Addend, int Value) {
  return _InterlockedAdd_acq(Addend, Value);
}

// CIR: define {{.*}}i32 @test_InterlockedAdd_acq(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// CIR:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} acquire
// CIR:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

// OGCG: define {{.*}}i32 @test_InterlockedAdd_acq(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} acquire
// OGCG:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

int test_InterlockedAdd_rel(volatile int *Addend, int Value) {
  return _InterlockedAdd_rel(Addend, Value);
}

// CIR: define {{.*}}i32 @test_InterlockedAdd_rel(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// CIR:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} release
// CIR:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

// OGCG: define {{.*}}i32 @test_InterlockedAdd_rel(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} release
// OGCG:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

int test_InterlockedAdd_nf(volatile int *Addend, int Value) {
  return _InterlockedAdd_nf(Addend, Value);
}

// CIR: define {{.*}}i32 @test_InterlockedAdd_nf(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// CIR:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} monotonic
// CIR:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

// OGCG: define {{.*}}i32 @test_InterlockedAdd_nf(ptr {{.*}}%{{.*}}, i32 {{.*}}%{{.*}})
// OGCG:   %{{.*}} = atomicrmw add ptr %{{.*}}, i32 %{{.*}} monotonic
// OGCG:   %{{.*}} = add i32 %{{.*}}, %{{.*}}

// CIR: ![[SYSREG]] = !{!"3:3:4:2:0"}
// OGCG: ![[SYSREG]] = !{!"3:3:4:2:0"}
