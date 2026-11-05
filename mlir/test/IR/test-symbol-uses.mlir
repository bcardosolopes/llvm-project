// RUN: mlir-opt -allow-unregistered-dialect %s -test-symbol-uses -split-input-file -verify-diagnostics

// Symbol references to the module itself don't affect uses of symbols within
// its table.
// expected-remark@below {{symbol_removable function successfully erased}}
module attributes {sym.outside_use = @symbol_foo } {
  // expected-remark@+1 {{symbol has 3 uses}}
  func.func private @symbol_foo()

  // expected-remark@below {{symbol has no uses}}
  // expected-remark@below {{found use of symbol : @symbol_foo}}
  // expected-remark@below {{symbol contains 3 nested references}}
  func.func @symbol_bar() attributes {sym.use = @symbol_foo} {
    // expected-remark@+1 {{found use of symbol : @symbol_foo}}
    "foo.op"() {
      non_symbol_attr,
      use = [{nested_symbol = [@symbol_foo]}],
      z_other_non_symbol_attr
    } : () -> ()
    // expected-remark@+1 {{found use of symbol : @symbol_foo}}
    "foo.op"() { use = distinct[0]<@symbol_foo> } : () -> ()
  }

  // expected-remark@below {{symbol has no uses}}
  func.func private @symbol_removable()

  // expected-remark@+1 {{symbol has 1 use}}
  func.func private @symbol_baz()

  // expected-remark@+1 {{found use of symbol : @symbol_baz}}
  module attributes {test.reference = @symbol_baz} {
    "foo.op"() {test.nested_reference = @symbol_baz} : () -> ()
  }
}

// -----

// Test nested attribute support
module {
  // expected-remark@+1 {{symbol has 2 uses}}
  module @module_b {
    // expected-remark@+1 {{symbol has 1 uses}}
    module @module_c {
      // expected-remark@+1 {{symbol has 1 uses}}
      func.func nested @foo()
    }
  }

  // expected-remark@below {{symbol has no uses}}
  // expected-remark@below {{symbol contains 2 nested references}}
  func.func @symbol_bar() {
    // expected-remark@below {{found use of symbol : @module_b::@module_c::@foo : "foo"}}
    // expected-remark@below {{found use of symbol : @module_b::@module_c::@foo : "module_c"}}
    // expected-remark@below {{found use of symbol : @module_b::@module_c::@foo : "module_b"}}
    // expected-remark@below {{found use of symbol : @module_b : "module_b"}}
    "foo.op"() {
      use_1 = [{ nested_symbol = [@module_b::@module_c::@foo]}],
      use_2 = @module_b
    } : () -> ()
  }
}


// -----

// expected-remark@+1 {{contains an unknown nested operation that 'may' define a new symbol table}}
func.func @symbol_bar() {
  "foo.possibly_unknown_symbol_table"() ({
  }) : () -> ()
}

// -----

// A registered op that keeps its inherent attributes in properties, as the
// test dialect's ops do: the symbol references there are uses, and so are the
// ones in its discardable attributes. The walk skips the attributes it already
// found to hold no reference -- the dictionary both ops carry -- and still
// reports an attribute that holds one on every op that carries it.
module {
  // expected-remark@+1 {{symbol has 2 uses}}
  func.func private @first_property()

  // expected-remark@+1 {{symbol has 2 uses}}
  func.func private @second_property()

  // expected-remark@+1 {{symbol has 2 uses}}
  func.func private @discardable()

  // expected-remark@below {{symbol has no uses}}
  // expected-remark@below {{symbol contains 6 nested references}}
  func.func @user() {
    // expected-remark@below {{found use of symbol : @first_property}}
    // expected-remark@below {{found use of symbol : @second_property}}
    // expected-remark@below {{found use of symbol : @discardable}}
    %0 = "test.op_with_effects_a"() <{first = @first_property, second = @second_property}> {no_refs = {k = 1 : i32}, refs = [@discardable]} : () -> memref<i32>
    // expected-remark@below {{found use of symbol : @first_property}}
    // expected-remark@below {{found use of symbol : @second_property}}
    // expected-remark@below {{found use of symbol : @discardable}}
    %1 = "test.op_with_effects_a"() <{first = @first_property, second = @second_property}> {no_refs = {k = 1 : i32}, refs = [@discardable]} : () -> memref<i32>
    return
  }
}
