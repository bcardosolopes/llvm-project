// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc \
// RUN:   -fexceptions -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=LEGACY-DIRECT
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc \
// RUN:   -fexceptions -fobjc-exceptions -o - %s | FileCheck %s --check-prefix=LEGACY-OGCG
// RUN: %clang_cc1 -triple x86_64-apple-darwin11 -emit-llvm -fobjc-arc \
// RUN:   -fobjc-runtime=macosx-10.7 -fexceptions -fobjc-exceptions \
// RUN:   -fclangir -o - %s | FileCheck %s --check-prefix=NATIVE-DIRECT
// RUN: %clang_cc1 -triple x86_64-apple-darwin11 -emit-llvm -fobjc-arc \
// RUN:   -fobjc-runtime=macosx-10.7 -fexceptions -fobjc-exceptions \
// RUN:   -o - %s | FileCheck %s --check-prefix=NATIVE-OGCG

void sink(id);

void test_legacy(void) {
  @autoreleasepool {
    sink((id)0);
  }
}

// LEGACY-DIRECT-LABEL: define void @test_legacy()
// LEGACY-DIRECT: load ptr, ptr @{{"?(OBJC_CLASSLIST_REFERENCES_\$_NSAutoreleasePool|OBJC_CLASSLIST_REFERENCES_\$_)"?}}, align 8
// LEGACY-DIRECT: call ptr @objc_msgSend(ptr noundef %{{.+}}, ptr noundef %{{.+}})
// LEGACY-DIRECT: call ptr @objc_msgSend(ptr noundef %{{.+}}, ptr noundef %{{.+}})
// LEGACY-DIRECT: call void @sink(ptr noundef null)
// LEGACY-DIRECT: call void @objc_msgSend(ptr noundef %{{.+}}, ptr noundef %{{.+}})

// LEGACY-OGCG-LABEL: define void @test_legacy()
// LEGACY-OGCG: load ptr, ptr @{{"?(OBJC_CLASSLIST_REFERENCES_\$_NSAutoreleasePool|OBJC_CLASSLIST_REFERENCES_\$_)"?}}, align 8
// LEGACY-OGCG: call ptr @objc_msgSend(ptr noundef %{{.+}}, ptr noundef %{{.+}})
// LEGACY-OGCG: call ptr @objc_msgSend(ptr noundef %{{.+}}, ptr noundef %{{.+}})
// LEGACY-OGCG: call void @sink(ptr noundef null)
// LEGACY-OGCG: call void @objc_msgSend(ptr noundef %{{.+}}, ptr noundef %{{.+}})

void test_native(void) {
  @autoreleasepool {
    sink((id)0);
  }
}

// NATIVE-DIRECT-LABEL: define void @test_native()
// NATIVE-DIRECT: call ptr @llvm.objc.autoreleasePoolPush()
// NATIVE-DIRECT: call void @sink(ptr noundef null)
// NATIVE-DIRECT: call void @llvm.objc.autoreleasePoolPop(ptr %{{.+}})

// NATIVE-OGCG-LABEL: define void @test_native()
// NATIVE-OGCG: call ptr @llvm.objc.autoreleasePoolPush()
// NATIVE-OGCG: call void @sink(ptr noundef null)
// NATIVE-OGCG: call void @llvm.objc.autoreleasePoolPop(ptr %{{.+}})
