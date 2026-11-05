// RUN: %clang_cc1 -triple i686-apple-ios10.3 -fobjc-runtime=ios-6.0 -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK32
// RUN: %clang_cc1 -triple i686--windows-msvc -fobjc-runtime=ios-6.0 -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK32
// RUN: %clang_cc1 -triple x86_64-apple-ios10.3 -fobjc-runtime=ios-6.0 -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK64
// RUN: %clang_cc1 -triple x86_64--windows-msvc -fobjc-runtime=ios-6.0 -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK-GNUSTEP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -fobjc-dispatch-method=non-legacy -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK-GNUSTEP-NONLEGACY
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=gcc -emit-llvm -fclangir -o - %s | FileCheck %s -check-prefix CHECK-GCC

void f(id a) {
  for (id i in a)
    (void)i;
}

// CHECK32-LABEL: define {{.*}}void @f(
// CHECK32: call i32 @objc_msgSend
// CHECK32: call i32 @objc_msgSend
// CHECK32: call void @objc_enumerationMutation(ptr

// CHECK64-LABEL: define {{.*}}void @f(
// CHECK64: call i64 @objc_msgSend
// CHECK64: call i64 @objc_msgSend
// CHECK64: call void @objc_enumerationMutation(ptr

// CHECK-GNUSTEP-LABEL: define {{.*}}void @f(
// CHECK-GNUSTEP: call ptr @objc_msg_lookup_sender(ptr %{{.*}}, ptr @{{.*}}, ptr null), !GNUObjCMessageSend ![[GNUSTEP_MSG:[0-9]+]]
// CHECK-GNUSTEP: call i64 %{{.*}}(ptr noundef %{{.*}}, ptr noundef @{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 16), !GNUObjCMessageSend ![[GNUSTEP_MSG]]
// CHECK-GNUSTEP: call ptr @objc_msg_lookup_sender(ptr %{{.*}}, ptr @{{.*}}, ptr null), !GNUObjCMessageSend ![[GNUSTEP_MSG]]
// CHECK-GNUSTEP: call i64 %{{.*}}(ptr noundef %{{.*}}, ptr noundef @{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 16), !GNUObjCMessageSend ![[GNUSTEP_MSG]]
// CHECK-GNUSTEP: call void @objc_enumerationMutation(ptr
// CHECK-GNUSTEP: ![[GNUSTEP_MSG]] = !{!"countByEnumeratingWithState:objects:count:", !"", i1 false}

// CHECK-GNUSTEP-NONLEGACY-LABEL: define {{.*}}void @f(
// CHECK-GNUSTEP-NONLEGACY-NOT: @objc_msg_lookup_sender
// CHECK-GNUSTEP-NONLEGACY: call i64 @objc_msgSend(ptr noundef %{{.*}}, ptr noundef @{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 16), !GNUObjCMessageSend ![[GNUSTEP_NL_MSG:[0-9]+]]
// CHECK-GNUSTEP-NONLEGACY: call i64 @objc_msgSend(ptr noundef %{{.*}}, ptr noundef @{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 16), !GNUObjCMessageSend ![[GNUSTEP_NL_MSG]]
// CHECK-GNUSTEP-NONLEGACY: call void @objc_enumerationMutation(ptr
// CHECK-GNUSTEP-NONLEGACY: ![[GNUSTEP_NL_MSG]] = !{!"countByEnumeratingWithState:objects:count:", !"", i1 false}

// CHECK-GCC-LABEL: define {{.*}}void @f(
// CHECK-GCC: call ptr @objc_msg_lookup(ptr %{{.*}}, ptr @.objc_selector_list), !GNUObjCMessageSend ![[GCC_MSG:[0-9]+]]
// CHECK-GCC: call i64 %{{.*}}(ptr noundef %{{.*}}, ptr noundef @.objc_selector_list, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 16), !GNUObjCMessageSend ![[GCC_MSG]]
// CHECK-GCC: call ptr @objc_msg_lookup(ptr %{{.*}}, ptr @.objc_selector_list), !GNUObjCMessageSend ![[GCC_MSG]]
// CHECK-GCC: call i64 %{{.*}}(ptr noundef %{{.*}}, ptr noundef @.objc_selector_list, ptr noundef %{{.*}}, ptr noundef %{{.*}}, i64 noundef 16), !GNUObjCMessageSend ![[GCC_MSG]]
// CHECK-GCC: call void @objc_enumerationMutation(ptr
// CHECK-GCC: ![[GCC_MSG]] = !{!"countByEnumeratingWithState:objects:count:", !"", i1 false}
