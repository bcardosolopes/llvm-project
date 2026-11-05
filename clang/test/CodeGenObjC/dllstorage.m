// RUN: %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -fobjc-runtime=ios -fobjc-exceptions -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefixes=CHECK-IR,CHECK-COFF-IR %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -fobjc-runtime=ios -fobjc-exceptions -fclangir -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefixes=CHECK-CIR-IR,CHECK-CIR-COFF-IR %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -fobjc-runtime=gnustep-2.0 -fobjc-exceptions -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefix CHECK-NF %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -fobjc-runtime=gnustep-2.0 -fobjc-exceptions -fclangir -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefix CHECK-CIR-NF %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -fobjc-runtime=gnustep-2.0 -fobjc-exceptions -emit-llvm -o - %s | FileCheck -check-prefix CHECK-NF-DECL %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -fobjc-runtime=gnustep-2.0 -fobjc-exceptions -fclangir -emit-llvm -o - %s | FileCheck -check-prefix CHECK-CIR-NF-DECL %s %}
// RUN: %clang_cc1 -triple i686-windows-itanium -fms-extensions -fobjc-runtime=macosx -fdeclspec -fobjc-exceptions -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefix CHECK-IR %s
// RUN: %clang_cc1 -triple i686-windows-itanium -fms-extensions -fobjc-runtime=objfw -fdeclspec -fobjc-exceptions -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefix CHECK-FW %s
// RUN: %if cir-support %{ %clang_cc1 -triple i686-windows-itanium -fms-extensions -fobjc-runtime=objfw -fdeclspec -fobjc-exceptions -fclangir -emit-llvm -o - %s | FileCheck -allow-deprecated-dag-overlap -check-prefix CHECK-CIR-FW %s %}

// CHECK-IR-DAG: @_objc_empty_cache = external dllimport global %struct._objc_cache
// CHECK-CIR-IR-DAG: @_objc_empty_cache = external dllimport global

__declspec(dllimport)
@interface I
+ (instancetype) new;
@end

// CHECK-IR-DAG: @"OBJC_METACLASS_$_I" = external dllimport global %struct._class_t
// CHECK-IR-DAG: @"OBJC_CLASS_$_I" = external dllimport global %struct._class_t
// CHECK-CIR-IR-DAG: @"OBJC_METACLASS_$_I" = external dllimport global
// CHECK-CIR-IR-DAG: @"OBJC_CLASS_$_I" = external dllimport global

// CHECK-NF-DAG: @"$_OBJC_CLASS_I" = external dllimport global ptr
// CHECK-CIR-NF-DAG: @"$_OBJC_CLASS_I" = external dllimport global ptr

__declspec(dllexport)
@interface J : I
@end

// CHECK-IR-DAG: @"OBJC_METACLASS_$_J" = dso_local dllexport global %struct._class_t
// CHECK-IR-DAG: @"OBJC_CLASS_$_J" = dso_local dllexport global %struct._class_t
// CHECK-CIR-IR-DAG: @"OBJC_METACLASS_$_J" = dso_local dllexport global
// CHECK-CIR-IR-DAG: @"OBJC_CLASS_$_J" = dso_local dllexport global

// CHECK-FW-DAG: @_OBJC_METACLASS_J = dso_local dllexport global
// CHECK-FW-DAG: @_OBJC_CLASS_J = dso_local dllexport global
// CHECK-CIR-FW-DAG: @_OBJC_METACLASS_J = dso_local dllexport global
// CHECK-CIR-FW-DAG: @_OBJC_CLASS_J = dso_local dllexport global

// CHECK-NF-DAG: @"$_OBJC_METACLASS_J" = internal global
// CHECK-NF-DAG: @"$_OBJC_CLASS_J" = dllexport global
// CHECK-CIR-NF-DAG: @"$_OBJC_CLASS_J" = dllexport global
// CHECK-CIR-NF-DAG: @"$_OBJC_REF_CLASS_J" = dllexport global ptr @"$_OBJC_CLASS_J", section ".objcrt$CLR$m"

@implementation J {
  id _ivar;
}
@end

// CHECK-IR-DAG: @"OBJC_IVAR_$_J._ivar" = global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_J._ivar" = global i32

// CHECK-NF-DAG: @"__objc_ivar_offset_J._ivar.@" = hidden global i32

@interface K : J
@end

// CHECK-IR-DAG: @"OBJC_METACLASS_$_K" = dso_local global %struct._class_t
// CHECK-IR-DAG: @"OBJC_CLASS_$_K" = dso_local global %struct._class_t

// CHECK-FW-DAG: @_OBJC_METACLASS_K = dso_local global
// CHECK-FW-DAG: @_OBJC_CLASS_K = dso_local global
// CHECK-CIR-FW-DAG: @_OBJC_METACLASS_K = dso_local global
// CHECK-CIR-FW-DAG: @_OBJC_CLASS_K = dso_local global

// CHECK-NF-DAG: @"$_OBJC_METACLASS_K" = internal global
// CHECK-NF-DAG: @"$_OBJC_CLASS_K" = global

@implementation K {
  id _ivar;
}
@end

// CHECK-IR-DAG: @"OBJC_IVAR_$_K._ivar" = global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_K._ivar" = global i32

// CHECK-NF-DAG: @"__objc_ivar_offset_K._ivar.@" = hidden global i32

__declspec(dllexport)
@interface L : K
@end

// CHECK-IR-DAG: @"OBJC_METACLASS_$_L" = dso_local dllexport global %struct._class_t
// CHECK-IR-DAG: @"OBJC_CLASS_$_L" = dso_local dllexport global %struct._class_t
// CHECK-CIR-IR-DAG: @"OBJC_METACLASS_$_L" = dso_local dllexport global
// CHECK-CIR-IR-DAG: @"OBJC_CLASS_$_L" = dso_local dllexport global

// CHECK-FW-DAG: @_OBJC_METACLASS_L = dso_local dllexport global
// CHECK-FW-DAG: @_OBJC_CLASS_L = dso_local dllexport global
// CHECK-CIR-FW-DAG: @_OBJC_METACLASS_L = dso_local dllexport global
// CHECK-CIR-FW-DAG: @_OBJC_CLASS_L = dso_local dllexport global

// CHECK-NF-DAG: @"$_OBJC_METACLASS_L" = internal global
// CHECK-NF-DAG: @"$_OBJC_CLASS_L" = dllexport global
// CHECK-CIR-NF-DAG: @"$_OBJC_CLASS_L" = dllexport global
// CHECK-CIR-NF-DAG: @"$_OBJC_REF_CLASS_L" = dllexport global ptr @"$_OBJC_CLASS_L", section ".objcrt$CLR$m"

@implementation L {
  id _none;

  @public
  id _public;

  @protected
  id _protected;

  @package
  id _package;

  @private
  id _private;
}
@end

// CHECK-IR-DAG: @"OBJC_IVAR_$_L._none" = global i32
// CHECK-IR-DAG: @"OBJC_IVAR_$_L._public" = dllexport global i32
// CHECK-IR-DAG: @"OBJC_IVAR_$_L._protected" = dllexport global i32
// CHECK-IR-DAG: @"OBJC_IVAR_$_L._package" = global i32
// CHECK-IR-DAG: @"OBJC_IVAR_$_L._private" = global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_L._none" = global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_L._public" = dllexport global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_L._protected" = dllexport global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_L._package" = global i32
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_L._private" = global i32

// CHECK-NF-DAG: @"__objc_ivar_offset_L._none.@" = hidden global i32
// CHECK-NF-DAG: @"__objc_ivar_offset_L._public.@" = dso_local dllexport global i32
// CHECK-NF-DAG: @"__objc_ivar_offset_L._protected.@" = dso_local dllexport global i32
// CHECK-NF-DAG: @"__objc_ivar_offset_L._package.@" = hidden global i32
// CHECK-NF-DAG: @"__objc_ivar_offset_L._private.@" = hidden global i32
// CHECK-CIR-NF-DAG: @"__objc_ivar_offset_L._public.@" = dso_local dllexport global i32 8
// CHECK-CIR-NF-DAG: @"__objc_ivar_offset_L._protected.@" = dso_local dllexport global i32 16

__declspec(dllimport)
@interface M : I {
  @public
  id _ivar;
}
@end

// CHECK-FW-DAG: @_OBJC_CLASS_M = external dllimport global i32
// CHECK-CIR-FW-DAG: @_OBJC_CLASS_M = external dllimport global

// CHECK-IR-DAG: @"OBJC_IVAR_$_M._ivar" = external dllimport global i32{{$}}
// CHECK-CIR-IR-DAG: @"OBJC_IVAR_$_M._ivar" = external dllimport global i32{{$}}

// CHECK-NF-DAG: @"$_OBJC_REF_CLASS_M" = external dllimport global ptr
// CHECK-NF-DAG: @"__objc_ivar_offset_M._ivar.@" = external dllimport global i32
// CHECK-CIR-NF-DAG: @"$_OBJC_CLASS_M" = external dllimport global ptr
// CHECK-CIR-NF-DAG: @"$_OBJC_REF_CLASS_M" = external dllimport global ptr
// CHECK-CIR-NF-DAG: @"__objc_ivar_offset_M._ivar.@" = external dllimport global i32

__declspec(dllexport)
__attribute__((__objc_exception__))
@interface N : I
@end

// CHECK-FW-DAG: @_OBJC_METACLASS_N = dso_local dllexport global
// CHECK-FW-DAG: @_OBJC_CLASS_N = dso_local dllexport global
// CHECK-CIR-FW-DAG: @_OBJC_METACLASS_N = dso_local dllexport global
// CHECK-CIR-FW-DAG: @_OBJC_CLASS_N = dso_local dllexport global

// CHECK-NF-DAG: @"$_OBJC_CLASS_N" = dllexport global
// CHECK-CIR-NF-DAG: @"$_OBJC_CLASS_N" = dllexport global
// CHECK-CIR-NF-DAG: @"$_OBJC_REF_CLASS_N" = dllexport global ptr @"$_OBJC_CLASS_N", section ".objcrt$CLR$m"
// CHECK-NF-DECL: declare dso_local ptr @objc_msg_lookup_sender(ptr captures(none), ptr, ptr)
// CHECK-CIR-NF-DECL: declare dso_local ptr @objc_msg_lookup_sender(ptr captures(none), ptr, ptr)

@implementation N : I
@end

// CHECK-IR-DAG: @"OBJC_EHTYPE_$_N" = dso_local dllexport global %struct._objc_typeinfo
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_N" = dso_local dllexport global

__declspec(dllimport)
__attribute__((__objc_exception__))
@interface O : I
@end

// CHECK-IR-DAG: @"OBJC_EHTYPE_$_O" = external dllimport global %struct._objc_typeinfo
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_O" = external dllimport global

__attribute__((__objc_exception__))
@interface P : I
@end

// CHECK-IR-DAG: @"OBJC_EHTYPE_$_P" = external dso_local global %struct._objc_typeinfo
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_P" = external dso_local global

@interface Q : M
@property (assign) id prop;
@end

@implementation Q {
  id _prop;
}
@synthesize prop = _prop;
@end

// CHECK-COFF-IR-DAG: @"OBJC_IVAR_$_Q._prop" = global i32 8, align 4{{$}}
// CHECK-CIR-COFF-IR-DAG: @"OBJC_IVAR_$_Q._prop" = global i32 8, align 4{{$}}

id f(Q *q) {
  return q->_ivar;
}

// CHECK-IR-DAG: @"OBJC_IVAR_$_M._ivar" = external dllimport global i32{{$}}

// CHECK-NF-DAG: @"__objc_ivar_offset_M._ivar.@" = external dllimport global i32

int g(void) {
  @autoreleasepool {
    M *mi = [M new];
    @try {
      mi->_ivar = (void *)0;
      @throw(@"CFConstantString");
    } @catch (id) {
      return 1;
    } @catch (I *) {
      return 2;
    } @catch (J *) {
      return 3;
    } @catch (K *) {
      return 4;
    } @catch (L *) {
      return 5;
    } @catch (M *) {
      return 6;
    } @catch (N *) {
      return 7;
    } @catch (O *) {
      return 8;
    } @catch (P *) {
      return 9;
    }
  }
  return 0;
}

// CHECK-IR-DAG: @OBJC_EHTYPE_id = external dllimport global %struct._objc_typeinfo
// CHECK-IR-DAG: @"OBJC_EHTYPE_$_I" = weak global %struct._objc_typeinfo
// CHECK-IR-DAG: @"OBJC_EHTYPE_$_K" = weak global %struct._objc_typeinfo
// CHECK-IR-DAG: @"OBJC_EHTYPE_$_L" = weak global %struct._objc_typeinfo
// CHECK-IR-DAG: @"OBJC_EHTYPE_$_M" = weak global %struct._objc_typeinfo
// CHECK-CIR-FW-DAG: @_OBJC_CLASS_NSConstantString = extern_weak global ptr
// CHECK-CIR-FW-DAG: @.str = private unnamed_addr constant [17 x i8] c"CFConstantString\00", align 1
// CHECK-CIR-FW-DAG: @.objc_str = internal global { ptr, ptr, i32 } { ptr @_OBJC_CLASS_NSConstantString, ptr @.str, i32 16 }, align 4
// CHECK-CIR-FW-DAG: @.objc_static_class_name = private unnamed_addr constant [17 x i8] c"NXConstantString\00", align 1
// CHECK-CIR-FW-DAG: @.objc_statics = internal global { ptr, [2 x ptr] } { ptr @.objc_static_class_name, [2 x ptr] [ptr @.objc_str, ptr null] }, align 4
// CHECK-CIR-FW-DAG: @.objc_statics_ptr = internal global [2 x ptr] [ptr @.objc_statics, ptr null], align 4
// CHECK-CIR-FW-DAG: @.objc_symtab = internal global { i32, ptr, i16, i16, [8 x ptr] } { i32 1, ptr @.objc_selector_list, i16 5, i16 1, [8 x ptr] [ptr @_OBJC_CLASS_J, ptr @_OBJC_CLASS_K, ptr @_OBJC_CLASS_L, ptr @_OBJC_CLASS_N, ptr @_OBJC_CLASS_Q, ptr @.objc_protocol_holder_category, ptr @.objc_statics_ptr, ptr null] }, align 4
// CHECK-CIR-FW: call void @objc_exception_throw(ptr @.objc_str)
// CHECK-CIR-IR-DAG: @objc_ehtype_vtable = external dllimport global
// CHECK-CIR-IR-DAG: @OBJC_EHTYPE_id = external dllimport global
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_I" = weak global
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_K" = weak global
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_L" = weak global
// CHECK-CIR-IR-DAG: @"OBJC_EHTYPE_$_M" = weak global
// CHECK-CIR-IR-DAG: @__CFConstantStringClassReference = external dllimport global
// CHECK-COFF-IR-DAG: @"OBJC_CLASS_$_NSAutoreleasePool" = external global %struct._class_t
// CHECK-COFF-IR-DAG: @"OBJC_CLASSLIST_REFERENCES_$_" = private global ptr @"OBJC_CLASS_$_NSAutoreleasePool", section ".objc_classrefs$B", align 8
// CHECK-CIR-COFF-IR-DAG: @"OBJC_CLASS_$_NSAutoreleasePool" = external global
// CHECK-CIR-COFF-IR-DAG: @"OBJC_CLASSLIST_REFERENCES_{{.*}}" = private global ptr @"OBJC_CLASS_$_NSAutoreleasePool", section ".objc_classrefs$B", align 8
// CHECK-NF-DAG: @.objc_sel_name_prop = linkonce_odr hidden constant [5 x i8] c"prop\00", comdat
// CHECK-NF-DAG: @".objc_sel_types_@16@0:8" = linkonce_odr hidden constant [8 x i8] c"@16@0:8\00", comdat
// CHECK-NF-DAG: @".objc_selector_prop_@16@0:8" = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_prop, ptr @".objc_sel_types_@16@0:8" }, section ".objcrt$SEL$m", comdat, align 8
// CHECK-NF-DAG: @".objc_sel_name_setProp:" = linkonce_odr hidden constant [9 x i8] c"setProp:\00", comdat
// CHECK-NF-DAG: @".objc_selector_setProp:_v24@0:8@16" = linkonce_odr hidden global { ptr, ptr } { ptr @".objc_sel_name_setProp:", ptr @".objc_sel_types_v24@0:8@16" }, section ".objcrt$SEL$m", comdat, align 8
// CHECK-NF-DAG: @.objc_sel_name_new = linkonce_odr hidden constant [4 x i8] c"new\00", comdat
// CHECK-NF-DAG: @".objc_selector_new_@16@0:8" = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_new, ptr @".objc_sel_types_@16@0:8" }, section ".objcrt$SEL$m", comdat, align 8
// CHECK-NF-DAG: @"??_7type_info@@6B@" = external constant ptr
// CHECK-NF-DAG: @"??_R0PEAUobjc_object@@@8" = linkonce_odr global
// CHECK-NF-DAG: @"??_R0PEAUN@@@8" = linkonce_odr global
// CHECK-CIR-NF-DAG: @"$_OBJC_CLASS_NSConstantString" = external dllimport global ptr
// CHECK-CIR-NF-DAG: @"$_OBJC_REF_CLASS_Q" = global ptr @"$_OBJC_CLASS_Q", section ".objcrt$CLR$m"
// CHECK-CIR-NF-DAG: @.objc_sel_name_prop = linkonce_odr hidden constant [5 x i8] c"prop\00", comdat
// CHECK-CIR-NF-DAG: @".objc_sel_types_@16@0:8" = linkonce_odr hidden constant [8 x i8] c"@16@0:8\00", comdat
// CHECK-CIR-NF-DAG: @".objc_selector_prop_@16@0:8" = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_prop, ptr @".objc_sel_types_@16@0:8" }, section ".objcrt$SEL$m", comdat, align 8
// CHECK-CIR-NF-DAG: @".objc_sel_name_setProp:" = linkonce_odr hidden constant [9 x i8] c"setProp:\00", comdat
// CHECK-CIR-NF-DAG: @".objc_selector_setProp:_v24@0:8@16" = linkonce_odr hidden global { ptr, ptr } { ptr @".objc_sel_name_setProp:", ptr @".objc_sel_types_v24@0:8@16" }, section ".objcrt$SEL$m", comdat, align 8
// CHECK-CIR-NF-DAG: @.objc_sel_name_new = linkonce_odr hidden constant [4 x i8] c"new\00", comdat
// CHECK-CIR-NF-DAG: @".objc_selector_new_@16@0:8" = linkonce_odr hidden global { ptr, ptr } { ptr @.objc_sel_name_new, ptr @".objc_sel_types_@16@0:8" }, section ".objcrt$SEL$m", comdat, align 8
// CHECK-CIR-NF-DAG: @"??_7type_info@@6B@" = external constant ptr
// CHECK-CIR-NF-DAG: @"??_R0PEAUobjc_object@@@8" = linkonce_odr global
// CHECK-CIR-NF-DAG: @"??_R0PEAUN@@@8" = linkonce_odr global
// CHECK-CIR-NF-NOT: @__objc_eh_str_
// CHECK-CIR-NF: @.objc_str_CFConstantString = linkonce_odr hidden global { ptr, i32, i32, i32, i32, ptr } { ptr null, i32 0, i32 16, i32 16, i32 0, ptr {{.*}} }, section ".objcrt$STR$m", comdat, align 8
// CHECK-CIR-NF: @.objc_early_init_ptr = internal constant ptr @.objc_early_init, section ".CRT$XCLb"
// CHECK-CIR-NF: @llvm.used = appending global [7 x ptr] [ptr @"$_OBJC_INIT_CLASS_J", ptr @"$_OBJC_INIT_CLASS_K", ptr @"$_OBJC_INIT_CLASS_L", ptr @"$_OBJC_INIT_CLASS_N", ptr @"$_OBJC_INIT_CLASS_Q", ptr @.objc_ctor, ptr @.objc_early_init_ptr], section "llvm.metadata"
// CHECK-CIR-NF-LABEL: define internal ptr @_i_Q__prop(
// CHECK-CIR-NF: load i32, ptr @"__objc_ivar_offset_Q._prop.@", align 4{{$}}
// CHECK-CIR-NF-LABEL: define internal void @_i_Q__setProp_(
// CHECK-CIR-NF: load i32, ptr @"__objc_ivar_offset_Q._prop.@", align 4{{$}}
// CHECK-CIR-NF-LABEL: define dso_local ptr @f(
// CHECK-CIR-NF: load i32, ptr @"__objc_ivar_offset_M._ivar.@", align 4{{$}}
// CHECK-NF-LABEL: define dso_local i32 @g(
// CHECK-NF: call ptr @objc_msg_lookup_sender(ptr {{.*}}, ptr {{.*}}, ptr null) #[[NF_LOOKUP_ATTR:[0-9]+]], !GNUObjCMessageSend ![[NF_MSG:[0-9]+]]
// CHECK-NF: load volatile ptr, ptr {{.*}}, align 8{{$}}
// CHECK-CIR-NF-LABEL: define dso_local i32 @g(
// CHECK-CIR-NF: call ptr @objc_msg_lookup_sender(ptr {{.*}}, ptr {{.*}}, ptr null) #[[CIR_LOOKUP_ATTR:[0-9]+]], !GNUObjCMessageSend ![[CIR_MSG:[0-9]+]]
// CHECK-CIR-NF: load volatile ptr, ptr {{.*}}, align 8{{$}}
// CHECK-CIR-NF: load i32, ptr @"__objc_ivar_offset_M._ivar.@", align 4{{$}}
// CHECK-CIR-NF: define internal void @.objc_early_init(){{.*}} {
// CHECK-CIR-NF-NEXT:   store ptr @"$_OBJC_CLASS_I", ptr {{.*}}@"$_OBJC_CLASS_J"{{.*}}, align 8
// CHECK-CIR-NF-NEXT:   store ptr @"$_OBJC_CLASS_J", ptr {{.*}}@"$_OBJC_CLASS_K"{{.*}}, align 8
// CHECK-CIR-NF-NEXT:   store ptr @"$_OBJC_CLASS_K", ptr {{.*}}@"$_OBJC_CLASS_L"{{.*}}, align 8
// CHECK-CIR-NF-NEXT:   store ptr @"$_OBJC_CLASS_I", ptr {{.*}}@"$_OBJC_CLASS_N"{{.*}}, align 8
// CHECK-CIR-NF-NEXT:   store ptr @"$_OBJC_CLASS_M", ptr {{.*}}@"$_OBJC_CLASS_Q"{{.*}}, align 8
// CHECK-CIR-NF-NEXT:   store ptr @"$_OBJC_CLASS_NSConstantString", ptr @.objc_str_CFConstantString, align 8
// CHECK-CIR-NF-NEXT:   ret void
// CHECK-NF: attributes #[[NF_LOOKUP_ATTR]] = { memory(read) }
// CHECK-CIR-NF: attributes #[[CIR_LOOKUP_ATTR]] = { memory(read) }
