// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gcc -fclangir -o - %s | FileCheck %s --check-prefix=GCC
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-2.0 -fclangir -o - %s | FileCheck %s --check-prefix=GNUSTEP
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -emit-llvm -fobjc-runtime=gnustep-2.0 -fclangir -o - %s | FileCheck %s --check-prefix=GNUSTEP-DECL

// GCC: target triple = "x86_64-unknown-freebsd"
// GCC-NOT: section "__objc_selectors"
// GCC: @.objc_selector_list = internal global [3 x { ptr, ptr }] [{ ptr, ptr } { ptr @.objc_sel_name, ptr @.objc_sel_types }, { ptr, ptr } { ptr @.objc_sel_name, ptr null }, { ptr, ptr } zeroinitializer], align 8
// GNUSTEP-DAG: @.objc_sel_name_foo = linkonce_odr hidden constant [4 x i8] c"foo\00", comdat
// GNUSTEP-DAG: @".objc_sel_types_\0116\010:8" = linkonce_odr hidden constant [8 x i8] c"@16@0:8\00", comdat
// GNUSTEP-DAG: @".objc_selector_foo_\0116\010:8" = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_foo, ptr @".objc_sel_types_\0116\010:8" }, section "__objc_selectors", comdat, align 8
// GNUSTEP-DAG: @.objc_selector_foo_ = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_foo, ptr null }, section "__objc_selectors", comdat, align 8
// GNUSTEP-DAG: @.objc_sel_name_testClass = linkonce_odr hidden constant [10 x i8] c"testClass\00", comdat
// GNUSTEP-DAG: @".objc_selector_testClass_\0116\010:8" = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_testClass, ptr @".objc_sel_types_\0116\010:8" }, section "__objc_selectors", comdat, align 8
// GNUSTEP-DECL: declare ptr @objc_msg_lookup_sender(ptr captures(none), ptr, ptr)

__attribute__((objc_root_class))
@interface Root
@end

@implementation Root
@end

@interface Super : Root
- (id)foo;
+ (id)foo;
@end

@interface Test : Super
- (id)foo;
+ (id)foo;
- (id)testInstance;
+ (id)testClass;
+ (id)testSuper;
@end

@implementation Test

// GCC-LABEL: define{{.*}} ptr @_i_Test__testInstance
// GCC: call ptr @objc_msg_lookup(ptr %{{.*}}, ptr @.objc_selector_list), !GNUObjCMessageSend ![[INST_MD:[0-9]+]]
// GCC: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @.objc_selector_list), !GNUObjCMessageSend ![[INST_MD]]
// GNUSTEP-LABEL: define{{.*}} ptr @_i_Test__testInstance
// GNUSTEP: call ptr @objc_msg_lookup_sender({{.*}}) #[[LOOKUP_ATTR:[0-9]+]], !GNUObjCMessageSend ![[INST_MD:[0-9]+]]
// GNUSTEP: load volatile ptr, ptr %{{.*}}, align 8
// GNUSTEP: call ptr %{{.*}}({{.*}}), !GNUObjCMessageSend ![[INST_MD]]
- (id)testInstance {
  return [self foo];
}

// GCC-LABEL: define{{.*}} ptr @_c_Test__testClass
// GCC: call ptr @objc_msg_lookup(ptr %{{.*}}, ptr @.objc_selector_list), !GNUObjCMessageSend ![[CLASS_MD:[0-9]+]]
// GCC: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef @.objc_selector_list), !GNUObjCMessageSend ![[CLASS_MD]]
// GNUSTEP-LABEL: define{{.*}} ptr @_c_Test__testClass
// GNUSTEP-NOT: @objc_lookup_class
// GNUSTEP: load ptr, ptr @._OBJC_REF_CLASS_Test, align 8
// GNUSTEP: call ptr @objc_msg_lookup_sender({{.*}}) #[[LOOKUP_ATTR]], !GNUObjCMessageSend ![[CLASS_MD:[0-9]+]]
// GNUSTEP: load volatile ptr, ptr %{{.*}}, align 8
// GNUSTEP: call ptr %{{.*}}({{.*}}), !GNUObjCMessageSend ![[CLASS_MD]]
+ (id)testClass {
  return [Test foo];
}

// GCC-LABEL: define{{.*}} ptr @_c_Test__testSuper
// GCC: call ptr @objc_lookup_class({{.*}})
// GCC: load ptr, ptr %{{.*}}, align 8
// GCC: call ptr @objc_msg_lookup_super(ptr %{{.*}}, ptr getelementptr inbounds nuw (i8, ptr @.objc_selector_list, i64 16)) #[[GCC_SUPER_LOOKUP_ATTR:[0-9]+]]
// GCC: call ptr %{{.*}}(ptr noundef %{{.*}}, ptr noundef getelementptr inbounds nuw (i8, ptr @.objc_selector_list, i64 16)), !GNUObjCMessageSend ![[SUPER_MD:[0-9]+]]
// GNUSTEP-LABEL: define{{.*}} ptr @_c_Test__testSuper
// GNUSTEP-NOT: @objc_lookup_class
// GNUSTEP: load ptr, ptr @._OBJC_REF_CLASS_Super, align 8
// GNUSTEP: load ptr, ptr %{{.*}}, align 8
// GNUSTEP: call ptr @objc_msg_lookup_super({{.*}}) #[[GNUSTEP_SUPER_LOOKUP_ATTR:[0-9]+]]
// GNUSTEP: call ptr %{{.*}}({{.*}}), !GNUObjCMessageSend ![[SUPER_MD:[0-9]+]]
+ (id)testSuper {
  return [super foo];
}

@end

// GNUSTEP: attributes #[[LOOKUP_ATTR]] = { memory(read) }
// GCC: attributes #[[GCC_SUPER_LOOKUP_ATTR]] = { nounwind }
// GNUSTEP: attributes #[[GNUSTEP_SUPER_LOOKUP_ATTR]] = { nounwind }
// GCC: ![[INST_MD]] = !{!"foo", !"", i1 false}
// GCC: ![[CLASS_MD]] = !{!"foo", !"Test", i1 true}
// GCC: ![[SUPER_MD]] = !{!"foo", !"Super", i1 true}
// GNUSTEP: ![[INST_MD]] = !{!"foo", !"", i1 false}
// GNUSTEP: ![[CLASS_MD]] = !{!"foo", !"Test", i1 true}
// GNUSTEP: ![[SUPER_MD]] = !{!"foo", !"Super", i1 true}
