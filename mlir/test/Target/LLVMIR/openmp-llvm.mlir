// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @test_stand_alone_directives() {
  omp.barrier

  omp.taskwait

  omp.taskyield

  llvm.return
}

llvm.func @test_flush_construct(%arg0: !llvm.ptr) {
  omp.flush

  omp.flush (%arg0 : !llvm.ptr)

  omp.flush (%arg0, %arg0 : !llvm.ptr, !llvm.ptr)

  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {in_type = i32, name = "a"} : (i64) -> !llvm.ptr
  omp.flush
  %2 = llvm.load %1 : !llvm.ptr -> i32

  llvm.return
}

llvm.func @test_omp_parallel_1() -> () {
  omp.parallel {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


llvm.func @body(i64)

llvm.func @test_omp_parallel_2() -> () {
  omp.parallel {
    ^bb0:
      %0 = llvm.mlir.constant(1 : index) : i64
      %1 = llvm.mlir.constant(42 : index) : i64
      llvm.call @body(%0) : (i64) -> ()
      llvm.call @body(%1) : (i64) -> ()
      llvm.br ^bb1

    ^bb1:
      %2 = llvm.add %0, %1 : i64
      llvm.call @body(%2) : (i64) -> ()
      omp.terminator
  }
  llvm.return
}


llvm.func @test_omp_parallel_num_threads_1(%arg0: i32) -> () {
  omp.parallel num_threads(%arg0: i32) {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


llvm.func @test_omp_parallel_num_threads_2() -> () {
  %0 = llvm.mlir.constant(4 : index) : i32
  omp.parallel num_threads(%0: i32) {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


llvm.func @test_omp_parallel_num_threads_3() -> () {
  %0 = llvm.mlir.constant(4 : index) : i32
  omp.parallel num_threads(%0: i32) {
    omp.barrier
    omp.terminator
  }
  %1 = llvm.mlir.constant(8 : index) : i32
  omp.parallel num_threads(%1: i32) {
    omp.barrier
    omp.terminator
  }

  llvm.return
}



llvm.mlir.global private unnamed_addr constant @strict_msg("msg\00") : !llvm.array<4 x i8>

llvm.func @test_omp_parallel_num_threads_strict(%arg0: i32) -> () {
  %msg = llvm.mlir.addressof @strict_msg : !llvm.ptr
  omp.parallel num_threads(strict: %arg0 : i32) severity(fatal) message(%msg : !llvm.ptr) {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


llvm.func @test_omp_parallel_if_1(%arg0: i32) -> () {

  %0 = llvm.mlir.constant(0 : index) : i32
  %1 = llvm.icmp "slt" %arg0, %0 : i32


  omp.parallel if(%1) {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


// -----

llvm.func @test_omp_parallel_attrs() -> () attributes {
  target_cpu = "x86-64",
  target_features = #llvm.target_features<["+mmx", "+sse"]>
} {
  omp.parallel {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


// -----

llvm.func @test_omp_parallel_3() -> () {
  omp.parallel proc_bind(master) {
    omp.barrier
    omp.terminator
  }
  omp.parallel proc_bind(close) {
    omp.barrier
    omp.terminator
  }
  omp.parallel proc_bind(spread) {
    omp.barrier
    omp.terminator
  }

  llvm.return
}


llvm.func @test_omp_parallel_4() -> () {
  omp.parallel {
    omp.barrier

    omp.parallel {
      omp.barrier
      omp.terminator
    }

    omp.barrier
    omp.terminator
  }
  llvm.return
}

llvm.func @test_omp_parallel_5() -> () {
  omp.parallel {
    omp.barrier

    omp.parallel {
      omp.parallel {
        omp.barrier
        omp.terminator
      }
      omp.terminator
    }

    omp.barrier
    omp.terminator
  }
  llvm.return
}

llvm.func @test_omp_master() -> () {
  omp.parallel {
    omp.master {
      omp.terminator
    }
    omp.terminator
  }
  omp.parallel {
    omp.parallel {
      omp.master {
        omp.terminator
      }
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @test_omp_masked(%arg0: i32)-> () {
  omp.parallel {
    omp.masked filter(%arg0: i32) {
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @wsloop_linear(%lb : i32, %ub : i32, %step : i32, %x : !llvm.ptr) {







  omp.wsloop linear(%x = %step : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  } {linear_var_types = [i32]}
  llvm.return
}

// -----


llvm.func @wsloop_simple(%arg0: !llvm.ptr) {
  %0 = llvm.mlir.constant(42 : index) : i64
  %1 = llvm.mlir.constant(10 : index) : i64
  %2 = llvm.mlir.constant(1 : index) : i64
  omp.parallel {
    "omp.wsloop"() ({
      omp.loop_nest (%arg1) : i64 = (%1) to (%0) step (%2) {
        // The form of the emitted IR is controlled by OpenMPIRBuilder and
        // tested there. Just check that the right functions are called.
        %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
        %4 = llvm.getelementptr %arg0[%arg1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
        llvm.store %3, %4 : f32, !llvm.ptr
        omp.yield
      }
    }) : () -> ()
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @wsloop_inclusive_1(%arg0: !llvm.ptr) {
  %0 = llvm.mlir.constant(42 : index) : i64
  %1 = llvm.mlir.constant(10 : index) : i64
  %2 = llvm.mlir.constant(1 : index) : i64
  "omp.wsloop"() ({
    omp.loop_nest (%arg1) : i64 = (%1) to (%0) step (%2) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      %4 = llvm.getelementptr %arg0[%arg1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      omp.yield
    }
  }) : () -> ()
  llvm.return
}

// -----

llvm.func @wsloop_inclusive_2(%arg0: !llvm.ptr) {
  %0 = llvm.mlir.constant(42 : index) : i64
  %1 = llvm.mlir.constant(10 : index) : i64
  %2 = llvm.mlir.constant(1 : index) : i64
  "omp.wsloop"() ({
    omp.loop_nest (%arg1) : i64 = (%1) to (%0) inclusive step (%2) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      %4 = llvm.getelementptr %arg0[%arg1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      omp.yield
    }
  }) : () -> ()
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_static_defchunk(%lb : i32, %ub : i32, %step : i32) -> () {
  omp.wsloop schedule(static) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_static_1(%lb : i32, %ub : i32, %step : i32) -> () {
  %static_chunk_size = llvm.mlir.constant(1 : i32) : i32
  omp.wsloop schedule(static = %static_chunk_size : i32) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_static_2(%lb : i32, %ub : i32, %step : i32) -> () {
  %static_chunk_size = llvm.mlir.constant(2 : i32) : i32
  omp.wsloop schedule(static = %static_chunk_size : i32) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(dynamic) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step)  {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic_chunk_const(%lb : i64, %ub : i64, %step : i64) -> () {
  %chunk_size_const = llvm.mlir.constant(2 : i16) : i16
  omp.wsloop schedule(dynamic = %chunk_size_const : i16) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step)  {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_dynamic_chunk_var(%lb : i32, %ub : i32, %step : i32) -> () {
  %1 = llvm.mlir.constant(1 : i64) : i64
  %chunk_size_alloca = llvm.alloca %1 x i16 {bindc_name = "chunk_size", in_type = i16, uniq_name = "_QFsub1Echunk_size"} : (i64) -> !llvm.ptr
  %chunk_size_var = llvm.load %chunk_size_alloca : !llvm.ptr -> i16
  omp.wsloop schedule(dynamic = %chunk_size_var : i16) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_dynamic_chunk_var2(%lb : i32, %ub : i32, %step : i32) -> () {
  %1 = llvm.mlir.constant(1 : i64) : i64
  %chunk_size_alloca = llvm.alloca %1 x i64 {bindc_name = "chunk_size", in_type = i64, uniq_name = "_QFsub1Echunk_size"} : (i64) -> !llvm.ptr
  %chunk_size_var = llvm.load %chunk_size_alloca : !llvm.ptr -> i64
  omp.wsloop schedule(dynamic = %chunk_size_var : i64) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_dynamic_chunk_var3(%lb : i32, %ub : i32, %step : i32, %chunk_size : i32) -> () {
  omp.wsloop schedule(dynamic = %chunk_size : i32) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_auto(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(auto) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_runtime(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(runtime) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_guided(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(guided) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic_nonmonotonic(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(dynamic, nonmonotonic) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic_monotonic(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(dynamic, monotonic) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_runtime_simd(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(runtime, simd) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_guided_simd(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(guided, simd) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @test_omp_wsloop_order(%lb : i64, %ub : i64, %step : i64, %arg0 : !llvm.ptr) {
  omp.wsloop order(concurrent) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      // The form of the emitted IR is controlled by OpenMPIRBuilder and
      // tested there. Just check that the right metadata is added.
      %addr = llvm.getelementptr %arg0[%iv] : (!llvm.ptr, i64) -> !llvm.ptr, i64
      llvm.store %iv, %addr : i64, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @simd_simple(%lb : i64, %ub : i64, %step : i64, %arg0: !llvm.ptr) {
  "omp.simd" () ({
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      // The form of the emitted IR is controlled by OpenMPIRBuilder and
      // tested there. Just check that the right metadata is added.
      %4 = llvm.getelementptr %arg0[%iv] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      omp.yield
    }
  }) : () -> ()

  llvm.return
}

// -----

llvm.func @simd_linear(%lb : i32, %ub : i32, %step : i32, %x : !llvm.ptr) {




  omp.simd linear(%x = %step : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  } {linear_var_types = [i32]}
  llvm.return
}

// -----

// Test linear clause with mismatched types (i32 step, i64 variable)
// This is a regression test for issue #173332
llvm.func @simd_linear_i64_var_i32_step(%lb : i32, %ub : i32, %x : !llvm.ptr) {
  %step = llvm.mlir.constant(1 : i32) : i32




// Verify type conversions: iv (i32) is extended to i64 before multiplication
// Verify multiplication and addition use consistent i64 types
  omp.simd linear(%x = %step : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  } {linear_var_types = [i64]}
  llvm.return
}

// -----

// Test linear clause with floating-point variable and i32 step
// This tests the floating-point path in updateLinearVar
llvm.func @simd_linear_f64_var_i32_step(%lb : i32, %ub : i32, %x : !llvm.ptr) {
  %step = llvm.mlir.constant(1 : i32) : i32




// Verify integer multiplication, load, and conversion to float
  omp.simd linear(%x = %step : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  } {linear_var_types = [f64]}
  llvm.return
}

// -----

llvm.func @simd_simple_multiple(%lb1 : i64, %ub1 : i64, %step1 : i64, %lb2 : i64, %ub2 : i64, %step2 : i64, %arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  omp.simd {
    omp.loop_nest (%iv1, %iv2) : i64 = (%lb1, %lb2) to (%ub1, %ub2) inclusive step (%step1, %step2) collapse(2) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      // The form of the emitted IR is controlled by OpenMPIRBuilder and
      // tested there. Just check that the right metadata is added and collapsed
      // loop bound is generated (Collapse clause is represented as a loop with
      // list of indices, bounds and steps where the size of the list is equal
      // to the collapse value.)
      %4 = llvm.getelementptr %arg0[%iv1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      %5 = llvm.getelementptr %arg1[%iv2] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      llvm.store %3, %5 : f32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @simd_simple_multiple_simdlen(%lb1 : i64, %ub1 : i64, %step1 : i64, %lb2 : i64, %ub2 : i64, %step2 : i64, %arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  omp.simd simdlen(2) {
    omp.loop_nest (%iv1, %iv2) : i64 = (%lb1, %lb2) to (%ub1, %ub2) step (%step1, %step2) collapse(2) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      // The form of the emitted IR is controlled by OpenMPIRBuilder and
      // tested there. Just check that the right metadata is added.
      %4 = llvm.getelementptr %arg0[%iv1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      %5 = llvm.getelementptr %arg1[%iv2] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      llvm.store %3, %5 : f32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @simd_simple_multiple_safelen(%lb1 : i64, %ub1 : i64, %step1 : i64, %lb2 : i64, %ub2 : i64, %step2 : i64, %arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  omp.simd safelen(2) {
    omp.loop_nest (%iv1, %iv2) : i64 = (%lb1, %lb2) to (%ub1, %ub2) step (%step1, %step2) collapse(2) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      %4 = llvm.getelementptr %arg0[%iv1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      %5 = llvm.getelementptr %arg1[%iv2] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      llvm.store %3, %5 : f32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @simd_simple_multiple_simdlen_safelen(%lb1 : i64, %ub1 : i64, %step1 : i64, %lb2 : i64, %ub2 : i64, %step2 : i64, %arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  omp.simd simdlen(1) safelen(2) {
    omp.loop_nest (%iv1, %iv2) : i64 = (%lb1, %lb2) to (%ub1, %ub2) step (%step1, %step2) collapse(2) {
      %3 = llvm.mlir.constant(2.000000e+00 : f32) : f32
      %4 = llvm.getelementptr %arg0[%iv1] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      %5 = llvm.getelementptr %arg1[%iv2] : (!llvm.ptr, i64) -> !llvm.ptr, f32
      llvm.store %3, %4 : f32, !llvm.ptr
      llvm.store %3, %5 : f32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @simd_if(%arg0: !llvm.ptr {fir.bindc_name = "n"}, %arg1: !llvm.ptr {fir.bindc_name = "threshold"}) {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {adapt.valuebyref, in_type = i32, operandSegmentSizes = array<i32: 0, 0>} : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "i", in_type = i32, operandSegmentSizes = array<i32: 0, 0>, uniq_name = "_QFtest_simdEi"} : (i64) -> !llvm.ptr
  %4 = llvm.mlir.constant(0 : i32) : i32
  %5 = llvm.load %arg0 : !llvm.ptr -> i32
  %6 = llvm.mlir.constant(1 : i32) : i32
  %7 = llvm.load %arg0 : !llvm.ptr -> i32
  %8 = llvm.load %arg1 : !llvm.ptr -> i32
  %9 = llvm.icmp "sge" %7, %8 : i32
  omp.simd if(%9) {
    omp.loop_nest (%arg2) : i32 = (%4) to (%5) inclusive step (%6) {
      // The form of the emitted IR is controlled by OpenMPIRBuilder and
      // tested there. Just check that the right metadata is added.
      llvm.store %arg2, %1 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}
// Be sure that llvm.loop.vectorize.enable metadata appears twice

// -----

llvm.func @simd_order() {
  %0 = llvm.mlir.constant(10 : i64) : i64
  %1 = llvm.mlir.constant(1 : i64) : i64
  %2 = llvm.alloca %1 x i64 : (i64) -> !llvm.ptr
  omp.simd order(concurrent) safelen(2) {
    omp.loop_nest (%arg0) : i64 = (%1) to (%0) inclusive step (%1) {
      llvm.store %arg0, %2 : i64, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}
// If clause order(concurrent) is specified then the memory instructions
// are marked parallel even if 'safelen' is finite.
// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_static_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(static) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i32)

llvm.func @test_omp_wsloop_static_chunk_ordered(%lb : i32, %ub : i32, %step : i32) -> () {
  %static_chunk_size = llvm.mlir.constant(1 : i32) : i32
  omp.wsloop schedule(static = %static_chunk_size : i32) ordered(0) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i32) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(dynamic) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_auto_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(auto) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_runtime_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(runtime) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_guided_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(guided) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic_nonmonotonic_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(dynamic, nonmonotonic) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @body(i64)

llvm.func @test_omp_wsloop_dynamic_monotonic_ordered(%lb : i64, %ub : i64, %step : i64) -> () {
  omp.wsloop schedule(dynamic, monotonic) ordered(0) {
    omp.loop_nest (%iv) : i64 = (%lb) to (%ub) step (%step) {
      llvm.call @body(%iv) : (i64) -> ()
      omp.yield
    }
  }
  llvm.return
}

// -----

omp.critical.declare @mutex_none hint(none) // 0
omp.critical.declare @mutex_uncontended hint(uncontended) // 1
omp.critical.declare @mutex_contended hint(contended) // 2
omp.critical.declare @mutex_nonspeculative hint(nonspeculative) // 4
omp.critical.declare @mutex_nonspeculative_uncontended hint(nonspeculative, uncontended) // 5
omp.critical.declare @mutex_nonspeculative_contended hint(nonspeculative, contended) // 6
omp.critical.declare @mutex_speculative hint(speculative) // 8
omp.critical.declare @mutex_speculative_uncontended hint(speculative, uncontended) // 9
omp.critical.declare @mutex_speculative_contended hint(speculative, contended) // 10

llvm.func @omp_critical(%x : !llvm.ptr, %xval : i32) -> () {
  omp.critical {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_none) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_uncontended) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_contended) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_nonspeculative) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_nonspeculative_uncontended) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_nonspeculative_contended) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_speculative) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_speculative_uncontended) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }

  omp.critical(@mutex_speculative_contended) {
    llvm.store %xval, %x : i32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

// -----

// Check that the loop bounds are emitted in the correct location in case of
// collapse. This only checks the overall shape of the IR, detailed checking
// is done by the OpenMPIRBuilder.

llvm.func @collapse_wsloop(
    %0: i32, %1: i32, %2: i32,
    %3: i32, %4: i32, %5: i32,
    %6: i32, %7: i32, %8: i32,
    %20: !llvm.ptr) {
  omp.parallel {
    //
    //
    omp.wsloop {
      omp.loop_nest (%arg0, %arg1, %arg2) : i32 = (%0, %1, %2) to (%3, %4, %5) step (%6, %7, %8) collapse(3) {
        %31 = llvm.load %20 : !llvm.ptr -> i32
        %32 = llvm.add %31, %arg0 : i32
        %33 = llvm.add %32, %arg1 : i32
        %34 = llvm.add %33, %arg2 : i32
        llvm.store %34, %20 : i32, !llvm.ptr
        omp.yield
      }
    }
    omp.terminator
  }
  llvm.return
}

// -----

// Check that the loop bounds are emitted in the correct location in case of
// collapse for dynamic schedule. This only checks the overall shape of the IR,
// detailed checking is done by the OpenMPIRBuilder.


llvm.func @collapse_wsloop_dynamic(
    %0: i32, %1: i32, %2: i32,
    %3: i32, %4: i32, %5: i32,
    %6: i32, %7: i32, %8: i32,
    %20: !llvm.ptr) {
  omp.parallel {
    //
    //
    omp.wsloop schedule(dynamic) {
      omp.loop_nest (%arg0, %arg1, %arg2) : i32 = (%0, %1, %2) to (%3, %4, %5) step (%6, %7, %8) collapse(3) {
        %31 = llvm.load %20 : !llvm.ptr -> i32
        %32 = llvm.add %31, %arg0 : i32
        %33 = llvm.add %32, %arg1 : i32
        %34 = llvm.add %33, %arg2 : i32
        llvm.store %34, %20 : i32, !llvm.ptr
        omp.yield
      }
    }
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @omp_ordered(%arg0 : i32, %arg1 : i32, %arg2 : i32, %arg3 : i64,
    %arg4: i64, %arg5: i64, %arg6: i64) -> () {

  omp.ordered.region {
    omp.terminator
  }

  omp.wsloop ordered(0) {
    omp.loop_nest (%arg7) : i32 = (%arg0) to (%arg1) step (%arg2) {
      omp.ordered.region  {
        omp.terminator
      }
      omp.yield
    }
  }

  omp.wsloop ordered(1) {
    omp.loop_nest (%arg7) : i32 = (%arg0) to (%arg1) step (%arg2) {
      omp.ordered depend_type(dependsink) depend_vec(%arg3 : i64) {doacross_num_loops = 1 : i64}

      omp.ordered depend_type(dependsource) depend_vec(%arg3 : i64) {doacross_num_loops = 1 : i64}

      omp.yield
    }
  }

  omp.wsloop ordered(2) {
    omp.loop_nest (%arg7) : i32 = (%arg0) to (%arg1) step (%arg2) {
      omp.ordered depend_type(dependsink) depend_vec(%arg3, %arg4, %arg5, %arg6 : i64, i64, i64, i64) {doacross_num_loops = 2 : i64}

      omp.ordered depend_type(dependsource) depend_vec(%arg3, %arg4 : i64, i64) {doacross_num_loops = 2 : i64}

      omp.yield
    }
  }

  llvm.return
}

// -----

llvm.func @omp_atomic_read(%arg0 : !llvm.ptr, %arg1 : !llvm.ptr) -> () {

  omp.atomic.read %arg1 = %arg0 : !llvm.ptr, !llvm.ptr, i32

  omp.atomic.read %arg1 = %arg0 memory_order(seq_cst) : !llvm.ptr, !llvm.ptr, i32

  omp.atomic.read %arg1 = %arg0 memory_order(acquire) : !llvm.ptr, !llvm.ptr, i32

  omp.atomic.read %arg1 = %arg0 memory_order(relaxed) : !llvm.ptr, !llvm.ptr, i32
  llvm.return
}

// -----

llvm.func @omp_atomic_read_implicit_cast () {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x f32 {bindc_name = "z"} : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x f64 {bindc_name = "y"} : (i64) -> !llvm.ptr
  %4 = llvm.mlir.constant(1 : i64) : i64
  %5 = llvm.alloca %4 x !llvm.array<2 x struct<(f32, f32)>> {bindc_name = "x"} : (i64) -> !llvm.ptr
  %6 = llvm.mlir.constant(1 : i64) : i64
  %7 = llvm.alloca %6 x i32 {bindc_name = "w"} : (i64) -> !llvm.ptr
  %8 = llvm.mlir.constant(1 : index) : i64
  %9 = llvm.mlir.constant(2 : index) : i64
  %10 = llvm.mlir.constant(1 : i64) : i64
  %11 = llvm.mlir.constant(0 : i64) : i64
  %12 = llvm.sub %8, %10 overflow<nsw> : i64
  %13 = llvm.mul %12, %10 overflow<nsw> : i64
  %14 = llvm.mul %13, %10 overflow<nsw> : i64
  %15 = llvm.add %14, %11 overflow<nsw> : i64
  %16 = llvm.mul %10, %9 overflow<nsw> : i64
  %17 = llvm.getelementptr %5[%15] : (!llvm.ptr, i64) -> !llvm.ptr, !llvm.struct<(f32, f32)>


  omp.atomic.read %3 = %17 : !llvm.ptr, !llvm.ptr, !llvm.struct<(f32, f32)>

  omp.atomic.read %3 = %1 : !llvm.ptr, !llvm.ptr, f32

  omp.atomic.read %3 = %7 : !llvm.ptr, !llvm.ptr, i32

  omp.atomic.read %1 = %3 : !llvm.ptr, !llvm.ptr, f64

  omp.atomic.read %1 = %7 : !llvm.ptr, !llvm.ptr, i32

  omp.atomic.read %7 = %3 : !llvm.ptr, !llvm.ptr, f64

  omp.atomic.read %7 = %1 : !llvm.ptr, !llvm.ptr, f32
  llvm.return
}

// -----

llvm.func @omp_atomic_write(%x: !llvm.ptr, %expr: i32) -> () {
  omp.atomic.write %x = %expr : !llvm.ptr, i32
  omp.atomic.write %x = %expr memory_order(seq_cst) : !llvm.ptr, i32
  omp.atomic.write %x = %expr memory_order(release) : !llvm.ptr, i32
  omp.atomic.write %x = %expr memory_order(relaxed) : !llvm.ptr, i32
  llvm.return
}

// -----

// Checking simple atomicrmw and cmpxchg based translation. This also checks for
// ambigous alloca insert point by putting llvm.mul as the first update operation.
llvm.func @omp_atomic_update(%x:!llvm.ptr, %expr: i32, %xbool: !llvm.ptr, %exprbool: i1) {
  omp.atomic.update %x : !llvm.ptr {
  ^bb0(%xval: i32):
    %newval = llvm.mul %xval, %expr : i32
    omp.yield(%newval : i32)
  }
  omp.atomic.update %x : !llvm.ptr {
  ^bb0(%xval: i32):
    %newval = llvm.add %xval, %expr : i32
    omp.yield(%newval : i32)
  }
  llvm.return
}

// -----

llvm.func @omp_atomic_write() {


    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x f32 {bindc_name = "x"} : (i64) -> !llvm.ptr
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x f32 {bindc_name = "r1"} : (i64) -> !llvm.ptr
    %4 = llvm.mlir.constant(1 : i64) : i64
    %5 = llvm.alloca %4 x !llvm.struct<(f32, f32)> {bindc_name = "c1"} : (i64) -> !llvm.ptr
    %6 = llvm.mlir.constant(1.000000e+00 : f32) : f32
    %7 = llvm.mlir.constant(0.000000e+00 : f32) : f32
    %8 = llvm.mlir.constant(1 : i64) : i64
    %9 = llvm.mlir.constant(1 : i64) : i64
    %10 = llvm.mlir.constant(1 : i64) : i64
    %11 = llvm.load %3 : !llvm.ptr -> f32
    %12 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
    %13 = llvm.insertvalue %11, %12[0] : !llvm.struct<(f32, f32)>
    %14 = llvm.insertvalue %7, %13[1] : !llvm.struct<(f32, f32)>
    omp.atomic.write %5 = %14 : !llvm.ptr, !llvm.struct<(f32, f32)>
    %15 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
    %16 = llvm.insertvalue %6, %15[0] : !llvm.struct<(f32, f32)>
    %17 = llvm.insertvalue %6, %16[1] : !llvm.struct<(f32, f32)>
    omp.atomic.write %5 = %17 : !llvm.ptr, !llvm.struct<(f32, f32)>
    llvm.return
}

// -----





llvm.func @_QPomp_atomic_update_complex() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x !llvm.struct<(f32, f32)> {bindc_name = "ib"} : (i64) -> !llvm.ptr
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x !llvm.struct<(f32, f32)> {bindc_name = "ia"} : (i64) -> !llvm.ptr
    %4 = llvm.mlir.constant(1.000000e+00 : f32) : f32
    %5 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
    %6 = llvm.insertvalue %4, %5[0] : !llvm.struct<(f32, f32)>
    %7 = llvm.insertvalue %4, %6[1] : !llvm.struct<(f32, f32)>
    omp.atomic.update %3 : !llvm.ptr {
    ^bb0(%arg0: !llvm.struct<(f32, f32)>):
      %8 = llvm.extractvalue %arg0[0] : !llvm.struct<(f32, f32)>
      %9 = llvm.extractvalue %arg0[1] : !llvm.struct<(f32, f32)>
      %10 = llvm.extractvalue %7[0] : !llvm.struct<(f32, f32)>
      %11 = llvm.extractvalue %7[1] : !llvm.struct<(f32, f32)>
      %12 = llvm.fadd %8, %10  {fastmathFlags = #llvm.fastmath<contract>} : f32
      %13 = llvm.fadd %9, %11  {fastmathFlags = #llvm.fastmath<contract>} : f32
      %14 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
      %15 = llvm.insertvalue %12, %14[0] : !llvm.struct<(f32, f32)>
      %16 = llvm.insertvalue %13, %15[1] : !llvm.struct<(f32, f32)>
      omp.yield(%16 : !llvm.struct<(f32, f32)>)
    }
   llvm.return
}

// -----




llvm.func @_QPomp_atomic_capture_complex() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x !llvm.struct<(f32, f32)> {bindc_name = "ib"} : (i64) -> !llvm.ptr
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x !llvm.struct<(f32, f32)> {bindc_name = "ia"} : (i64) -> !llvm.ptr
    %4 = llvm.mlir.constant(1.000000e+00 : f32) : f32
    %5 = llvm.mlir.constant(2.000000e+00 : f32) : f32
    %6 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
    %7 = llvm.insertvalue %5, %6[0] : !llvm.struct<(f32, f32)>
    %8 = llvm.insertvalue %5, %7[1] : !llvm.struct<(f32, f32)>
    llvm.store %8, %3 : !llvm.struct<(f32, f32)>, !llvm.ptr
    %9 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
    %10 = llvm.insertvalue %4, %9[0] : !llvm.struct<(f32, f32)>
    %11 = llvm.insertvalue %4, %10[1] : !llvm.struct<(f32, f32)>
    omp.atomic.capture {
      omp.atomic.update %3 : !llvm.ptr {
      ^bb0(%arg0: !llvm.struct<(f32, f32)>):
        %12 = llvm.extractvalue %arg0[0] : !llvm.struct<(f32, f32)>
        %13 = llvm.extractvalue %arg0[1] : !llvm.struct<(f32, f32)>
        %14 = llvm.extractvalue %11[0] : !llvm.struct<(f32, f32)>
        %15 = llvm.extractvalue %11[1] : !llvm.struct<(f32, f32)>
        %16 = llvm.fadd %12, %14  {fastmathFlags = #llvm.fastmath<contract>} : f32
        %17 = llvm.fadd %13, %15  {fastmathFlags = #llvm.fastmath<contract>} : f32
        %18 = llvm.mlir.undef : !llvm.struct<(f32, f32)>
        %19 = llvm.insertvalue %16, %18[0] : !llvm.struct<(f32, f32)>
        %20 = llvm.insertvalue %17, %19[1] : !llvm.struct<(f32, f32)>
        omp.yield(%20 : !llvm.struct<(f32, f32)>)
      }
      omp.atomic.read %1 = %3 : !llvm.ptr, !llvm.ptr, !llvm.struct<(f32, f32)>
    }
    llvm.return
}

// -----

llvm.func @omp_atomic_read_complex(){


    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x !llvm.struct<(f32, f32)> {bindc_name = "ib"} : (i64) -> !llvm.ptr
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x !llvm.struct<(f32, f32)> {bindc_name = "ia"} : (i64) -> !llvm.ptr
    omp.atomic.read %1 = %3 : !llvm.ptr, !llvm.ptr, !llvm.struct<(f32, f32)>
    llvm.return
}

// -----

// Checking an order-dependent operation when the order is `expr binop x`
llvm.func @omp_atomic_update_ordering(%x:!llvm.ptr, %expr: i32) {
  omp.atomic.update %x : !llvm.ptr {
  ^bb0(%xval: i32):
    %newval = llvm.shl %expr, %xval : i32
    omp.yield(%newval : i32)
  }
  llvm.return
}

// -----

// Checking an order-dependent operation when the order is `x binop expr`
llvm.func @omp_atomic_update_ordering(%x:!llvm.ptr, %expr: i32) {
  omp.atomic.update %x : !llvm.ptr {
  ^bb0(%xval: i32):
    %newval = llvm.shl %xval, %expr : i32
    omp.yield(%newval : i32)
  }
  llvm.return
}

// -----

// Checking intrinsic translation.
llvm.func @omp_atomic_update_intrinsic(%x:!llvm.ptr, %expr: i32) {
  omp.atomic.update %x : !llvm.ptr {
  ^bb0(%xval: i32):
    %newval = "llvm.intr.smax"(%xval, %expr) : (i32, i32) -> i32
    omp.yield(%newval : i32)
  }
  omp.atomic.update %x : !llvm.ptr {
  ^bb0(%xval: i32):
    %newval = "llvm.intr.umax"(%xval, %expr) : (i32, i32) -> i32
    omp.yield(%newval : i32)
  }
  llvm.return
}

// -----


llvm.func @atomic_update_cmpxchg(%arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  %0 = llvm.load %arg1 : !llvm.ptr -> f32
  omp.atomic.update %arg0 : !llvm.ptr {
  ^bb0(%arg2: i32):
    %1 = llvm.sitofp %arg2 : i32 to f32
    %2 = llvm.fadd %1, %0 : f32
    %3 = llvm.fptosi %2 : f32 to i32
    omp.yield(%3 : i32)
  }
  llvm.return
}

// -----

llvm.func @omp_atomic_capture_prefix_update(
  %x: !llvm.ptr, %v: !llvm.ptr, %expr: i32,
  %xf: !llvm.ptr, %vf: !llvm.ptr, %exprf: f32) -> () {
  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.sub %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.and %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.or %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.xor %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.mul %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.sdiv %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.udiv %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.shl %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.lshr %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.ashr %xval, %expr : i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.smax"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.smin"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.umax"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.umin"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
  }

  omp.atomic.capture {
    omp.atomic.update %xf : !llvm.ptr {
    ^bb0(%xval: f32):
      %newval = llvm.fadd %xval, %exprf : f32
      omp.yield(%newval : f32)
    }
    omp.atomic.read %vf = %xf : !llvm.ptr, !llvm.ptr, f32
  }

  omp.atomic.capture {
    omp.atomic.update %xf : !llvm.ptr {
    ^bb0(%xval: f32):
      %newval = llvm.fsub %xval, %exprf : f32
      omp.yield(%newval : f32)
    }
    omp.atomic.read %vf = %xf : !llvm.ptr, !llvm.ptr, f32
  }

  llvm.return
}

// -----

llvm.func @omp_atomic_capture_postfix_update(
  %x: !llvm.ptr, %v: !llvm.ptr, %expr: i32,
  %xf: !llvm.ptr, %vf: !llvm.ptr, %exprf: f32) -> () {
  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.sub %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.and %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.or %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.xor %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.mul %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.sdiv %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.udiv %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.shl %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.lshr %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.ashr %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.smax"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.smin"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.umax"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = "llvm.intr.umin"(%xval, %expr) : (i32, i32) -> i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %vf = %xf : !llvm.ptr, !llvm.ptr, f32
    omp.atomic.update %xf : !llvm.ptr {
    ^bb0(%xval: f32):
      %newval = llvm.fadd %xval, %exprf : f32
      omp.yield(%newval : f32)
    }
  }

  omp.atomic.capture {
    omp.atomic.read %vf = %xf : !llvm.ptr, !llvm.ptr, f32
    omp.atomic.update %xf : !llvm.ptr {
    ^bb0(%xval: f32):
      %newval = llvm.fsub %xval, %exprf : f32
      omp.yield(%newval : f32)
    }
  }

  llvm.return
}

// -----
llvm.func @omp_atomic_capture_misc(
  %x: !llvm.ptr, %v: !llvm.ptr, %expr: i32,
  %xf: !llvm.ptr, %vf: !llvm.ptr, %exprf: f32) -> () {
  omp.atomic.capture{
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.write %x = %expr : !llvm.ptr, i32
  }

  omp.atomic.capture{
    omp.atomic.read %vf = %xf : !llvm.ptr, !llvm.ptr, f32
    omp.atomic.write %xf = %exprf : !llvm.ptr, f32
  }

  omp.atomic.capture memory_order(seq_cst) {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture memory_order(acquire) {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture memory_order(release) {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture memory_order(relaxed) {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  omp.atomic.capture memory_order(acq_rel) {
    omp.atomic.read %v = %x : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %x : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }

  llvm.return
}

// -----

llvm.func @omp_sections_empty() -> () {
  omp.sections {
    omp.terminator
  }
  llvm.return
}

// -----

// Check IR generation for simple empty sections. This only checks the overall
// shape of the IR, detailed checking is done by the OpenMPIRBuilder.

llvm.func @omp_sections_trivial() -> () {





  omp.sections {
    omp.section {
      omp.terminator
    }
    omp.section {
      omp.terminator
    }
    omp.terminator
  }



  llvm.return
}

// -----

llvm.func @foo()

llvm.func @bar(%arg0 : i32)

llvm.func @omp_sections(%arg0 : i32, %arg1 : i32, %arg2 : !llvm.ptr) -> () {

  omp.sections {
    omp.section {
      llvm.call @foo() : () -> ()
      omp.terminator
    }
    omp.section {
      llvm.call @bar(%arg0) : (i32) -> ()
      omp.terminator
    }
    omp.section {
      %add = llvm.add %arg0, %arg1 : i32
      llvm.store %add, %arg2 : i32, !llvm.ptr
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @omp_sections_runtime_flags() -> () {
  omp.sections {
    omp.section {
      omp.terminator
    }
    omp.section {
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @foo()

llvm.func @omp_sections_with_clauses() -> () {
  omp.sections nowait {
    omp.section {
      llvm.call @foo() : () -> ()
      omp.terminator
    }
    omp.section {
      llvm.call @foo() : () -> ()
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// -----

// Check that translation doesn't crash in presence of repeated successor
// blocks with different arguments within OpenMP operations: LLVM cannot
// represent this and a dummy block will be introduced for forwarding. The
// introduction mechanism itself is tested elsewhere.
llvm.func @repeated_successor(%arg0: i64, %arg1: i64, %arg2: i64, %arg3: i1) {
  omp.wsloop {
    omp.loop_nest (%arg4) : i64 = (%arg0) to (%arg1) step (%arg2)  {
      llvm.cond_br %arg3, ^bb1(%arg0 : i64), ^bb1(%arg1 : i64)
    ^bb1(%0: i64):  // 2 preds: ^bb0, ^bb0
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @single(%x: i32, %y: i32, %zaddr: !llvm.ptr) {
  %a = llvm.sub %x, %y : i32
  llvm.store %a, %zaddr : i32, !llvm.ptr
  omp.single {
    %z = llvm.add %x, %y : i32
    llvm.store %z, %zaddr : i32, !llvm.ptr
    omp.terminator
  }
  %b = llvm.mul %x, %y : i32
  llvm.store %b, %zaddr : i32, !llvm.ptr
  llvm.return
}

// -----

llvm.func @single_nowait(%x: i32, %y: i32, %zaddr: !llvm.ptr) {
  %a = llvm.sub %x, %y : i32
  llvm.store %a, %zaddr : i32, !llvm.ptr
  omp.single nowait {
    %z = llvm.add %x, %y : i32
    llvm.store %z, %zaddr : i32, !llvm.ptr
    omp.terminator
  }
  %t = llvm.mul %x, %y : i32
  llvm.store %t, %zaddr : i32, !llvm.ptr
  llvm.return
}

// -----

llvm.func @copy_i32(!llvm.ptr, !llvm.ptr)
llvm.func @copy_f32(!llvm.ptr, !llvm.ptr)

llvm.func @single_copyprivate(%ip: !llvm.ptr, %fp: !llvm.ptr) {
  omp.single copyprivate(%ip -> @copy_i32 : !llvm.ptr, %fp -> @copy_f32 : !llvm.ptr) {
    %i = llvm.load %ip : !llvm.ptr -> i32
    %i2 = llvm.add %i, %i : i32
    llvm.store %i2, %ip : i32, !llvm.ptr
    %f = llvm.load %fp : !llvm.ptr -> f32
    %f2 = llvm.fadd %f, %f : f32
    llvm.store %f2, %fp : f32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

// -----


llvm.func @omp_threadprivate() {


  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.constant(2 : i32) : i32
  %2 = llvm.mlir.constant(3 : i32) : i32

  %3 = llvm.mlir.addressof @_QFsubEx : !llvm.ptr
  %4 = omp.threadprivate %3 : !llvm.ptr -> !llvm.ptr

  llvm.store %0, %4 : i32, !llvm.ptr

  omp.parallel  {
    %5 = omp.threadprivate %3 : !llvm.ptr -> !llvm.ptr
    llvm.store %1, %5 : i32, !llvm.ptr
    omp.terminator
  }

  llvm.store %2, %4 : i32, !llvm.ptr
  llvm.return
}

llvm.mlir.global internal @_QFsubEx() : i32

// -----

llvm.func @omp_task_detach(%event_handle : !llvm.ptr){
   omp.task detach(%event_handle : !llvm.ptr){
     omp.terminator
   }
   llvm.return
}

// -----

llvm.func @omp_task(%x: i32, %y: i32, %zaddr: !llvm.ptr) {
  omp.task {
    %n = llvm.mlir.constant(1 : i64) : i64
    %valaddr = llvm.alloca %n x i32 : (i64) -> !llvm.ptr
    %val = llvm.load %valaddr : !llvm.ptr -> i32
    %double = llvm.add %val, %val : i32
    llvm.store %double, %valaddr : i32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}


// -----

llvm.func @omp_task_attrs() -> () attributes {
  target_cpu = "x86-64",
  target_features = #llvm.target_features<["+mmx", "+sse"]>
} {
  omp.task {
    omp.terminator
  }

  llvm.return
}


// -----




// -----
// dependence_type: Out
//         [...]
// -----
// dependence_type: Inout
//         [...]
// -----
// dependence_type: Mutexinoutset
//         [...]
// -----
// dependence_type: Inoutset
//         [...]
llvm.func @omp_task_with_deps(%zaddr: !llvm.ptr) {
  omp.task depend(taskdependin -> %zaddr : !llvm.ptr) {
    %n = llvm.mlir.constant(1 : i64) : i64
    %valaddr = llvm.alloca %n x i32 : (i64) -> !llvm.ptr
    %val = llvm.load %valaddr : !llvm.ptr -> i32
    %double = llvm.add %val, %val : i32
    llvm.store %double, %valaddr : i32, !llvm.ptr
    omp.terminator
  }
  omp.task depend(taskdependout -> %zaddr : !llvm.ptr) {
    omp.terminator
  }
  omp.task depend(taskdependinout -> %zaddr : !llvm.ptr) {
    omp.terminator
  }
  omp.task depend(taskdependmutexinoutset -> %zaddr : !llvm.ptr) {
    omp.terminator
  }
  omp.task depend(taskdependinoutset -> %zaddr : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}


llvm.func @omp_task_with_size(%dep: !llvm.ptr, %len: i32) {
  omp.task depend(taskdependin -> %dep : !llvm.ptr size(%len : i32)) {
    omp.terminator
  } {cir.omp.depend_sizes_owned}
  llvm.return
}

// -----

module attributes {llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.func @omp_task(%x: i32, %y: i32, %zaddr: !llvm.ptr) {
    %diff = llvm.sub %x, %y : i32
    llvm.store %diff, %zaddr : i32, !llvm.ptr
    omp.task {
      %z = llvm.add %x, %y : i32
      llvm.store %z, %zaddr : i32, !llvm.ptr
      omp.terminator
    }
    %b = llvm.mul %x, %y : i32
    llvm.store %b, %zaddr : i32, !llvm.ptr
    llvm.return
  }
}


// -----

llvm.func @par_task_(%arg0: !llvm.ptr {fir.bindc_name = "a"}) {
  %0 = llvm.mlir.constant(1 : i32) : i32
  omp.task   {
    omp.parallel   {
      llvm.store %0, %arg0 : i32, !llvm.ptr
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// CHEKC: %[[NEW_STRUCTARG:.*]] = alloca { ptr }, align 8
// -----

llvm.func @foo(!llvm.ptr) -> ()
llvm.func @destroy(!llvm.ptr) -> ()

omp.private {type = firstprivate} @privatizer : i32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %0, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
} dealloc {
^bb0(%arg0 : !llvm.ptr):
  llvm.call @destroy(%arg0) : (!llvm.ptr) -> ()
  omp.yield
}

llvm.func @task(%arg0 : !llvm.ptr) {
  omp.task private(@privatizer %arg0 -> %arg1 : !llvm.ptr) {
    llvm.call @foo(%arg1) : (!llvm.ptr) -> ()
    omp.terminator
  }
  llvm.return
}
//                ...
//                ...

// -----

llvm.func @foo() -> ()

llvm.func @omp_taskgroup(%x: i32, %y: i32, %zaddr: !llvm.ptr) {
  omp.taskgroup {
    llvm.call @foo() : () -> ()
    omp.terminator
  }
  llvm.return
}


// -----

llvm.func @foo() -> ()
llvm.func @bar(i32, i32, !llvm.ptr) -> ()

llvm.func @omp_taskgroup_task(%x: i32, %y: i32, %zaddr: !llvm.ptr) {
  omp.taskgroup {
    %c1 = llvm.mlir.constant(1) : i32
    %ptr1 = llvm.alloca %c1 x i8 : (i32) -> !llvm.ptr
    omp.task {
      llvm.call @foo() : () -> ()
      omp.terminator
    }
    omp.task {
      llvm.call @bar(%x, %y, %zaddr) : (i32, i32, !llvm.ptr) -> ()
      omp.terminator
    }
    llvm.br ^bb1
  ^bb1:
    llvm.call @foo() : () -> ()
    omp.terminator
  }
  llvm.return
}




// -----

llvm.func @test_01() attributes {sym_visibility = "private"}
llvm.func @test_02() attributes {sym_visibility = "private"}
llvm.func @_QPomp_task_priority() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {bindc_name = "x"} : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(4 : i32) : i32
  %3 = llvm.mlir.constant(true) : i1
  %4 = llvm.load %1 : !llvm.ptr -> i32
  omp.task priority(%4 : i32) {
    llvm.call @test_01() : () -> ()
    omp.terminator
  }
  omp.task final(%3) priority(%2 : i32) {
    llvm.call @test_02() : () -> ()
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @omp_opaque_pointers(%arg0 : !llvm.ptr, %arg1: !llvm.ptr, %expr: i32) -> () {
  omp.atomic.read %arg1 = %arg0 : !llvm.ptr, !llvm.ptr, i32

  omp.atomic.capture memory_order(acq_rel) {
    omp.atomic.read %arg0 = %arg1 : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.update %arg1 : !llvm.ptr {
    ^bb0(%xval: i32):
      %newval = llvm.add %xval, %expr : i32
      omp.yield(%newval : i32)
    }
  }
  llvm.return
}

// -----

module attributes {omp.flags = #omp.flags<debug_kind = 1, assume_teams_oversubscription = true, 
                                          assume_threads_oversubscription = true, assume_no_thread_state = true, 
                                          assume_no_nested_parallelism = true>} {}
// -----

module attributes {omp.flags = #omp.flags<>} {}

// -----

module attributes {omp.flags = #omp.flags<openmp_device_version = 51>} {}

// -----

module attributes {omp.version = #omp.version<version = 50>, omp.flags = #omp.flags<>} {}

// -----

module attributes {omp.version = #omp.version<version = 51>} {}

// -----
module attributes {omp.flags = #omp.flags<debug_kind = 0, assume_teams_oversubscription = false, 
                                          assume_threads_oversubscription = false, assume_no_thread_state = false, 
                                          assume_no_nested_parallelism = false>} {}

// -----

module attributes {omp.flags = #omp.flags<assume_teams_oversubscription = true, assume_no_thread_state = true>} {}

// -----

module attributes {omp.flags = #omp.flags<assume_teams_oversubscription = true, assume_no_thread_state = true,
                                          no_gpu_lib=true>} {}

// -----

module attributes {omp.is_target_device = false} {
  llvm.func @filter_nohost() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (nohost), capture_clause = (to)>
      } {
    llvm.return
  }

  llvm.func @filter_host() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (host), capture_clause = (to)>
      } {
    llvm.return
  }
}

// -----

module attributes {omp.is_target_device = false} {
  llvm.func @filter_nohost() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (nohost), capture_clause = (enter)>
      } {
    llvm.return
  }

  llvm.func @filter_host() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (host), capture_clause = (enter)>
      } {
    llvm.return
  }
}

// -----

module attributes {omp.is_target_device = true} {
  llvm.func @filter_nohost() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (nohost), capture_clause = (to)>
      } {
    llvm.return
  }

  llvm.func @filter_host() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (host), capture_clause = (to)>
      } {
    llvm.return
  }
}

// -----

module attributes {omp.is_target_device = true} {
  llvm.func @filter_nohost() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (nohost), capture_clause = (enter)>
      } {
    llvm.return
  }

  llvm.func @filter_host() -> ()
      attributes {
        omp.declare_target =
          #omp.declaretarget<device_type = (host), capture_clause = (enter)>
      } {
    llvm.return
  }
}

// -----

llvm.func @omp_task_untied() {
  // The third argument is 0: which signifies the untied task
  omp.task untied {
        omp.terminator
  }
  llvm.return
}

// -----

// Third argument is 5: essentially (4 || 1)
// signifying this task is TIED and MERGEABLE

llvm.func @omp_task_mergeable() {
  omp.task mergeable {
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func external @foo_before() -> ()
llvm.func external @foo() -> ()
llvm.func external @foo_after() -> ()

llvm.func @omp_task_final(%boolexpr: i1) {
  llvm.call @foo_before() : () -> ()
  omp.task final(%boolexpr) {
    llvm.call @foo() : () -> ()
    omp.terminator
  }
  llvm.call @foo_after() : () -> ()
  llvm.return
}


// -----

llvm.func external @foo_before() -> ()
llvm.func external @foo() -> ()
llvm.func external @foo_after() -> ()

llvm.func @omp_task_if(%boolexpr: i1) {
  llvm.call @foo_before() : () -> ()
  omp.task if(%boolexpr) {
    llvm.call @foo() : () -> ()
    omp.terminator
  }
  llvm.call @foo_after() : () -> ()
  llvm.return
}


// -----

module attributes {omp.requires = #omp<clause_requires reverse_offload|unified_shared_memory>} {}

// -----

llvm.func @distribute() {
  %0 = llvm.mlir.constant(42 : index) : i64
  %1 = llvm.mlir.constant(10 : index) : i64
  %2 = llvm.mlir.constant(1 : index) : i64
  omp.distribute {
    omp.loop_nest (%arg1) : i64 = (%1) to (%0) step (%2) {
      omp.yield
    }
  }
  llvm.return
}


// -----

llvm.func @distribute_wsloop(%lb : i32, %ub : i32, %step : i32) {
  omp.parallel {
    omp.distribute {
      omp.wsloop {
        omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
          omp.yield
        }
      } {omp.composite}
    } {omp.composite}
    omp.terminator
  } {omp.composite}
  llvm.return
}









// -----

omp.private {type = private} @_QFEx_private_i32 : i32
llvm.func @nested_task_with_deps() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {bindc_name = "x"} : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(1 : i64) : i64
  omp.parallel private(@_QFEx_private_i32 %1 -> %arg0 : !llvm.ptr) {
    omp.task depend(taskdependout -> %arg0 : !llvm.ptr) {
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// CHECK: @strict_msg = private unnamed_addr constant [4 x i8] c"msg\00"
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 34, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_stand_alone_directives(
// CHECK:   %[[VAL_0:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_0]])
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_omp_taskwait(ptr @2, i32 %[[VAL_1]])
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_omp_taskyield(ptr @2, i32 %[[VAL_3]], i32 0)
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_flush_construct(
// CHECK:   call void @__kmpc_flush(ptr @2)
// CHECK:   call void @__kmpc_flush(ptr @2)
// CHECK:   call void @__kmpc_flush(ptr @2)
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   call void @__kmpc_flush(ptr @2)
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_1(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_1..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_1..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_2(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_2..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_2..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   call void @body(i64 1)
// CHECK:   call void @body(i64 42)
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.par.region2:
// CHECK:   call void @body(i64 43)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_num_threads_1(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_push_num_threads(ptr @2, i32 %[[VAL_1]], i32 %[[VAL_2:.*]])
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_num_threads_1..omp_par)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_num_threads_1..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_num_threads_2(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_push_num_threads(ptr @2, i32 %[[VAL_1]], i32 4)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_num_threads_2..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_num_threads_2..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_num_threads_3(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_push_num_threads(ptr @2, i32 %[[VAL_1]], i32 4)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_num_threads_3..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_push_num_threads(ptr @2, i32 %[[VAL_4]], i32 8)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_parallel19:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_num_threads_3..omp_par.1)
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit9:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_num_threads_3..omp_par.1(
// CHECK: omp.par.entry6:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca14:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region7:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region16:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont15:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize8:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini18:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit9.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_num_threads_3..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_num_threads_strict(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_push_num_threads(ptr @2, i32 %[[VAL_1]], i32 %[[VAL_2:.*]])
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 0, ptr @test_omp_parallel_num_threads_strict..omp_par)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_num_threads_strict..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_if_1(
// CHECK:   %[[VAL_0:.*]] = icmp slt i32 %[[VAL_1:.*]], 0
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_5:.*]] = sext i1 %[[VAL_0]] to i32
// CHECK:   call void @__kmpc_fork_call_if(ptr @2, i32 0, ptr @test_omp_parallel_if_1..omp_par, i32 %[[VAL_5]], ptr null)
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_if_1..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 34, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_parallel_attrs(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_attrs..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_attrs..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 34, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_parallel_3(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_proc_bind(ptr @1, i32 %[[VAL_1]], i32 2)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_3..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_proc_bind(ptr @1, i32 %[[VAL_4]], i32 3)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_parallel35:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_3..omp_par.1)
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit9:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_proc_bind(ptr @1, i32 %[[VAL_7]], i32 4)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp_parallel36:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_3..omp_par.2)
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.exit25:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_3..omp_par.2(
// CHECK: omp.par.entry22:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca30:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region23:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region32:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont31:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize24:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini34:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit25.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_3..omp_par.1(
// CHECK: omp.par.entry6:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca14:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region7:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region16:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont15:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize8:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini18:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit9.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_3..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_4(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_4..omp_par.3)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_4..omp_par.3(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_4..omp_par)
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.exit9:
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_11]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: .fini19:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_4..omp_par(
// CHECK: omp.par.entry6:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca14:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region7:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region16:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont15:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize8:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit9.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_parallel_5(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_5..omp_par.5)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_5..omp_par.5(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_5..omp_par.4)
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.exit9:
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_11]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: .fini34:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_5..omp_par.4(
// CHECK: omp.par.entry6:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca14:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region7:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region16:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_parallel_5..omp_par)
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.exit23:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont15:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.pre_finalize8:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: .fini32:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.exit9.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_parallel_5..omp_par(
// CHECK: omp.par.entry20:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca28:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region21:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region30:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont29:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.pre_finalize22:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.exit23.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @test_omp_master(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_master..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_parallel41:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_master..omp_par.7)
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit10:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_master..omp_par.7(
// CHECK: omp.par.entry7:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca15:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region8:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region17:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp_parallel40:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @test_omp_master..omp_par.6)
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.exit24:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont16:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.pre_finalize9:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: .fini39:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.exit10.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_master..omp_par.6(
// CHECK: omp.par.entry21:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca29:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region22:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region31:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_master(ptr @1, i32 %[[VAL_7]])
// CHECK:   %[[VAL_9:.*]] = icmp ne i32 %[[VAL_8]], 0
// CHECK:   br i1 %[[VAL_9]], label %[[VAL_10:.*]], label %[[VAL_11:.*]]
// CHECK: omp_region.end33:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont30:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.pre_finalize23:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: .fini38:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp_region.body35:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.master.region37:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.region.cont36:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp_region.finalize34:
// CHECK:   call void @__kmpc_end_master(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_11]]
// CHECK: omp.par.exit24.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_master..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_master(ptr @1, i32 %[[VAL_7]])
// CHECK:   %[[VAL_9:.*]] = icmp ne i32 %[[VAL_8]], 0
// CHECK:   br i1 %[[VAL_9]], label %[[VAL_10:.*]], label %[[VAL_11:.*]]
// CHECK: omp_region.end:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp_region.body:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.master.region:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp_region.finalize:
// CHECK:   call void @__kmpc_end_master(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_11]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_masked(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @test_omp_masked..omp_par, i32 %[[VAL_3:.*]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @test_omp_masked..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_masked(ptr @1, i32 %[[VAL_7]], i32 %[[VAL_9:.*]])
// CHECK:   %[[VAL_10:.*]] = icmp ne i32 %[[VAL_8]], 0
// CHECK:   br i1 %[[VAL_10]], label %[[VAL_11:.*]], label %[[VAL_12:.*]]
// CHECK: omp_region.end:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_region.body:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.masked.region:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_region.finalize:
// CHECK:   call void @__kmpc_end_masked(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_12]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 34, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @wsloop_linear(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_9:.*]] = icmp slt i32 %[[VAL_10:.*]], 0
// CHECK:   %[[VAL_11:.*]] = sub i32 0, %[[VAL_10]]
// CHECK:   %[[VAL_12:.*]] = select i1 %[[VAL_9]], i32 %[[VAL_11]], i32 %[[VAL_10]]
// CHECK:   %[[VAL_13:.*]] = select i1 %[[VAL_9]], i32 %[[VAL_14:.*]], i32 %[[VAL_15:.*]]
// CHECK:   %[[VAL_16:.*]] = select i1 %[[VAL_9]], i32 %[[VAL_15]], i32 %[[VAL_14]]
// CHECK:   %[[VAL_17:.*]] = sub nsw i32 %[[VAL_16]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = icmp sle i32 %[[VAL_16]], %[[VAL_13]]
// CHECK:   %[[VAL_19:.*]] = sub i32 %[[VAL_17]], 1
// CHECK:   %[[VAL_20:.*]] = udiv i32 %[[VAL_19]], %[[VAL_12]]
// CHECK:   %[[VAL_21:.*]] = add i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = icmp ule i32 %[[VAL_17]], %[[VAL_12]]
// CHECK:   %[[VAL_23:.*]] = select i1 %[[VAL_22]], i32 1, i32 %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_18]], i32 0, i32 %[[VAL_23]]
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_26:.*]] = load i32, ptr %[[VAL_27:.*]], align 4
// CHECK:   store i32 %[[VAL_26]], ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_28:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_28]])
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i32 %[[VAL_24]], 1
// CHECK:   store i32 %[[VAL_29]], ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_30:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @2, i32 %[[VAL_30]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_32:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_33:.*]] = sub i32 %[[VAL_32]], %[[VAL_31]]
// CHECK:   %[[VAL_34:.*]] = add i32 %[[VAL_33]], 1
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_36:.*]] = phi i32 [ 0, %[[VAL_25]] ], [ %[[VAL_37:.*]], %[[VAL_38:.*]] ]
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_40:.*]] = icmp ult i32 %[[VAL_36]], %[[VAL_34]]
// CHECK:   br i1 %[[VAL_40]], label %[[VAL_41:.*]], label %[[VAL_42:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_36]], %[[VAL_31]]
// CHECK:   %[[VAL_44:.*]] = mul i32 %[[VAL_43]], %[[VAL_10]]
// CHECK:   %[[VAL_45:.*]] = add i32 %[[VAL_44]], %[[VAL_15]]
// CHECK:   %[[VAL_46:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_47:.*]] = mul i32 %[[VAL_43]], %[[VAL_10]]
// CHECK:   %[[VAL_48:.*]] = add i32 %[[VAL_46]], %[[VAL_47]]
// CHECK:   store i32 %[[VAL_48]], ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_38]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_37]] = add nuw i32 %[[VAL_36]], 1
// CHECK:   br label %[[VAL_35]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @2, i32 %[[VAL_30]])
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_loop.linear_finalization:
// CHECK:   %[[VAL_52:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_53:.*]] = icmp ne i32 %[[VAL_52]], 0
// CHECK:   br i1 %[[VAL_53]], label %[[VAL_54:.*]], label %[[VAL_55:.*]]
// CHECK: omp_loop.linear_lastiter_exit:
// CHECK:   %[[VAL_56:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   store i32 %[[VAL_56]], ptr %[[VAL_27]], align 4
// CHECK:   br label %[[VAL_55]]
// CHECK: omp_loop.linear_exit:
// CHECK:   %[[VAL_57:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   call void @__kmpc_barrier(ptr @1, i32 %[[VAL_57]])
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @wsloop_simple(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @wsloop_simple..omp_par, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @wsloop_simple..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_5:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_6:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_9:.*]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_4]], align 4
// CHECK:   store i64 31, ptr %[[VAL_5]], align 4
// CHECK:   store i64 1, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_17:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_17]], i32 34, ptr %[[VAL_3]], ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], i64 1, i64 1)
// CHECK:   %[[VAL_18:.*]] = load i64, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_19:.*]] = load i64, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_20:.*]] = sub i64 %[[VAL_19]], %[[VAL_18]]
// CHECK:   %[[VAL_21:.*]] = add i64 %[[VAL_20]], 1
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_23:.*]] = phi i64 [ 0, %[[VAL_16]] ], [ %[[VAL_24:.*]], %[[VAL_25:.*]] ]
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_27:.*]] = icmp ult i64 %[[VAL_23]], %[[VAL_21]]
// CHECK:   br i1 %[[VAL_27]], label %[[VAL_28:.*]], label %[[VAL_29:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_17]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_17]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_36:.*]] = add i64 %[[VAL_23]], %[[VAL_18]]
// CHECK:   %[[VAL_37:.*]] = mul i64 %[[VAL_36]], 1
// CHECK:   %[[VAL_38:.*]] = add i64 %[[VAL_37]], 10
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_40:.*]] = getelementptr float, ptr %[[VAL_2]], i64 %[[VAL_38]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_40]], align 4
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_25]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_24]] = add nuw i64 %[[VAL_23]], 1
// CHECK:   br label %[[VAL_22]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @wsloop_inclusive_1(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_1]], align 4
// CHECK:   store i64 31, ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_8]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i64 1, i64 1)
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_11:.*]] = sub i64 %[[VAL_10]], %[[VAL_9]]
// CHECK:   %[[VAL_12:.*]] = add i64 %[[VAL_11]], 1
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_14:.*]] = phi i64 [ 0, %[[VAL_7]] ], [ %[[VAL_15:.*]], %[[VAL_16:.*]] ]
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_18:.*]] = icmp ult i64 %[[VAL_14]], %[[VAL_12]]
// CHECK:   br i1 %[[VAL_18]], label %[[VAL_19:.*]], label %[[VAL_20:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_21:.*]] = add i64 %[[VAL_14]], %[[VAL_9]]
// CHECK:   %[[VAL_22:.*]] = mul i64 %[[VAL_21]], 1
// CHECK:   %[[VAL_23:.*]] = add i64 %[[VAL_22]], 10
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_25:.*]] = getelementptr float, ptr %[[VAL_26:.*]], i64 %[[VAL_23]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_25]], align 4
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_16]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_15]] = add nuw i64 %[[VAL_14]], 1
// CHECK:   br label %[[VAL_13]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_8]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_8]])
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @wsloop_inclusive_2(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_1]], align 4
// CHECK:   store i64 32, ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_8]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i64 1, i64 1)
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_11:.*]] = sub i64 %[[VAL_10]], %[[VAL_9]]
// CHECK:   %[[VAL_12:.*]] = add i64 %[[VAL_11]], 1
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_14:.*]] = phi i64 [ 0, %[[VAL_7]] ], [ %[[VAL_15:.*]], %[[VAL_16:.*]] ]
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_18:.*]] = icmp ult i64 %[[VAL_14]], %[[VAL_12]]
// CHECK:   br i1 %[[VAL_18]], label %[[VAL_19:.*]], label %[[VAL_20:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_21:.*]] = add i64 %[[VAL_14]], %[[VAL_9]]
// CHECK:   %[[VAL_22:.*]] = mul i64 %[[VAL_21]], 1
// CHECK:   %[[VAL_23:.*]] = add i64 %[[VAL_22]], 10
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_25:.*]] = getelementptr float, ptr %[[VAL_26:.*]], i64 %[[VAL_23]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_25]], align 4
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_16]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_15]] = add nuw i64 %[[VAL_14]], 1
// CHECK:   br label %[[VAL_13]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_8]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_8]])
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_static_defchunk(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i32 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i32 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_24:.*]] = sub i32 %[[VAL_22]], 1
// CHECK:   store i32 %[[VAL_24]], ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_25:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_25]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_26:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i32 %[[VAL_27]], %[[VAL_26]]
// CHECK:   %[[VAL_29:.*]] = add i32 %[[VAL_28]], 1
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_31:.*]] = phi i32 [ 0, %[[VAL_23]] ], [ %[[VAL_32:.*]], %[[VAL_33:.*]] ]
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_35:.*]] = icmp ult i32 %[[VAL_31]], %[[VAL_29]]
// CHECK:   br i1 %[[VAL_35]], label %[[VAL_36:.*]], label %[[VAL_37:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_38:.*]] = add i32 %[[VAL_31]], %[[VAL_26]]
// CHECK:   %[[VAL_39:.*]] = mul i32 %[[VAL_38]], %[[VAL_8]]
// CHECK:   %[[VAL_40:.*]] = add i32 %[[VAL_39]], %[[VAL_13]]
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_40]])
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_32]] = add nuw i32 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_30]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_25]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_25]])
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_static_1(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i32 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i32 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_24:.*]] = sub i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_25:.*]] = icmp eq i32 %[[VAL_22]], 0
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 0, i32 %[[VAL_24]]
// CHECK:   store i32 %[[VAL_26]], ptr %[[VAL_3]], align 4
// CHECK:   store i32 1, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_27:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_27]], i32 33, ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], ptr %[[VAL_4]], i32 1, i32 1)
// CHECK:   %[[VAL_28:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_29:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_30:.*]] = add i32 %[[VAL_29]], 1
// CHECK:   %[[VAL_31:.*]] = sub i32 %[[VAL_30]], %[[VAL_28]]
// CHECK:   %[[VAL_32:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_33:.*]] = sub nuw i32 %[[VAL_22]], %[[VAL_28]]
// CHECK:   %[[VAL_34:.*]] = icmp ule i32 %[[VAL_22]], %[[VAL_28]]
// CHECK:   %[[VAL_35:.*]] = sub i32 %[[VAL_33]], 1
// CHECK:   %[[VAL_36:.*]] = udiv i32 %[[VAL_35]], %[[VAL_32]]
// CHECK:   %[[VAL_37:.*]] = add i32 %[[VAL_36]], 1
// CHECK:   %[[VAL_38:.*]] = icmp ule i32 %[[VAL_33]], %[[VAL_32]]
// CHECK:   %[[VAL_39:.*]] = select i1 %[[VAL_38]], i32 1, i32 %[[VAL_37]]
// CHECK:   %[[VAL_40:.*]] = select i1 %[[VAL_34]], i32 0, i32 %[[VAL_39]]
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_43:.*]] = phi i32 [ 0, %[[VAL_41]] ], [ %[[VAL_44:.*]], %[[VAL_45:.*]] ]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_47:.*]] = icmp ult i32 %[[VAL_43]], %[[VAL_40]]
// CHECK:   br i1 %[[VAL_47]], label %[[VAL_48:.*]], label %[[VAL_49:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_50:.*]] = mul i32 %[[VAL_43]], %[[VAL_32]]
// CHECK:   %[[VAL_51:.*]] = add i32 %[[VAL_50]], %[[VAL_28]]
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_44]] = add nuw i32 %[[VAL_43]], 1
// CHECK:   br label %[[VAL_42]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_27]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_27]])
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp_loop.preheader2:
// CHECK:   %[[VAL_55:.*]] = add i32 %[[VAL_51]], %[[VAL_31]]
// CHECK:   %[[VAL_56:.*]] = icmp uge i32 %[[VAL_55]], %[[VAL_22]]
// CHECK:   %[[VAL_57:.*]] = sub i32 %[[VAL_22]], %[[VAL_51]]
// CHECK:   %[[VAL_58:.*]] = select i1 %[[VAL_56]], i32 %[[VAL_57]], i32 %[[VAL_31]]
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_60:.*]] = phi i32 [ 0, %[[VAL_52]] ], [ %[[VAL_61:.*]], %[[VAL_62:.*]] ]
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_64:.*]] = icmp ult i32 %[[VAL_60]], %[[VAL_58]]
// CHECK:   br i1 %[[VAL_64]], label %[[VAL_65:.*]], label %[[VAL_66:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_67:.*]] = add i32 %[[VAL_60]], %[[VAL_51]]
// CHECK:   %[[VAL_68:.*]] = mul i32 %[[VAL_67]], %[[VAL_8]]
// CHECK:   %[[VAL_69:.*]] = add i32 %[[VAL_68]], %[[VAL_13]]
// CHECK:   br label %[[VAL_70:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_69]]), !llvm.access.group !1
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_62]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_61]] = add nuw i32 %[[VAL_60]], 1
// CHECK:   br label %[[VAL_59]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_45]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_72:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_static_2(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i32 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i32 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_24:.*]] = sub i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_25:.*]] = icmp eq i32 %[[VAL_22]], 0
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 0, i32 %[[VAL_24]]
// CHECK:   store i32 %[[VAL_26]], ptr %[[VAL_3]], align 4
// CHECK:   store i32 1, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_27:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_27]], i32 33, ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], ptr %[[VAL_4]], i32 1, i32 2)
// CHECK:   %[[VAL_28:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_29:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_30:.*]] = add i32 %[[VAL_29]], 1
// CHECK:   %[[VAL_31:.*]] = sub i32 %[[VAL_30]], %[[VAL_28]]
// CHECK:   %[[VAL_32:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_33:.*]] = sub nuw i32 %[[VAL_22]], %[[VAL_28]]
// CHECK:   %[[VAL_34:.*]] = icmp ule i32 %[[VAL_22]], %[[VAL_28]]
// CHECK:   %[[VAL_35:.*]] = sub i32 %[[VAL_33]], 1
// CHECK:   %[[VAL_36:.*]] = udiv i32 %[[VAL_35]], %[[VAL_32]]
// CHECK:   %[[VAL_37:.*]] = add i32 %[[VAL_36]], 1
// CHECK:   %[[VAL_38:.*]] = icmp ule i32 %[[VAL_33]], %[[VAL_32]]
// CHECK:   %[[VAL_39:.*]] = select i1 %[[VAL_38]], i32 1, i32 %[[VAL_37]]
// CHECK:   %[[VAL_40:.*]] = select i1 %[[VAL_34]], i32 0, i32 %[[VAL_39]]
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_43:.*]] = phi i32 [ 0, %[[VAL_41]] ], [ %[[VAL_44:.*]], %[[VAL_45:.*]] ]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_47:.*]] = icmp ult i32 %[[VAL_43]], %[[VAL_40]]
// CHECK:   br i1 %[[VAL_47]], label %[[VAL_48:.*]], label %[[VAL_49:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_50:.*]] = mul i32 %[[VAL_43]], %[[VAL_32]]
// CHECK:   %[[VAL_51:.*]] = add i32 %[[VAL_50]], %[[VAL_28]]
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_44]] = add nuw i32 %[[VAL_43]], 1
// CHECK:   br label %[[VAL_42]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_27]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_27]])
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp_loop.preheader2:
// CHECK:   %[[VAL_55:.*]] = add i32 %[[VAL_51]], %[[VAL_31]]
// CHECK:   %[[VAL_56:.*]] = icmp uge i32 %[[VAL_55]], %[[VAL_22]]
// CHECK:   %[[VAL_57:.*]] = sub i32 %[[VAL_22]], %[[VAL_51]]
// CHECK:   %[[VAL_58:.*]] = select i1 %[[VAL_56]], i32 %[[VAL_57]], i32 %[[VAL_31]]
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_60:.*]] = phi i32 [ 0, %[[VAL_52]] ], [ %[[VAL_61:.*]], %[[VAL_62:.*]] ]
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_64:.*]] = icmp ult i32 %[[VAL_60]], %[[VAL_58]]
// CHECK:   br i1 %[[VAL_64]], label %[[VAL_65:.*]], label %[[VAL_66:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_67:.*]] = add i32 %[[VAL_60]], %[[VAL_51]]
// CHECK:   %[[VAL_68:.*]] = mul i32 %[[VAL_67]], %[[VAL_8]]
// CHECK:   %[[VAL_69:.*]] = add i32 %[[VAL_68]], %[[VAL_13]]
// CHECK:   br label %[[VAL_70:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_69]]), !llvm.access.group !1
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_62]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_61]] = add nuw i32 %[[VAL_60]], 1
// CHECK:   br label %[[VAL_59]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_45]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_72:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741859, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_chunk_const(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741859, i64 1, i64 %[[VAL_22]], i64 1, i64 2)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_chunk_var(
// CHECK:   %[[VAL_0:.*]] = alloca i16, i64 1, align 2
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = load i16, ptr %[[VAL_0]], align 2
// CHECK:   %[[VAL_6:.*]] = sext i16 %[[VAL_5]] to i32
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_10:.*]] = icmp slt i32 %[[VAL_11:.*]], 0
// CHECK:   %[[VAL_12:.*]] = sub i32 0, %[[VAL_11]]
// CHECK:   %[[VAL_13:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_12]], i32 %[[VAL_11]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_15:.*]], i32 %[[VAL_16:.*]]
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_16]], i32 %[[VAL_15]]
// CHECK:   %[[VAL_18:.*]] = sub nsw i32 %[[VAL_17]], %[[VAL_14]]
// CHECK:   %[[VAL_19:.*]] = icmp sle i32 %[[VAL_17]], %[[VAL_14]]
// CHECK:   %[[VAL_20:.*]] = sub i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_21:.*]] = udiv i32 %[[VAL_20]], %[[VAL_13]]
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 1
// CHECK:   %[[VAL_23:.*]] = icmp ule i32 %[[VAL_18]], %[[VAL_13]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_23]], i32 1, i32 %[[VAL_22]]
// CHECK:   %[[VAL_25:.*]] = select i1 %[[VAL_19]], i32 0, i32 %[[VAL_24]]
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_25]], ptr %[[VAL_3]], align 4
// CHECK:   store i32 1, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_27:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_4u(ptr @1, i32 %[[VAL_27]], i32 1073741859, i32 1, i32 %[[VAL_25]], i32 1, i32 %[[VAL_6]])
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ %[[VAL_30:.*]], %[[VAL_28]] ], [ %[[VAL_31:.*]], %[[VAL_32:.*]] ]
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_35:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_34]]
// CHECK:   br i1 %[[VAL_35]], label %[[VAL_36:.*]], label %[[VAL_28]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_37:.*]] = mul i32 %[[VAL_29]], %[[VAL_11]]
// CHECK:   %[[VAL_38:.*]] = add i32 %[[VAL_37]], %[[VAL_16]]
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_38]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_32]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_31]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_27]])
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_42]])
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_45:.*]] = call i32 @__kmpc_dispatch_next_4u(ptr @1, i32 %[[VAL_27]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], ptr %[[VAL_4]])
// CHECK:   %[[VAL_46:.*]] = icmp ne i32 %[[VAL_45]], 0
// CHECK:   %[[VAL_47:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_30]] = sub i32 %[[VAL_47]], 1
// CHECK:   br i1 %[[VAL_46]], label %[[VAL_41]], label %[[VAL_48:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_chunk_var2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, i64 1, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_6:.*]] = trunc i64 %[[VAL_5]] to i32
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_10:.*]] = icmp slt i32 %[[VAL_11:.*]], 0
// CHECK:   %[[VAL_12:.*]] = sub i32 0, %[[VAL_11]]
// CHECK:   %[[VAL_13:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_12]], i32 %[[VAL_11]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_15:.*]], i32 %[[VAL_16:.*]]
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_16]], i32 %[[VAL_15]]
// CHECK:   %[[VAL_18:.*]] = sub nsw i32 %[[VAL_17]], %[[VAL_14]]
// CHECK:   %[[VAL_19:.*]] = icmp sle i32 %[[VAL_17]], %[[VAL_14]]
// CHECK:   %[[VAL_20:.*]] = sub i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_21:.*]] = udiv i32 %[[VAL_20]], %[[VAL_13]]
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 1
// CHECK:   %[[VAL_23:.*]] = icmp ule i32 %[[VAL_18]], %[[VAL_13]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_23]], i32 1, i32 %[[VAL_22]]
// CHECK:   %[[VAL_25:.*]] = select i1 %[[VAL_19]], i32 0, i32 %[[VAL_24]]
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_25]], ptr %[[VAL_3]], align 4
// CHECK:   store i32 1, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_27:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_4u(ptr @1, i32 %[[VAL_27]], i32 1073741859, i32 1, i32 %[[VAL_25]], i32 1, i32 %[[VAL_6]])
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ %[[VAL_30:.*]], %[[VAL_28]] ], [ %[[VAL_31:.*]], %[[VAL_32:.*]] ]
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_35:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_34]]
// CHECK:   br i1 %[[VAL_35]], label %[[VAL_36:.*]], label %[[VAL_28]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_37:.*]] = mul i32 %[[VAL_29]], %[[VAL_11]]
// CHECK:   %[[VAL_38:.*]] = add i32 %[[VAL_37]], %[[VAL_16]]
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_38]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_32]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_31]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_27]])
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_42]])
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_45:.*]] = call i32 @__kmpc_dispatch_next_4u(ptr @1, i32 %[[VAL_27]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], ptr %[[VAL_4]])
// CHECK:   %[[VAL_46:.*]] = icmp ne i32 %[[VAL_45]], 0
// CHECK:   %[[VAL_47:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_30]] = sub i32 %[[VAL_47]], 1
// CHECK:   br i1 %[[VAL_46]], label %[[VAL_41]], label %[[VAL_48:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_chunk_var3(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i32 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i32 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 1, ptr %[[VAL_1]], align 4
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_4u(ptr @1, i32 %[[VAL_24]], i32 1073741859, i32 1, i32 %[[VAL_22]], i32 1, i32 %[[VAL_25:.*]])
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_27:.*]] = phi i32 [ %[[VAL_28:.*]], %[[VAL_26]] ], [ %[[VAL_29:.*]], %[[VAL_30:.*]] ]
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_32:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_27]], %[[VAL_32]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_26]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_35:.*]] = mul i32 %[[VAL_27]], %[[VAL_8]]
// CHECK:   %[[VAL_36:.*]] = add i32 %[[VAL_35]], %[[VAL_13]]
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_36]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_30]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_29]] = add nuw i32 %[[VAL_27]], 1
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_40:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_40]])
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_43:.*]] = call i32 @__kmpc_dispatch_next_4u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_44:.*]] = icmp ne i32 %[[VAL_43]], 0
// CHECK:   %[[VAL_45:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_28]] = sub i32 %[[VAL_45]], 1
// CHECK:   br i1 %[[VAL_44]], label %[[VAL_39]], label %[[VAL_46:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_auto(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741862, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_runtime(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741861, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_guided(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741860, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_nonmonotonic(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741859, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_monotonic(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 536870947, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_runtime_simd(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741871, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_guided_simd(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741870, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_order(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_24:.*]] = sub i64 %[[VAL_22]], 1
// CHECK:   store i64 %[[VAL_24]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_25:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_25]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i64 1, i64 1)
// CHECK:   %[[VAL_26:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i64 %[[VAL_27]], %[[VAL_26]]
// CHECK:   %[[VAL_29:.*]] = add i64 %[[VAL_28]], 1
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_31:.*]] = phi i64 [ 0, %[[VAL_23]] ], [ %[[VAL_32:.*]], %[[VAL_33:.*]] ]
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_35:.*]] = icmp ult i64 %[[VAL_31]], %[[VAL_29]]
// CHECK:   br i1 %[[VAL_35]], label %[[VAL_36:.*]], label %[[VAL_37:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_38:.*]] = add i64 %[[VAL_31]], %[[VAL_26]]
// CHECK:   %[[VAL_39:.*]] = mul i64 %[[VAL_38]], %[[VAL_8]]
// CHECK:   %[[VAL_40:.*]] = add i64 %[[VAL_39]], %[[VAL_13]]
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_42:.*]] = getelementptr i64, ptr %[[VAL_43:.*]], i64 %[[VAL_40]]
// CHECK:   store i64 %[[VAL_40]], ptr %[[VAL_42]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_32]] = add nuw i64 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_30]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_25]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_25]])
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_simple(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_3:.*]] = icmp slt i64 %[[VAL_4:.*]], 0
// CHECK:   %[[VAL_5:.*]] = sub i64 0, %[[VAL_4]]
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_5]], i64 %[[VAL_4]]
// CHECK:   %[[VAL_7:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_8:.*]], i64 %[[VAL_9:.*]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = sub nsw i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_12:.*]] = icmp sle i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_13:.*]] = sub i64 %[[VAL_11]], 1
// CHECK:   %[[VAL_14:.*]] = udiv i64 %[[VAL_13]], %[[VAL_6]]
// CHECK:   %[[VAL_15:.*]] = add i64 %[[VAL_14]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ule i64 %[[VAL_11]], %[[VAL_6]]
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_16]], i64 1, i64 %[[VAL_15]]
// CHECK:   %[[VAL_18:.*]] = select i1 %[[VAL_12]], i64 0, i64 %[[VAL_17]]
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_21:.*]] = phi i64 [ 0, %[[VAL_19]] ], [ %[[VAL_22:.*]], %[[VAL_23:.*]] ]
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_25:.*]] = icmp ult i64 %[[VAL_21]], %[[VAL_18]]
// CHECK:   br i1 %[[VAL_25]], label %[[VAL_26:.*]], label %[[VAL_27:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_28:.*]] = mul i64 %[[VAL_21]], %[[VAL_4]]
// CHECK:   %[[VAL_29:.*]] = add i64 %[[VAL_28]], %[[VAL_9]]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_31:.*]] = getelementptr float, ptr %[[VAL_32:.*]], i64 %[[VAL_29]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_31]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i64 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_linear(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_5:.*]] = icmp slt i32 %[[VAL_6:.*]], 0
// CHECK:   %[[VAL_7:.*]] = sub i32 0, %[[VAL_6]]
// CHECK:   %[[VAL_8:.*]] = select i1 %[[VAL_5]], i32 %[[VAL_7]], i32 %[[VAL_6]]
// CHECK:   %[[VAL_9:.*]] = select i1 %[[VAL_5]], i32 %[[VAL_10:.*]], i32 %[[VAL_11:.*]]
// CHECK:   %[[VAL_12:.*]] = select i1 %[[VAL_5]], i32 %[[VAL_11]], i32 %[[VAL_10]]
// CHECK:   %[[VAL_13:.*]] = sub nsw i32 %[[VAL_12]], %[[VAL_9]]
// CHECK:   %[[VAL_14:.*]] = icmp sle i32 %[[VAL_12]], %[[VAL_9]]
// CHECK:   %[[VAL_15:.*]] = sub i32 %[[VAL_13]], 1
// CHECK:   %[[VAL_16:.*]] = udiv i32 %[[VAL_15]], %[[VAL_8]]
// CHECK:   %[[VAL_17:.*]] = add i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_18:.*]] = icmp ule i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_18]], i32 1, i32 %[[VAL_17]]
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_14]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_22:.*]] = load i32, ptr %[[VAL_23:.*]], align 4
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_25:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_26:.*]], %[[VAL_27:.*]] ]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_29:.*]] = icmp ult i32 %[[VAL_25]], %[[VAL_20]]
// CHECK:   br i1 %[[VAL_29]], label %[[VAL_30:.*]], label %[[VAL_31:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_32:.*]] = mul i32 %[[VAL_25]], %[[VAL_6]]
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], %[[VAL_11]]
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_2]], align 4, !llvm.access.group !1
// CHECK:   %[[VAL_35:.*]] = mul i32 %[[VAL_25]], %[[VAL_6]]
// CHECK:   %[[VAL_36:.*]] = add i32 %[[VAL_34]], %[[VAL_35]]
// CHECK:   store i32 %[[VAL_36]], ptr %[[VAL_3]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_27]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_26]] = add nuw i32 %[[VAL_25]], 1
// CHECK:   br label %[[VAL_24]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_41:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_41]], ptr %[[VAL_23]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_linear_i64_var_i32_step(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_5:.*]] = select i1 false, i32 %[[VAL_6:.*]], i32 %[[VAL_7:.*]]
// CHECK:   %[[VAL_8:.*]] = select i1 false, i32 %[[VAL_7]], i32 %[[VAL_6]]
// CHECK:   %[[VAL_9:.*]] = sub nsw i32 %[[VAL_8]], %[[VAL_5]]
// CHECK:   %[[VAL_10:.*]] = icmp sle i32 %[[VAL_8]], %[[VAL_5]]
// CHECK:   %[[VAL_11:.*]] = sub i32 %[[VAL_9]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_11]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = icmp ule i32 %[[VAL_9]], 1
// CHECK:   %[[VAL_15:.*]] = select i1 %[[VAL_14]], i32 1, i32 %[[VAL_13]]
// CHECK:   %[[VAL_16:.*]] = select i1 %[[VAL_10]], i32 0, i32 %[[VAL_15]]
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_18:.*]] = load i64, ptr %[[VAL_19:.*]], align 4
// CHECK:   store i64 %[[VAL_18]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_21:.*]] = phi i32 [ 0, %[[VAL_17]] ], [ %[[VAL_22:.*]], %[[VAL_23:.*]] ]
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_25:.*]] = icmp ult i32 %[[VAL_21]], %[[VAL_16]]
// CHECK:   br i1 %[[VAL_25]], label %[[VAL_26:.*]], label %[[VAL_27:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_28:.*]] = mul i32 %[[VAL_21]], 1
// CHECK:   %[[VAL_29:.*]] = add i32 %[[VAL_28]], %[[VAL_7]]
// CHECK:   %[[VAL_30:.*]] = sext i32 %[[VAL_21]] to i64
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4, !llvm.access.group !1
// CHECK:   %[[VAL_32:.*]] = mul i64 %[[VAL_30]], 1
// CHECK:   %[[VAL_33:.*]] = add i64 %[[VAL_31]], %[[VAL_32]]
// CHECK:   store i64 %[[VAL_33]], ptr %[[VAL_3]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i32 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_38:.*]] = load i64, ptr %[[VAL_3]], align 4
// CHECK:   store i64 %[[VAL_38]], ptr %[[VAL_19]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_linear_f64_var_i32_step(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = alloca double, align 8
// CHECK:   %[[VAL_3:.*]] = alloca double, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_5:.*]] = select i1 false, i32 %[[VAL_6:.*]], i32 %[[VAL_7:.*]]
// CHECK:   %[[VAL_8:.*]] = select i1 false, i32 %[[VAL_7]], i32 %[[VAL_6]]
// CHECK:   %[[VAL_9:.*]] = sub nsw i32 %[[VAL_8]], %[[VAL_5]]
// CHECK:   %[[VAL_10:.*]] = icmp sle i32 %[[VAL_8]], %[[VAL_5]]
// CHECK:   %[[VAL_11:.*]] = sub i32 %[[VAL_9]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_11]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = icmp ule i32 %[[VAL_9]], 1
// CHECK:   %[[VAL_15:.*]] = select i1 %[[VAL_14]], i32 1, i32 %[[VAL_13]]
// CHECK:   %[[VAL_16:.*]] = select i1 %[[VAL_10]], i32 0, i32 %[[VAL_15]]
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_18:.*]] = load double, ptr %[[VAL_19:.*]], align 8
// CHECK:   store double %[[VAL_18]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_21:.*]] = phi i32 [ 0, %[[VAL_17]] ], [ %[[VAL_22:.*]], %[[VAL_23:.*]] ]
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_25:.*]] = icmp ult i32 %[[VAL_21]], %[[VAL_16]]
// CHECK:   br i1 %[[VAL_25]], label %[[VAL_26:.*]], label %[[VAL_27:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_28:.*]] = mul i32 %[[VAL_21]], 1
// CHECK:   %[[VAL_29:.*]] = add i32 %[[VAL_28]], %[[VAL_7]]
// CHECK:   %[[VAL_30:.*]] = mul i32 %[[VAL_21]], 1
// CHECK:   %[[VAL_31:.*]] = load double, ptr %[[VAL_2]], align 8, !llvm.access.group !1
// CHECK:   %[[VAL_32:.*]] = sitofp i32 %[[VAL_30]] to double
// CHECK:   %[[VAL_33:.*]] = fadd double %[[VAL_31]], %[[VAL_32]]
// CHECK:   store double %[[VAL_33]], ptr %[[VAL_3]], align 8, !llvm.access.group !1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i32 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_38:.*]] = load double, ptr %[[VAL_3]], align 8
// CHECK:   store double %[[VAL_38]], ptr %[[VAL_19]], align 8
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_simple_multiple(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_3:.*]] = icmp slt i64 %[[VAL_4:.*]], 0
// CHECK:   %[[VAL_5:.*]] = sub i64 0, %[[VAL_4]]
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_5]], i64 %[[VAL_4]]
// CHECK:   %[[VAL_7:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_8:.*]], i64 %[[VAL_9:.*]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = sub nsw i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_12:.*]] = icmp slt i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_13:.*]] = udiv i64 %[[VAL_11]], %[[VAL_6]]
// CHECK:   %[[VAL_14:.*]] = add i64 %[[VAL_13]], 1
// CHECK:   %[[VAL_15:.*]] = select i1 %[[VAL_12]], i64 0, i64 %[[VAL_14]]
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_17:.*]] = icmp slt i64 %[[VAL_18:.*]], 0
// CHECK:   %[[VAL_19:.*]] = sub i64 0, %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i64 %[[VAL_19]], i64 %[[VAL_18]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_17]], i64 %[[VAL_22:.*]], i64 %[[VAL_23:.*]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_17]], i64 %[[VAL_23]], i64 %[[VAL_22]]
// CHECK:   %[[VAL_25:.*]] = sub nsw i64 %[[VAL_24]], %[[VAL_21]]
// CHECK:   %[[VAL_26:.*]] = icmp slt i64 %[[VAL_24]], %[[VAL_21]]
// CHECK:   %[[VAL_27:.*]] = udiv i64 %[[VAL_25]], %[[VAL_20]]
// CHECK:   %[[VAL_28:.*]] = add i64 %[[VAL_27]], 1
// CHECK:   %[[VAL_29:.*]] = select i1 %[[VAL_26]], i64 0, i64 %[[VAL_28]]
// CHECK:   %[[VAL_30:.*]] = mul nuw i64 %[[VAL_15]], %[[VAL_29]]
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp_collapsed.preheader:
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_collapsed.header:
// CHECK:   %[[VAL_33:.*]] = phi i64 [ 0, %[[VAL_31]] ], [ %[[VAL_34:.*]], %[[VAL_35:.*]] ]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_collapsed.cond:
// CHECK:   %[[VAL_37:.*]] = icmp ult i64 %[[VAL_33]], %[[VAL_30]]
// CHECK:   br i1 %[[VAL_37]], label %[[VAL_38:.*]], label %[[VAL_39:.*]]
// CHECK: omp_collapsed.body:
// CHECK:   %[[VAL_40:.*]] = urem i64 %[[VAL_33]], %[[VAL_29]]
// CHECK:   %[[VAL_41:.*]] = udiv i64 %[[VAL_33]], %[[VAL_29]]
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_43:.*]] = mul i64 %[[VAL_41]], %[[VAL_4]]
// CHECK:   %[[VAL_44:.*]] = add i64 %[[VAL_43]], %[[VAL_9]]
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp_loop.preheader2:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_loop.body5:
// CHECK:   %[[VAL_47:.*]] = mul i64 %[[VAL_40]], %[[VAL_18]]
// CHECK:   %[[VAL_48:.*]] = add i64 %[[VAL_47]], %[[VAL_23]]
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_50:.*]] = getelementptr float, ptr %[[VAL_51:.*]], i64 %[[VAL_44]]
// CHECK:   %[[VAL_52:.*]] = getelementptr float, ptr %[[VAL_53:.*]], i64 %[[VAL_48]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_50]], align 4, !llvm.access.group !1
// CHECK:   store float 2.000000e+00, ptr %[[VAL_52]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.region.cont12:
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_loop.after8:
// CHECK:   br label %[[VAL_35]]
// CHECK: omp_collapsed.inc:
// CHECK:   %[[VAL_34]] = add nuw i64 %[[VAL_33]], 1
// CHECK:   br label %[[VAL_32]], !llvm.loop !2
// CHECK: omp_collapsed.exit:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_collapsed.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_simple_multiple_simdlen(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_3:.*]] = icmp slt i64 %[[VAL_4:.*]], 0
// CHECK:   %[[VAL_5:.*]] = sub i64 0, %[[VAL_4]]
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_5]], i64 %[[VAL_4]]
// CHECK:   %[[VAL_7:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_8:.*]], i64 %[[VAL_9:.*]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = sub nsw i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_12:.*]] = icmp sle i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_13:.*]] = sub i64 %[[VAL_11]], 1
// CHECK:   %[[VAL_14:.*]] = udiv i64 %[[VAL_13]], %[[VAL_6]]
// CHECK:   %[[VAL_15:.*]] = add i64 %[[VAL_14]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ule i64 %[[VAL_11]], %[[VAL_6]]
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_16]], i64 1, i64 %[[VAL_15]]
// CHECK:   %[[VAL_18:.*]] = select i1 %[[VAL_12]], i64 0, i64 %[[VAL_17]]
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_20:.*]] = icmp slt i64 %[[VAL_21:.*]], 0
// CHECK:   %[[VAL_22:.*]] = sub i64 0, %[[VAL_21]]
// CHECK:   %[[VAL_23:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_22]], i64 %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_25:.*]], i64 %[[VAL_26:.*]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_26]], i64 %[[VAL_25]]
// CHECK:   %[[VAL_28:.*]] = sub nsw i64 %[[VAL_27]], %[[VAL_24]]
// CHECK:   %[[VAL_29:.*]] = icmp sle i64 %[[VAL_27]], %[[VAL_24]]
// CHECK:   %[[VAL_30:.*]] = sub i64 %[[VAL_28]], 1
// CHECK:   %[[VAL_31:.*]] = udiv i64 %[[VAL_30]], %[[VAL_23]]
// CHECK:   %[[VAL_32:.*]] = add i64 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = icmp ule i64 %[[VAL_28]], %[[VAL_23]]
// CHECK:   %[[VAL_34:.*]] = select i1 %[[VAL_33]], i64 1, i64 %[[VAL_32]]
// CHECK:   %[[VAL_35:.*]] = select i1 %[[VAL_29]], i64 0, i64 %[[VAL_34]]
// CHECK:   %[[VAL_36:.*]] = mul nuw i64 %[[VAL_18]], %[[VAL_35]]
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp_collapsed.preheader:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_collapsed.header:
// CHECK:   %[[VAL_39:.*]] = phi i64 [ 0, %[[VAL_37]] ], [ %[[VAL_40:.*]], %[[VAL_41:.*]] ]
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_collapsed.cond:
// CHECK:   %[[VAL_43:.*]] = icmp ult i64 %[[VAL_39]], %[[VAL_36]]
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_44:.*]], label %[[VAL_45:.*]]
// CHECK: omp_collapsed.body:
// CHECK:   %[[VAL_46:.*]] = urem i64 %[[VAL_39]], %[[VAL_35]]
// CHECK:   %[[VAL_47:.*]] = udiv i64 %[[VAL_39]], %[[VAL_35]]
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_49:.*]] = mul i64 %[[VAL_47]], %[[VAL_4]]
// CHECK:   %[[VAL_50:.*]] = add i64 %[[VAL_49]], %[[VAL_9]]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_loop.preheader2:
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp_loop.body5:
// CHECK:   %[[VAL_53:.*]] = mul i64 %[[VAL_46]], %[[VAL_21]]
// CHECK:   %[[VAL_54:.*]] = add i64 %[[VAL_53]], %[[VAL_26]]
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_56:.*]] = getelementptr float, ptr %[[VAL_57:.*]], i64 %[[VAL_50]]
// CHECK:   %[[VAL_58:.*]] = getelementptr float, ptr %[[VAL_59:.*]], i64 %[[VAL_54]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_56]], align 4, !llvm.access.group !1
// CHECK:   store float 2.000000e+00, ptr %[[VAL_58]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont12:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp_loop.after8:
// CHECK:   br label %[[VAL_41]]
// CHECK: omp_collapsed.inc:
// CHECK:   %[[VAL_40]] = add nuw i64 %[[VAL_39]], 1
// CHECK:   br label %[[VAL_38]], !llvm.loop !2
// CHECK: omp_collapsed.exit:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: omp_collapsed.after:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_64:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_simple_multiple_safelen(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_3:.*]] = icmp slt i64 %[[VAL_4:.*]], 0
// CHECK:   %[[VAL_5:.*]] = sub i64 0, %[[VAL_4]]
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_5]], i64 %[[VAL_4]]
// CHECK:   %[[VAL_7:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_8:.*]], i64 %[[VAL_9:.*]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = sub nsw i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_12:.*]] = icmp sle i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_13:.*]] = sub i64 %[[VAL_11]], 1
// CHECK:   %[[VAL_14:.*]] = udiv i64 %[[VAL_13]], %[[VAL_6]]
// CHECK:   %[[VAL_15:.*]] = add i64 %[[VAL_14]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ule i64 %[[VAL_11]], %[[VAL_6]]
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_16]], i64 1, i64 %[[VAL_15]]
// CHECK:   %[[VAL_18:.*]] = select i1 %[[VAL_12]], i64 0, i64 %[[VAL_17]]
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_20:.*]] = icmp slt i64 %[[VAL_21:.*]], 0
// CHECK:   %[[VAL_22:.*]] = sub i64 0, %[[VAL_21]]
// CHECK:   %[[VAL_23:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_22]], i64 %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_25:.*]], i64 %[[VAL_26:.*]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_26]], i64 %[[VAL_25]]
// CHECK:   %[[VAL_28:.*]] = sub nsw i64 %[[VAL_27]], %[[VAL_24]]
// CHECK:   %[[VAL_29:.*]] = icmp sle i64 %[[VAL_27]], %[[VAL_24]]
// CHECK:   %[[VAL_30:.*]] = sub i64 %[[VAL_28]], 1
// CHECK:   %[[VAL_31:.*]] = udiv i64 %[[VAL_30]], %[[VAL_23]]
// CHECK:   %[[VAL_32:.*]] = add i64 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = icmp ule i64 %[[VAL_28]], %[[VAL_23]]
// CHECK:   %[[VAL_34:.*]] = select i1 %[[VAL_33]], i64 1, i64 %[[VAL_32]]
// CHECK:   %[[VAL_35:.*]] = select i1 %[[VAL_29]], i64 0, i64 %[[VAL_34]]
// CHECK:   %[[VAL_36:.*]] = mul nuw i64 %[[VAL_18]], %[[VAL_35]]
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp_collapsed.preheader:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_collapsed.header:
// CHECK:   %[[VAL_39:.*]] = phi i64 [ 0, %[[VAL_37]] ], [ %[[VAL_40:.*]], %[[VAL_41:.*]] ]
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_collapsed.cond:
// CHECK:   %[[VAL_43:.*]] = icmp ult i64 %[[VAL_39]], %[[VAL_36]]
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_44:.*]], label %[[VAL_45:.*]]
// CHECK: omp_collapsed.body:
// CHECK:   %[[VAL_46:.*]] = urem i64 %[[VAL_39]], %[[VAL_35]]
// CHECK:   %[[VAL_47:.*]] = udiv i64 %[[VAL_39]], %[[VAL_35]]
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_49:.*]] = mul i64 %[[VAL_47]], %[[VAL_4]]
// CHECK:   %[[VAL_50:.*]] = add i64 %[[VAL_49]], %[[VAL_9]]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_loop.preheader2:
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp_loop.body5:
// CHECK:   %[[VAL_53:.*]] = mul i64 %[[VAL_46]], %[[VAL_21]]
// CHECK:   %[[VAL_54:.*]] = add i64 %[[VAL_53]], %[[VAL_26]]
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_56:.*]] = getelementptr float, ptr %[[VAL_57:.*]], i64 %[[VAL_50]]
// CHECK:   %[[VAL_58:.*]] = getelementptr float, ptr %[[VAL_59:.*]], i64 %[[VAL_54]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_56]], align 4
// CHECK:   store float 2.000000e+00, ptr %[[VAL_58]], align 4
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont12:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp_loop.after8:
// CHECK:   br label %[[VAL_41]]
// CHECK: omp_collapsed.inc:
// CHECK:   %[[VAL_40]] = add nuw i64 %[[VAL_39]], 1
// CHECK:   br label %[[VAL_38]], !llvm.loop !1
// CHECK: omp_collapsed.exit:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: omp_collapsed.after:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_64:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_simple_multiple_simdlen_safelen(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_3:.*]] = icmp slt i64 %[[VAL_4:.*]], 0
// CHECK:   %[[VAL_5:.*]] = sub i64 0, %[[VAL_4]]
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_5]], i64 %[[VAL_4]]
// CHECK:   %[[VAL_7:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_8:.*]], i64 %[[VAL_9:.*]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_3]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = sub nsw i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_12:.*]] = icmp sle i64 %[[VAL_10]], %[[VAL_7]]
// CHECK:   %[[VAL_13:.*]] = sub i64 %[[VAL_11]], 1
// CHECK:   %[[VAL_14:.*]] = udiv i64 %[[VAL_13]], %[[VAL_6]]
// CHECK:   %[[VAL_15:.*]] = add i64 %[[VAL_14]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ule i64 %[[VAL_11]], %[[VAL_6]]
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_16]], i64 1, i64 %[[VAL_15]]
// CHECK:   %[[VAL_18:.*]] = select i1 %[[VAL_12]], i64 0, i64 %[[VAL_17]]
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_20:.*]] = icmp slt i64 %[[VAL_21:.*]], 0
// CHECK:   %[[VAL_22:.*]] = sub i64 0, %[[VAL_21]]
// CHECK:   %[[VAL_23:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_22]], i64 %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_25:.*]], i64 %[[VAL_26:.*]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_20]], i64 %[[VAL_26]], i64 %[[VAL_25]]
// CHECK:   %[[VAL_28:.*]] = sub nsw i64 %[[VAL_27]], %[[VAL_24]]
// CHECK:   %[[VAL_29:.*]] = icmp sle i64 %[[VAL_27]], %[[VAL_24]]
// CHECK:   %[[VAL_30:.*]] = sub i64 %[[VAL_28]], 1
// CHECK:   %[[VAL_31:.*]] = udiv i64 %[[VAL_30]], %[[VAL_23]]
// CHECK:   %[[VAL_32:.*]] = add i64 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = icmp ule i64 %[[VAL_28]], %[[VAL_23]]
// CHECK:   %[[VAL_34:.*]] = select i1 %[[VAL_33]], i64 1, i64 %[[VAL_32]]
// CHECK:   %[[VAL_35:.*]] = select i1 %[[VAL_29]], i64 0, i64 %[[VAL_34]]
// CHECK:   %[[VAL_36:.*]] = mul nuw i64 %[[VAL_18]], %[[VAL_35]]
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp_collapsed.preheader:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_collapsed.header:
// CHECK:   %[[VAL_39:.*]] = phi i64 [ 0, %[[VAL_37]] ], [ %[[VAL_40:.*]], %[[VAL_41:.*]] ]
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_collapsed.cond:
// CHECK:   %[[VAL_43:.*]] = icmp ult i64 %[[VAL_39]], %[[VAL_36]]
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_44:.*]], label %[[VAL_45:.*]]
// CHECK: omp_collapsed.body:
// CHECK:   %[[VAL_46:.*]] = urem i64 %[[VAL_39]], %[[VAL_35]]
// CHECK:   %[[VAL_47:.*]] = udiv i64 %[[VAL_39]], %[[VAL_35]]
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_49:.*]] = mul i64 %[[VAL_47]], %[[VAL_4]]
// CHECK:   %[[VAL_50:.*]] = add i64 %[[VAL_49]], %[[VAL_9]]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_loop.preheader2:
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp_loop.body5:
// CHECK:   %[[VAL_53:.*]] = mul i64 %[[VAL_46]], %[[VAL_21]]
// CHECK:   %[[VAL_54:.*]] = add i64 %[[VAL_53]], %[[VAL_26]]
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_56:.*]] = getelementptr float, ptr %[[VAL_57:.*]], i64 %[[VAL_50]]
// CHECK:   %[[VAL_58:.*]] = getelementptr float, ptr %[[VAL_59:.*]], i64 %[[VAL_54]]
// CHECK:   store float 2.000000e+00, ptr %[[VAL_56]], align 4
// CHECK:   store float 2.000000e+00, ptr %[[VAL_58]], align 4
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont12:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp_loop.after8:
// CHECK:   br label %[[VAL_41]]
// CHECK: omp_collapsed.inc:
// CHECK:   %[[VAL_40]] = add nuw i64 %[[VAL_39]], 1
// CHECK:   br label %[[VAL_38]], !llvm.loop !1
// CHECK: omp_collapsed.exit:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: omp_collapsed.after:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_64:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_if(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_3:.*]], align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_6:.*]], align 4
// CHECK:   %[[VAL_7:.*]] = icmp sge i32 %[[VAL_4]], %[[VAL_5]]
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.simd.region:
// CHECK:   %[[VAL_11:.*]] = select i1 false, i32 %[[VAL_2]], i32 0
// CHECK:   %[[VAL_12:.*]] = select i1 false, i32 0, i32 %[[VAL_2]]
// CHECK:   %[[VAL_13:.*]] = sub nsw i32 %[[VAL_12]], %[[VAL_11]]
// CHECK:   %[[VAL_14:.*]] = icmp slt i32 %[[VAL_12]], %[[VAL_11]]
// CHECK:   %[[VAL_15:.*]] = udiv i32 %[[VAL_13]], 1
// CHECK:   %[[VAL_16:.*]] = add i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_17:.*]] = select i1 %[[VAL_14]], i32 0, i32 %[[VAL_16]]
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_20:.*]] = phi i32 [ 0, %[[VAL_18]] ], [ %[[VAL_21:.*]], %[[VAL_22:.*]] ]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_24:.*]] = icmp ult i32 %[[VAL_20]], %[[VAL_17]]
// CHECK:   br i1 %[[VAL_24]], label %[[VAL_25:.*]], label %[[VAL_26:.*]]
// CHECK: omp_loop.body:
// CHECK:   br i1 %[[VAL_7]], label %[[VAL_27:.*]], label %[[VAL_28:.*]]
// CHECK: simd.if.then:
// CHECK:   %[[VAL_29:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_30:.*]] = add i32 %[[VAL_29]], 0
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_30]], ptr %[[VAL_0]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: simd.pre_latch:
// CHECK:   br label %[[VAL_22]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_21]] = add nuw i32 %[[VAL_20]], 1
// CHECK:   br label %[[VAL_19]], !llvm.loop !2
// CHECK: simd.if.else:
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: simd.if.else3:
// CHECK:   %[[VAL_35:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_36:.*]] = add i32 %[[VAL_35]], 0
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.loop_nest.region4:
// CHECK:   store i32 %[[VAL_36]], ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp.region.cont15:
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @simd_order(
// CHECK:   %[[VAL_0:.*]] = alloca i64, i64 1, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.simd.region:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_6:.*]] = phi i64 [ 0, %[[VAL_4]] ], [ %[[VAL_7:.*]], %[[VAL_8:.*]] ]
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_10:.*]] = icmp ult i64 %[[VAL_6]], 10
// CHECK:   br i1 %[[VAL_10]], label %[[VAL_11:.*]], label %[[VAL_12:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_13:.*]] = mul i64 %[[VAL_6]], 1
// CHECK:   %[[VAL_14:.*]] = add i64 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i64 %[[VAL_14]], ptr %[[VAL_0]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_8]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_7]] = add nuw i64 %[[VAL_6]], 1
// CHECK:   br label %[[VAL_5]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 66, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_static_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 66, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_static_chunk_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i32 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i32 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 1, ptr %[[VAL_1]], align 4
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_4u(ptr @1, i32 %[[VAL_24]], i32 65, i32 1, i32 %[[VAL_22]], i32 1, i32 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i32 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i32 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i32 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i32 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i32 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i32 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_4u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_4u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i32 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 67, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_auto_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 70, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_runtime_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 69, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_guided_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 68, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_nonmonotonic_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 1073741891, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @test_omp_wsloop_dynamic_monotonic_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 1, ptr %[[VAL_1]], align 4
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_8u(ptr @1, i32 %[[VAL_24]], i32 536870979, i64 1, i64 %[[VAL_22]], i64 1, i64 1)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i64 [ %[[VAL_27:.*]], %[[VAL_25]] ], [ %[[VAL_28:.*]], %[[VAL_29:.*]] ]
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_32:.*]] = icmp ult i64 %[[VAL_26]], %[[VAL_31]]
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_33:.*]], label %[[VAL_25]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_34:.*]] = mul i64 %[[VAL_26]], %[[VAL_8]]
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], %[[VAL_13]]
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   call void @body(i64 %[[VAL_35]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_28]] = add nuw i64 %[[VAL_26]], 1
// CHECK:   call void @__kmpc_dispatch_fini_8u(ptr @1, i32 %[[VAL_24]])
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_24]])
// CHECK:   %[[VAL_39:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_39]])
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_dispatch_next_8u(ptr @1, i32 %[[VAL_24]], ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]])
// CHECK:   %[[VAL_43:.*]] = icmp ne i32 %[[VAL_42]], 0
// CHECK:   %[[VAL_44:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27]] = sub i64 %[[VAL_44]], 1
// CHECK:   br i1 %[[VAL_43]], label %[[VAL_38]], label %[[VAL_45:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @.gomp_critical_user_.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_none.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_uncontended.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_contended.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_nonspeculative.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_nonspeculative_uncontended.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_nonspeculative_contended.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_speculative.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_speculative_uncontended.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @.gomp_critical_user_mutex_speculative_contended.var = common global [8 x i32] zeroinitializer, align 8
// CHECK-LABEL: define void @omp_critical(
// CHECK:   %[[VAL_0:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical(ptr @1, i32 %[[VAL_0]], ptr @.gomp_critical_user_.var)
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.critical.region:
// CHECK:   store i32 %[[VAL_2:.*]], ptr %[[VAL_3:.*]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_region.finalize:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_0]], ptr @.gomp_critical_user_.var)
// CHECK:   %[[VAL_6:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_6]], ptr @.gomp_critical_user_mutex_none.var, i32 0)
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.critical.region4:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_region.finalize2:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_6]], ptr @.gomp_critical_user_mutex_none.var)
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_10]], ptr @.gomp_critical_user_mutex_uncontended.var, i32 1)
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.critical.region8:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp_region.finalize6:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_10]], ptr @.gomp_critical_user_mutex_uncontended.var)
// CHECK:   %[[VAL_14:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_14]], ptr @.gomp_critical_user_mutex_contended.var, i32 2)
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.critical.region12:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.region.cont11:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_region.finalize10:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_14]], ptr @.gomp_critical_user_mutex_contended.var)
// CHECK:   %[[VAL_18:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_18]], ptr @.gomp_critical_user_mutex_nonspeculative.var, i32 4)
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.critical.region16:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.region.cont15:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_region.finalize14:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_18]], ptr @.gomp_critical_user_mutex_nonspeculative.var)
// CHECK:   %[[VAL_22:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_22]], ptr @.gomp_critical_user_mutex_nonspeculative_uncontended.var, i32 5)
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp.critical.region20:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp.region.cont19:
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_region.finalize18:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_22]], ptr @.gomp_critical_user_mutex_nonspeculative_uncontended.var)
// CHECK:   %[[VAL_26:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_26]], ptr @.gomp_critical_user_mutex_nonspeculative_contended.var, i32 6)
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.critical.region24:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp.region.cont23:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp_region.finalize22:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_26]], ptr @.gomp_critical_user_mutex_nonspeculative_contended.var)
// CHECK:   %[[VAL_30:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_30]], ptr @.gomp_critical_user_mutex_speculative.var, i32 8)
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp.critical.region28:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp.region.cont27:
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp_region.finalize26:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_30]], ptr @.gomp_critical_user_mutex_speculative.var)
// CHECK:   %[[VAL_34:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_34]], ptr @.gomp_critical_user_mutex_speculative_uncontended.var, i32 9)
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp.critical.region32:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.region.cont31:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp_region.finalize30:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_34]], ptr @.gomp_critical_user_mutex_speculative_uncontended.var)
// CHECK:   %[[VAL_38:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_critical_with_hint(ptr @1, i32 %[[VAL_38]], ptr @.gomp_critical_user_mutex_speculative_contended.var, i32 10)
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.critical.region36:
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp.region.cont35:
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_region.finalize34:
// CHECK:   call void @__kmpc_end_critical(ptr @1, i32 %[[VAL_38]], ptr @.gomp_critical_user_mutex_speculative_contended.var)
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @collapse_wsloop(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 10, ptr @collapse_wsloop..omp_par, i32 %[[VAL_6:.*]], i32 %[[VAL_7:.*]], i32 %[[VAL_8:.*]], i32 %[[VAL_9:.*]], i32 %[[VAL_10:.*]], i32 %[[VAL_11:.*]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]], i32 %[[VAL_14:.*]], ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @collapse_wsloop..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_9:.*]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_16:.*]] = icmp slt i32 %[[VAL_17:.*]], 0
// CHECK:   %[[VAL_18:.*]] = sub i32 0, %[[VAL_17]]
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_16]], i32 %[[VAL_18]], i32 %[[VAL_17]]
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_16]], i32 %[[VAL_21:.*]], i32 %[[VAL_22:.*]]
// CHECK:   %[[VAL_23:.*]] = select i1 %[[VAL_16]], i32 %[[VAL_22]], i32 %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = sub nsw i32 %[[VAL_23]], %[[VAL_20]]
// CHECK:   %[[VAL_25:.*]] = icmp sle i32 %[[VAL_23]], %[[VAL_20]]
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_24]], 1
// CHECK:   %[[VAL_27:.*]] = udiv i32 %[[VAL_26]], %[[VAL_19]]
// CHECK:   %[[VAL_28:.*]] = add i32 %[[VAL_27]], 1
// CHECK:   %[[VAL_29:.*]] = icmp ule i32 %[[VAL_24]], %[[VAL_19]]
// CHECK:   %[[VAL_30:.*]] = select i1 %[[VAL_29]], i32 1, i32 %[[VAL_28]]
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_25]], i32 0, i32 %[[VAL_30]]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_33:.*]] = icmp slt i32 %[[VAL_34:.*]], 0
// CHECK:   %[[VAL_35:.*]] = sub i32 0, %[[VAL_34]]
// CHECK:   %[[VAL_36:.*]] = select i1 %[[VAL_33]], i32 %[[VAL_35]], i32 %[[VAL_34]]
// CHECK:   %[[VAL_37:.*]] = select i1 %[[VAL_33]], i32 %[[VAL_38:.*]], i32 %[[VAL_39:.*]]
// CHECK:   %[[VAL_40:.*]] = select i1 %[[VAL_33]], i32 %[[VAL_39]], i32 %[[VAL_38]]
// CHECK:   %[[VAL_41:.*]] = sub nsw i32 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = icmp sle i32 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_43:.*]] = sub i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_44:.*]] = udiv i32 %[[VAL_43]], %[[VAL_36]]
// CHECK:   %[[VAL_45:.*]] = add i32 %[[VAL_44]], 1
// CHECK:   %[[VAL_46:.*]] = icmp ule i32 %[[VAL_41]], %[[VAL_36]]
// CHECK:   %[[VAL_47:.*]] = select i1 %[[VAL_46]], i32 1, i32 %[[VAL_45]]
// CHECK:   %[[VAL_48:.*]] = select i1 %[[VAL_42]], i32 0, i32 %[[VAL_47]]
// CHECK:   %[[VAL_49:.*]] = icmp slt i32 %[[VAL_50:.*]], 0
// CHECK:   %[[VAL_51:.*]] = sub i32 0, %[[VAL_50]]
// CHECK:   %[[VAL_52:.*]] = select i1 %[[VAL_49]], i32 %[[VAL_51]], i32 %[[VAL_50]]
// CHECK:   %[[VAL_53:.*]] = select i1 %[[VAL_49]], i32 %[[VAL_54:.*]], i32 %[[VAL_55:.*]]
// CHECK:   %[[VAL_56:.*]] = select i1 %[[VAL_49]], i32 %[[VAL_55]], i32 %[[VAL_54]]
// CHECK:   %[[VAL_57:.*]] = sub nsw i32 %[[VAL_56]], %[[VAL_53]]
// CHECK:   %[[VAL_58:.*]] = icmp sle i32 %[[VAL_56]], %[[VAL_53]]
// CHECK:   %[[VAL_59:.*]] = sub i32 %[[VAL_57]], 1
// CHECK:   %[[VAL_60:.*]] = udiv i32 %[[VAL_59]], %[[VAL_52]]
// CHECK:   %[[VAL_61:.*]] = add i32 %[[VAL_60]], 1
// CHECK:   %[[VAL_62:.*]] = icmp ule i32 %[[VAL_57]], %[[VAL_52]]
// CHECK:   %[[VAL_63:.*]] = select i1 %[[VAL_62]], i32 1, i32 %[[VAL_61]]
// CHECK:   %[[VAL_64:.*]] = select i1 %[[VAL_58]], i32 0, i32 %[[VAL_63]]
// CHECK:   %[[VAL_65:.*]] = mul nuw i32 %[[VAL_31]], %[[VAL_48]]
// CHECK:   %[[VAL_66:.*]] = mul nuw i32 %[[VAL_65]], %[[VAL_64]]
// CHECK:   br label %[[VAL_67:.*]]
// CHECK: omp_collapsed.preheader:
// CHECK:   store i32 0, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_68:.*]] = sub i32 %[[VAL_66]], 1
// CHECK:   store i32 %[[VAL_68]], ptr %[[VAL_5]], align 4
// CHECK:   store i32 1, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_69:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_69]], i32 34, ptr %[[VAL_3]], ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], i32 1, i32 1)
// CHECK:   %[[VAL_70:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_71:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_72:.*]] = sub i32 %[[VAL_71]], %[[VAL_70]]
// CHECK:   %[[VAL_73:.*]] = add i32 %[[VAL_72]], 1
// CHECK:   br label %[[VAL_74:.*]]
// CHECK: omp_collapsed.header:
// CHECK:   %[[VAL_75:.*]] = phi i32 [ 0, %[[VAL_67]] ], [ %[[VAL_76:.*]], %[[VAL_77:.*]] ]
// CHECK:   br label %[[VAL_78:.*]]
// CHECK: omp_collapsed.cond:
// CHECK:   %[[VAL_79:.*]] = icmp ult i32 %[[VAL_75]], %[[VAL_73]]
// CHECK:   br i1 %[[VAL_79]], label %[[VAL_80:.*]], label %[[VAL_81:.*]]
// CHECK: omp_collapsed.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_69]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_69]])
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: omp_collapsed.after:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_84:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_85:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_86:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_87:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_88:.*]]
// CHECK: omp_collapsed.body:
// CHECK:   %[[VAL_89:.*]] = add i32 %[[VAL_75]], %[[VAL_70]]
// CHECK:   %[[VAL_90:.*]] = urem i32 %[[VAL_89]], %[[VAL_64]]
// CHECK:   %[[VAL_91:.*]] = udiv i32 %[[VAL_89]], %[[VAL_64]]
// CHECK:   %[[VAL_92:.*]] = urem i32 %[[VAL_91]], %[[VAL_48]]
// CHECK:   %[[VAL_93:.*]] = udiv i32 %[[VAL_91]], %[[VAL_48]]
// CHECK:   br label %[[VAL_94:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_95:.*]] = mul i32 %[[VAL_93]], %[[VAL_17]]
// CHECK:   %[[VAL_96:.*]] = add i32 %[[VAL_95]], %[[VAL_22]]
// CHECK:   br label %[[VAL_97:.*]]
// CHECK: omp_loop.preheader5:
// CHECK:   br label %[[VAL_98:.*]]
// CHECK: omp_loop.body8:
// CHECK:   %[[VAL_99:.*]] = mul i32 %[[VAL_92]], %[[VAL_34]]
// CHECK:   %[[VAL_100:.*]] = add i32 %[[VAL_99]], %[[VAL_39]]
// CHECK:   br label %[[VAL_101:.*]]
// CHECK: omp_loop.preheader16:
// CHECK:   br label %[[VAL_102:.*]]
// CHECK: omp_loop.body19:
// CHECK:   %[[VAL_103:.*]] = mul i32 %[[VAL_90]], %[[VAL_50]]
// CHECK:   %[[VAL_104:.*]] = add i32 %[[VAL_103]], %[[VAL_55]]
// CHECK:   br label %[[VAL_105:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_106:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_107:.*]] = add i32 %[[VAL_106]], %[[VAL_96]]
// CHECK:   %[[VAL_108:.*]] = add i32 %[[VAL_107]], %[[VAL_100]]
// CHECK:   %[[VAL_109:.*]] = add i32 %[[VAL_108]], %[[VAL_104]]
// CHECK:   store i32 %[[VAL_109]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_110:.*]]
// CHECK: omp.region.cont26:
// CHECK:   br label %[[VAL_111:.*]]
// CHECK: omp_loop.after22:
// CHECK:   br label %[[VAL_112:.*]]
// CHECK: omp_loop.after11:
// CHECK:   br label %[[VAL_77]]
// CHECK: omp_collapsed.inc:
// CHECK:   %[[VAL_76]] = add nuw i32 %[[VAL_75]], 1
// CHECK:   br label %[[VAL_74]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @collapse_wsloop_dynamic(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 10, ptr @collapse_wsloop_dynamic..omp_par, i32 %[[VAL_6:.*]], i32 %[[VAL_7:.*]], i32 %[[VAL_8:.*]], i32 %[[VAL_9:.*]], i32 %[[VAL_10:.*]], i32 %[[VAL_11:.*]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]], i32 %[[VAL_14:.*]], ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @collapse_wsloop_dynamic..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_9:.*]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_16:.*]] = icmp slt i32 %[[VAL_17:.*]], 0
// CHECK:   %[[VAL_18:.*]] = sub i32 0, %[[VAL_17]]
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_16]], i32 %[[VAL_18]], i32 %[[VAL_17]]
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_16]], i32 %[[VAL_21:.*]], i32 %[[VAL_22:.*]]
// CHECK:   %[[VAL_23:.*]] = select i1 %[[VAL_16]], i32 %[[VAL_22]], i32 %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = sub nsw i32 %[[VAL_23]], %[[VAL_20]]
// CHECK:   %[[VAL_25:.*]] = icmp sle i32 %[[VAL_23]], %[[VAL_20]]
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_24]], 1
// CHECK:   %[[VAL_27:.*]] = udiv i32 %[[VAL_26]], %[[VAL_19]]
// CHECK:   %[[VAL_28:.*]] = add i32 %[[VAL_27]], 1
// CHECK:   %[[VAL_29:.*]] = icmp ule i32 %[[VAL_24]], %[[VAL_19]]
// CHECK:   %[[VAL_30:.*]] = select i1 %[[VAL_29]], i32 1, i32 %[[VAL_28]]
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_25]], i32 0, i32 %[[VAL_30]]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_33:.*]] = icmp slt i32 %[[VAL_34:.*]], 0
// CHECK:   %[[VAL_35:.*]] = sub i32 0, %[[VAL_34]]
// CHECK:   %[[VAL_36:.*]] = select i1 %[[VAL_33]], i32 %[[VAL_35]], i32 %[[VAL_34]]
// CHECK:   %[[VAL_37:.*]] = select i1 %[[VAL_33]], i32 %[[VAL_38:.*]], i32 %[[VAL_39:.*]]
// CHECK:   %[[VAL_40:.*]] = select i1 %[[VAL_33]], i32 %[[VAL_39]], i32 %[[VAL_38]]
// CHECK:   %[[VAL_41:.*]] = sub nsw i32 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = icmp sle i32 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_43:.*]] = sub i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_44:.*]] = udiv i32 %[[VAL_43]], %[[VAL_36]]
// CHECK:   %[[VAL_45:.*]] = add i32 %[[VAL_44]], 1
// CHECK:   %[[VAL_46:.*]] = icmp ule i32 %[[VAL_41]], %[[VAL_36]]
// CHECK:   %[[VAL_47:.*]] = select i1 %[[VAL_46]], i32 1, i32 %[[VAL_45]]
// CHECK:   %[[VAL_48:.*]] = select i1 %[[VAL_42]], i32 0, i32 %[[VAL_47]]
// CHECK:   %[[VAL_49:.*]] = icmp slt i32 %[[VAL_50:.*]], 0
// CHECK:   %[[VAL_51:.*]] = sub i32 0, %[[VAL_50]]
// CHECK:   %[[VAL_52:.*]] = select i1 %[[VAL_49]], i32 %[[VAL_51]], i32 %[[VAL_50]]
// CHECK:   %[[VAL_53:.*]] = select i1 %[[VAL_49]], i32 %[[VAL_54:.*]], i32 %[[VAL_55:.*]]
// CHECK:   %[[VAL_56:.*]] = select i1 %[[VAL_49]], i32 %[[VAL_55]], i32 %[[VAL_54]]
// CHECK:   %[[VAL_57:.*]] = sub nsw i32 %[[VAL_56]], %[[VAL_53]]
// CHECK:   %[[VAL_58:.*]] = icmp sle i32 %[[VAL_56]], %[[VAL_53]]
// CHECK:   %[[VAL_59:.*]] = sub i32 %[[VAL_57]], 1
// CHECK:   %[[VAL_60:.*]] = udiv i32 %[[VAL_59]], %[[VAL_52]]
// CHECK:   %[[VAL_61:.*]] = add i32 %[[VAL_60]], 1
// CHECK:   %[[VAL_62:.*]] = icmp ule i32 %[[VAL_57]], %[[VAL_52]]
// CHECK:   %[[VAL_63:.*]] = select i1 %[[VAL_62]], i32 1, i32 %[[VAL_61]]
// CHECK:   %[[VAL_64:.*]] = select i1 %[[VAL_58]], i32 0, i32 %[[VAL_63]]
// CHECK:   %[[VAL_65:.*]] = mul nuw i32 %[[VAL_31]], %[[VAL_48]]
// CHECK:   %[[VAL_66:.*]] = mul nuw i32 %[[VAL_65]], %[[VAL_64]]
// CHECK:   br label %[[VAL_67:.*]]
// CHECK: omp_collapsed.preheader:
// CHECK:   store i32 1, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_66]], ptr %[[VAL_5]], align 4
// CHECK:   store i32 1, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_68:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_4u(ptr @1, i32 %[[VAL_68]], i32 1073741859, i32 1, i32 %[[VAL_66]], i32 1, i32 1)
// CHECK:   br label %[[VAL_69:.*]]
// CHECK: omp_collapsed.preheader.outer.cond:
// CHECK:   %[[VAL_70:.*]] = call i32 @__kmpc_dispatch_next_4u(ptr @1, i32 %[[VAL_68]], ptr %[[VAL_3]], ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   %[[VAL_71:.*]] = icmp ne i32 %[[VAL_70]], 0
// CHECK:   %[[VAL_72:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_73:.*]] = sub i32 %[[VAL_72]], 1
// CHECK:   br i1 %[[VAL_71]], label %[[VAL_74:.*]], label %[[VAL_75:.*]]
// CHECK: omp_collapsed.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_68]])
// CHECK:   %[[VAL_76:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_76]])
// CHECK:   br label %[[VAL_77:.*]]
// CHECK: omp_collapsed.after:
// CHECK:   br label %[[VAL_78:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_79:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_80:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_81:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp_collapsed.header:
// CHECK:   %[[VAL_84:.*]] = phi i32 [ %[[VAL_73]], %[[VAL_69]] ], [ %[[VAL_85:.*]], %[[VAL_86:.*]] ]
// CHECK:   br label %[[VAL_87:.*]]
// CHECK: omp_collapsed.cond:
// CHECK:   %[[VAL_88:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_89:.*]] = icmp ult i32 %[[VAL_84]], %[[VAL_88]]
// CHECK:   br i1 %[[VAL_89]], label %[[VAL_90:.*]], label %[[VAL_69]]
// CHECK: omp_collapsed.body:
// CHECK:   %[[VAL_91:.*]] = urem i32 %[[VAL_84]], %[[VAL_64]]
// CHECK:   %[[VAL_92:.*]] = udiv i32 %[[VAL_84]], %[[VAL_64]]
// CHECK:   %[[VAL_93:.*]] = urem i32 %[[VAL_92]], %[[VAL_48]]
// CHECK:   %[[VAL_94:.*]] = udiv i32 %[[VAL_92]], %[[VAL_48]]
// CHECK:   br label %[[VAL_95:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_96:.*]] = mul i32 %[[VAL_94]], %[[VAL_17]]
// CHECK:   %[[VAL_97:.*]] = add i32 %[[VAL_96]], %[[VAL_22]]
// CHECK:   br label %[[VAL_98:.*]]
// CHECK: omp_loop.preheader5:
// CHECK:   br label %[[VAL_99:.*]]
// CHECK: omp_loop.body8:
// CHECK:   %[[VAL_100:.*]] = mul i32 %[[VAL_93]], %[[VAL_34]]
// CHECK:   %[[VAL_101:.*]] = add i32 %[[VAL_100]], %[[VAL_39]]
// CHECK:   br label %[[VAL_102:.*]]
// CHECK: omp_loop.preheader16:
// CHECK:   br label %[[VAL_103:.*]]
// CHECK: omp_loop.body19:
// CHECK:   %[[VAL_104:.*]] = mul i32 %[[VAL_91]], %[[VAL_50]]
// CHECK:   %[[VAL_105:.*]] = add i32 %[[VAL_104]], %[[VAL_55]]
// CHECK:   br label %[[VAL_106:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_107:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_108:.*]] = add i32 %[[VAL_107]], %[[VAL_97]]
// CHECK:   %[[VAL_109:.*]] = add i32 %[[VAL_108]], %[[VAL_101]]
// CHECK:   %[[VAL_110:.*]] = add i32 %[[VAL_109]], %[[VAL_105]]
// CHECK:   store i32 %[[VAL_110]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_111:.*]]
// CHECK: omp.region.cont26:
// CHECK:   br label %[[VAL_112:.*]]
// CHECK: omp_loop.after22:
// CHECK:   br label %[[VAL_113:.*]]
// CHECK: omp_loop.after11:
// CHECK:   br label %[[VAL_86]]
// CHECK: omp_collapsed.inc:
// CHECK:   %[[VAL_85]] = add nuw i32 %[[VAL_84]], 1
// CHECK:   br label %[[VAL_74]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_ordered(
// CHECK:   %[[VAL_0:.*]] = alloca [2 x i64], align 8
// CHECK:   %[[VAL_1:.*]] = alloca [2 x i64], align 8
// CHECK:   %[[VAL_2:.*]] = alloca [2 x i64], align 8
// CHECK:   %[[VAL_3:.*]] = alloca [2 x {{[{]}} i64, i64, i64 }], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca [1 x i64], align 8
// CHECK:   %[[VAL_9:.*]] = alloca [1 x i64], align 8
// CHECK:   %[[VAL_10:.*]] = alloca [1 x {{[{]}} i64, i64, i64 }], align 8
// CHECK:   %[[VAL_11:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_12:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_13:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_14:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_15:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_16:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_17:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_18:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_19:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_ordered(ptr @1, i32 %[[VAL_19]])
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.region.after_alloca36:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp.region.after_alloca9:
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp.ordered.region:
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_region.finalize:
// CHECK:   call void @__kmpc_end_ordered(ptr @1, i32 %[[VAL_19]])
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_27:.*]] = icmp slt i32 %[[VAL_28:.*]], 0
// CHECK:   %[[VAL_29:.*]] = sub i32 0, %[[VAL_28]]
// CHECK:   %[[VAL_30:.*]] = select i1 %[[VAL_27]], i32 %[[VAL_29]], i32 %[[VAL_28]]
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_27]], i32 %[[VAL_32:.*]], i32 %[[VAL_33:.*]]
// CHECK:   %[[VAL_34:.*]] = select i1 %[[VAL_27]], i32 %[[VAL_33]], i32 %[[VAL_32]]
// CHECK:   %[[VAL_35:.*]] = sub nsw i32 %[[VAL_34]], %[[VAL_31]]
// CHECK:   %[[VAL_36:.*]] = icmp sle i32 %[[VAL_34]], %[[VAL_31]]
// CHECK:   %[[VAL_37:.*]] = sub i32 %[[VAL_35]], 1
// CHECK:   %[[VAL_38:.*]] = udiv i32 %[[VAL_37]], %[[VAL_30]]
// CHECK:   %[[VAL_39:.*]] = add i32 %[[VAL_38]], 1
// CHECK:   %[[VAL_40:.*]] = icmp ule i32 %[[VAL_35]], %[[VAL_30]]
// CHECK:   %[[VAL_41:.*]] = select i1 %[[VAL_40]], i32 1, i32 %[[VAL_39]]
// CHECK:   %[[VAL_42:.*]] = select i1 %[[VAL_36]], i32 0, i32 %[[VAL_41]]
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 1, ptr %[[VAL_16]], align 4
// CHECK:   store i32 %[[VAL_42]], ptr %[[VAL_17]], align 4
// CHECK:   store i32 1, ptr %[[VAL_18]], align 4
// CHECK:   %[[VAL_44:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_dispatch_init_4u(ptr @1, i32 %[[VAL_44]], i32 66, i32 1, i32 %[[VAL_42]], i32 1, i32 1)
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_46:.*]] = phi i32 [ %[[VAL_47:.*]], %[[VAL_45]] ], [ %[[VAL_48:.*]], %[[VAL_49:.*]] ]
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_51:.*]] = load i32, ptr %[[VAL_17]], align 4
// CHECK:   %[[VAL_52:.*]] = icmp ult i32 %[[VAL_46]], %[[VAL_51]]
// CHECK:   br i1 %[[VAL_52]], label %[[VAL_53:.*]], label %[[VAL_45]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_54:.*]] = mul i32 %[[VAL_46]], %[[VAL_28]]
// CHECK:   %[[VAL_55:.*]] = add i32 %[[VAL_54]], %[[VAL_33]]
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_57:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_ordered(ptr @1, i32 %[[VAL_57]])
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.ordered.region6:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp_region.finalize4:
// CHECK:   call void @__kmpc_end_ordered(ptr @1, i32 %[[VAL_57]])
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_49]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_48]] = add nuw i32 %[[VAL_46]], 1
// CHECK:   call void @__kmpc_dispatch_fini_4u(ptr @1, i32 %[[VAL_44]])
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_dispatch_deinit(ptr @1, i32 %[[VAL_44]])
// CHECK:   %[[VAL_63:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_63]])
// CHECK:   br label %[[VAL_64:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_65:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: omp.wsloop.region11:
// CHECK:   %[[VAL_67:.*]] = icmp slt i32 %[[VAL_28]], 0
// CHECK:   %[[VAL_68:.*]] = sub i32 0, %[[VAL_28]]
// CHECK:   %[[VAL_69:.*]] = select i1 %[[VAL_67]], i32 %[[VAL_68]], i32 %[[VAL_28]]
// CHECK:   %[[VAL_70:.*]] = select i1 %[[VAL_67]], i32 %[[VAL_32]], i32 %[[VAL_33]]
// CHECK:   %[[VAL_71:.*]] = select i1 %[[VAL_67]], i32 %[[VAL_33]], i32 %[[VAL_32]]
// CHECK:   %[[VAL_72:.*]] = sub nsw i32 %[[VAL_71]], %[[VAL_70]]
// CHECK:   %[[VAL_73:.*]] = icmp sle i32 %[[VAL_71]], %[[VAL_70]]
// CHECK:   %[[VAL_74:.*]] = sub i32 %[[VAL_72]], 1
// CHECK:   %[[VAL_75:.*]] = udiv i32 %[[VAL_74]], %[[VAL_69]]
// CHECK:   %[[VAL_76:.*]] = add i32 %[[VAL_75]], 1
// CHECK:   %[[VAL_77:.*]] = icmp ule i32 %[[VAL_72]], %[[VAL_69]]
// CHECK:   %[[VAL_78:.*]] = select i1 %[[VAL_77]], i32 1, i32 %[[VAL_76]]
// CHECK:   %[[VAL_79:.*]] = select i1 %[[VAL_73]], i32 0, i32 %[[VAL_78]]
// CHECK:   br label %[[VAL_80:.*]]
// CHECK: omp_loop.preheader13:
// CHECK:   call void @llvm.memset.p0.i64(ptr align 8 %[[VAL_10]], i8 0, i64 24, i1 false)
// CHECK:   %[[VAL_81:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_82:.*]] = zext i32 %[[VAL_79]] to i64
// CHECK:   %[[VAL_83:.*]] = getelementptr inbounds [1 x {{[{]}} i64, i64, i64 }], ptr %[[VAL_10]], i64 0, i64 0
// CHECK:   %[[VAL_84:.*]] = getelementptr inbounds nuw {{[{]}} i64, i64, i64 }, ptr %[[VAL_83]], i32 0, i32 1
// CHECK:   %[[VAL_85:.*]] = getelementptr inbounds nuw {{[{]}} i64, i64, i64 }, ptr %[[VAL_83]], i32 0, i32 2
// CHECK:   store i64 %[[VAL_82]], ptr %[[VAL_84]], align 4
// CHECK:   store i64 1, ptr %[[VAL_85]], align 4
// CHECK:   %[[VAL_86:.*]] = getelementptr inbounds [1 x {{[{]}} i64, i64, i64 }], ptr %[[VAL_10]], i64 0, i64 0
// CHECK:   call void @__kmpc_doacross_init(ptr @1, i32 %[[VAL_81]], i32 1, ptr %[[VAL_86]])
// CHECK:   store i32 0, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_87:.*]] = sub i32 %[[VAL_79]], 1
// CHECK:   %[[VAL_88:.*]] = icmp eq i32 %[[VAL_79]], 0
// CHECK:   %[[VAL_89:.*]] = select i1 %[[VAL_88]], i32 0, i32 %[[VAL_87]]
// CHECK:   store i32 %[[VAL_89]], ptr %[[VAL_13]], align 4
// CHECK:   store i32 1, ptr %[[VAL_14]], align 4
// CHECK:   %[[VAL_90:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_90]], i32 33, ptr %[[VAL_11]], ptr %[[VAL_12]], ptr %[[VAL_13]], ptr %[[VAL_14]], i32 1, i32 1)
// CHECK:   %[[VAL_91:.*]] = load i32, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_92:.*]] = load i32, ptr %[[VAL_13]], align 4
// CHECK:   %[[VAL_93:.*]] = add i32 %[[VAL_92]], 1
// CHECK:   %[[VAL_94:.*]] = sub i32 %[[VAL_93]], %[[VAL_91]]
// CHECK:   %[[VAL_95:.*]] = load i32, ptr %[[VAL_14]], align 4
// CHECK:   %[[VAL_96:.*]] = sub nuw i32 %[[VAL_79]], %[[VAL_91]]
// CHECK:   %[[VAL_97:.*]] = icmp ule i32 %[[VAL_79]], %[[VAL_91]]
// CHECK:   %[[VAL_98:.*]] = sub i32 %[[VAL_96]], 1
// CHECK:   %[[VAL_99:.*]] = udiv i32 %[[VAL_98]], %[[VAL_95]]
// CHECK:   %[[VAL_100:.*]] = add i32 %[[VAL_99]], 1
// CHECK:   %[[VAL_101:.*]] = icmp ule i32 %[[VAL_96]], %[[VAL_95]]
// CHECK:   %[[VAL_102:.*]] = select i1 %[[VAL_101]], i32 1, i32 %[[VAL_100]]
// CHECK:   %[[VAL_103:.*]] = select i1 %[[VAL_97]], i32 0, i32 %[[VAL_102]]
// CHECK:   br label %[[VAL_104:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_105:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_106:.*]] = phi i32 [ 0, %[[VAL_104]] ], [ %[[VAL_107:.*]], %[[VAL_108:.*]] ]
// CHECK:   br label %[[VAL_109:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_110:.*]] = icmp ult i32 %[[VAL_106]], %[[VAL_103]]
// CHECK:   br i1 %[[VAL_110]], label %[[VAL_111:.*]], label %[[VAL_112:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_113:.*]] = mul i32 %[[VAL_106]], %[[VAL_95]]
// CHECK:   %[[VAL_114:.*]] = add i32 %[[VAL_113]], %[[VAL_91]]
// CHECK:   br label %[[VAL_115:.*]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_107]] = add nuw i32 %[[VAL_106]], 1
// CHECK:   br label %[[VAL_105]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_90]])
// CHECK:   br label %[[VAL_116:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   call void @__kmpc_doacross_fini(ptr @1, i32 %[[VAL_81]])
// CHECK:   %[[VAL_117:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_117]])
// CHECK:   br label %[[VAL_118:.*]]
// CHECK: omp_loop.preheader1334:
// CHECK:   %[[VAL_119:.*]] = add i32 %[[VAL_114]], %[[VAL_94]]
// CHECK:   %[[VAL_120:.*]] = icmp uge i32 %[[VAL_119]], %[[VAL_79]]
// CHECK:   %[[VAL_121:.*]] = sub i32 %[[VAL_79]], %[[VAL_114]]
// CHECK:   %[[VAL_122:.*]] = select i1 %[[VAL_120]], i32 %[[VAL_121]], i32 %[[VAL_94]]
// CHECK:   br label %[[VAL_123:.*]]
// CHECK: omp_loop.header14:
// CHECK:   %[[VAL_124:.*]] = phi i32 [ 0, %[[VAL_115]] ], [ %[[VAL_125:.*]], %[[VAL_126:.*]] ]
// CHECK:   br label %[[VAL_127:.*]]
// CHECK: omp_loop.cond15:
// CHECK:   %[[VAL_128:.*]] = icmp ult i32 %[[VAL_124]], %[[VAL_122]]
// CHECK:   br i1 %[[VAL_128]], label %[[VAL_129:.*]], label %[[VAL_130:.*]]
// CHECK: omp_loop.body16:
// CHECK:   %[[VAL_131:.*]] = add i32 %[[VAL_124]], %[[VAL_114]]
// CHECK:   %[[VAL_132:.*]] = mul i32 %[[VAL_131]], %[[VAL_28]]
// CHECK:   %[[VAL_133:.*]] = add i32 %[[VAL_132]], %[[VAL_33]]
// CHECK:   br label %[[VAL_134:.*]]
// CHECK: omp.loop_nest.region24:
// CHECK:   %[[VAL_135:.*]] = getelementptr inbounds [1 x i64], ptr %[[VAL_9]], i64 0, i64 0
// CHECK:   store i64 %[[VAL_136:.*]], ptr %[[VAL_135]], align 8, !llvm.access.group !1
// CHECK:   %[[VAL_137:.*]] = getelementptr inbounds [1 x i64], ptr %[[VAL_9]], i64 0, i64 0
// CHECK:   %[[VAL_138:.*]] = call i32 @__kmpc_global_thread_num(ptr @1), !llvm.access.group !1
// CHECK:   call void @__kmpc_doacross_wait(ptr @1, i32 %[[VAL_138]], ptr %[[VAL_137]]), !llvm.access.group !1
// CHECK:   %[[VAL_139:.*]] = getelementptr inbounds [1 x i64], ptr %[[VAL_8]], i64 0, i64 0
// CHECK:   store i64 %[[VAL_136]], ptr %[[VAL_139]], align 8, !llvm.access.group !1
// CHECK:   %[[VAL_140:.*]] = getelementptr inbounds [1 x i64], ptr %[[VAL_8]], i64 0, i64 0
// CHECK:   %[[VAL_141:.*]] = call i32 @__kmpc_global_thread_num(ptr @1), !llvm.access.group !1
// CHECK:   call void @__kmpc_doacross_post(ptr @1, i32 %[[VAL_141]], ptr %[[VAL_140]]), !llvm.access.group !1
// CHECK:   br label %[[VAL_142:.*]]
// CHECK: omp.region.cont23:
// CHECK:   br label %[[VAL_126]]
// CHECK: omp_loop.inc17:
// CHECK:   %[[VAL_125]] = add nuw i32 %[[VAL_124]], 1
// CHECK:   br label %[[VAL_123]], !llvm.loop !2
// CHECK: omp_loop.exit18:
// CHECK:   br label %[[VAL_108]]
// CHECK: omp_loop.after19:
// CHECK:   br label %[[VAL_143:.*]]
// CHECK: omp.region.cont10:
// CHECK:   br label %[[VAL_144:.*]]
// CHECK: omp.wsloop.region38:
// CHECK:   %[[VAL_145:.*]] = icmp slt i32 %[[VAL_28]], 0
// CHECK:   %[[VAL_146:.*]] = sub i32 0, %[[VAL_28]]
// CHECK:   %[[VAL_147:.*]] = select i1 %[[VAL_145]], i32 %[[VAL_146]], i32 %[[VAL_28]]
// CHECK:   %[[VAL_148:.*]] = select i1 %[[VAL_145]], i32 %[[VAL_32]], i32 %[[VAL_33]]
// CHECK:   %[[VAL_149:.*]] = select i1 %[[VAL_145]], i32 %[[VAL_33]], i32 %[[VAL_32]]
// CHECK:   %[[VAL_150:.*]] = sub nsw i32 %[[VAL_149]], %[[VAL_148]]
// CHECK:   %[[VAL_151:.*]] = icmp sle i32 %[[VAL_149]], %[[VAL_148]]
// CHECK:   %[[VAL_152:.*]] = sub i32 %[[VAL_150]], 1
// CHECK:   %[[VAL_153:.*]] = udiv i32 %[[VAL_152]], %[[VAL_147]]
// CHECK:   %[[VAL_154:.*]] = add i32 %[[VAL_153]], 1
// CHECK:   %[[VAL_155:.*]] = icmp ule i32 %[[VAL_150]], %[[VAL_147]]
// CHECK:   %[[VAL_156:.*]] = select i1 %[[VAL_155]], i32 1, i32 %[[VAL_154]]
// CHECK:   %[[VAL_157:.*]] = select i1 %[[VAL_151]], i32 0, i32 %[[VAL_156]]
// CHECK:   br label %[[VAL_158:.*]]
// CHECK: omp_loop.preheader40:
// CHECK:   call void @llvm.memset.p0.i64(ptr align 8 %[[VAL_3]], i8 0, i64 48, i1 false)
// CHECK:   %[[VAL_159:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_160:.*]] = sub i32 %[[VAL_157]], 1
// CHECK:   %[[VAL_161:.*]] = icmp eq i32 %[[VAL_157]], 0
// CHECK:   %[[VAL_162:.*]] = select i1 %[[VAL_161]], i32 0, i32 %[[VAL_160]]
// CHECK:   store i32 %[[VAL_162]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_163:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_163]], i32 33, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_164:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_165:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_166:.*]] = add i32 %[[VAL_165]], 1
// CHECK:   %[[VAL_167:.*]] = sub i32 %[[VAL_166]], %[[VAL_164]]
// CHECK:   %[[VAL_168:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_169:.*]] = sub nuw i32 %[[VAL_157]], %[[VAL_164]]
// CHECK:   %[[VAL_170:.*]] = icmp ule i32 %[[VAL_157]], %[[VAL_164]]
// CHECK:   %[[VAL_171:.*]] = sub i32 %[[VAL_169]], 1
// CHECK:   %[[VAL_172:.*]] = udiv i32 %[[VAL_171]], %[[VAL_168]]
// CHECK:   %[[VAL_173:.*]] = add i32 %[[VAL_172]], 1
// CHECK:   %[[VAL_174:.*]] = icmp ule i32 %[[VAL_169]], %[[VAL_168]]
// CHECK:   %[[VAL_175:.*]] = select i1 %[[VAL_174]], i32 1, i32 %[[VAL_173]]
// CHECK:   %[[VAL_176:.*]] = select i1 %[[VAL_170]], i32 0, i32 %[[VAL_175]]
// CHECK:   br label %[[VAL_177:.*]]
// CHECK: omp_dispatch.preheader71:
// CHECK:   br label %[[VAL_178:.*]]
// CHECK: omp_dispatch.header72:
// CHECK:   %[[VAL_179:.*]] = phi i32 [ 0, %[[VAL_177]] ], [ %[[VAL_180:.*]], %[[VAL_181:.*]] ]
// CHECK:   br label %[[VAL_182:.*]]
// CHECK: omp_dispatch.cond73:
// CHECK:   %[[VAL_183:.*]] = icmp ult i32 %[[VAL_179]], %[[VAL_176]]
// CHECK:   br i1 %[[VAL_183]], label %[[VAL_184:.*]], label %[[VAL_185:.*]]
// CHECK: omp_dispatch.body74:
// CHECK:   %[[VAL_186:.*]] = mul i32 %[[VAL_179]], %[[VAL_168]]
// CHECK:   %[[VAL_187:.*]] = add i32 %[[VAL_186]], %[[VAL_164]]
// CHECK:   br label %[[VAL_188:.*]]
// CHECK: omp_dispatch.inc75:
// CHECK:   %[[VAL_180]] = add nuw i32 %[[VAL_179]], 1
// CHECK:   br label %[[VAL_178]]
// CHECK: omp_dispatch.exit76:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_163]])
// CHECK:   br label %[[VAL_189:.*]]
// CHECK: omp_dispatch.after77:
// CHECK:   br label %[[VAL_190:.*]]
// CHECK: omp_loop.preheader4069:
// CHECK:   %[[VAL_191:.*]] = add i32 %[[VAL_187]], %[[VAL_167]]
// CHECK:   %[[VAL_192:.*]] = icmp uge i32 %[[VAL_191]], %[[VAL_157]]
// CHECK:   %[[VAL_193:.*]] = sub i32 %[[VAL_157]], %[[VAL_187]]
// CHECK:   %[[VAL_194:.*]] = select i1 %[[VAL_192]], i32 %[[VAL_193]], i32 %[[VAL_167]]
// CHECK:   br label %[[VAL_195:.*]]
// CHECK: omp_loop.header41:
// CHECK:   %[[VAL_196:.*]] = phi i32 [ 0, %[[VAL_188]] ], [ %[[VAL_197:.*]], %[[VAL_198:.*]] ]
// CHECK:   br label %[[VAL_199:.*]]
// CHECK: omp_loop.cond42:
// CHECK:   %[[VAL_200:.*]] = icmp ult i32 %[[VAL_196]], %[[VAL_194]]
// CHECK:   br i1 %[[VAL_200]], label %[[VAL_201:.*]], label %[[VAL_202:.*]]
// CHECK: omp_loop.body43:
// CHECK:   %[[VAL_203:.*]] = add i32 %[[VAL_196]], %[[VAL_187]]
// CHECK:   %[[VAL_204:.*]] = mul i32 %[[VAL_203]], %[[VAL_28]]
// CHECK:   %[[VAL_205:.*]] = add i32 %[[VAL_204]], %[[VAL_33]]
// CHECK:   br label %[[VAL_206:.*]]
// CHECK: omp.loop_nest.region51:
// CHECK:   %[[VAL_207:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_2]], i64 0, i64 0
// CHECK:   store i64 %[[VAL_136]], ptr %[[VAL_207]], align 8, !llvm.access.group !4
// CHECK:   %[[VAL_208:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_2]], i64 0, i64 1
// CHECK:   store i64 %[[VAL_209:.*]], ptr %[[VAL_208]], align 8, !llvm.access.group !4
// CHECK:   %[[VAL_210:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_2]], i64 0, i64 0
// CHECK:   %[[VAL_211:.*]] = call i32 @__kmpc_global_thread_num(ptr @1), !llvm.access.group !4
// CHECK:   call void @__kmpc_doacross_wait(ptr @1, i32 %[[VAL_211]], ptr %[[VAL_210]]), !llvm.access.group !4
// CHECK:   %[[VAL_212:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_1]], i64 0, i64 0
// CHECK:   store i64 %[[VAL_213:.*]], ptr %[[VAL_212]], align 8, !llvm.access.group !4
// CHECK:   %[[VAL_214:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_1]], i64 0, i64 1
// CHECK:   store i64 %[[VAL_215:.*]], ptr %[[VAL_214]], align 8, !llvm.access.group !4
// CHECK:   %[[VAL_216:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_1]], i64 0, i64 0
// CHECK:   %[[VAL_217:.*]] = call i32 @__kmpc_global_thread_num(ptr @1), !llvm.access.group !4
// CHECK:   call void @__kmpc_doacross_wait(ptr @1, i32 %[[VAL_217]], ptr %[[VAL_216]]), !llvm.access.group !4
// CHECK:   %[[VAL_218:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_0]], i64 0, i64 0
// CHECK:   store i64 %[[VAL_136]], ptr %[[VAL_218]], align 8, !llvm.access.group !4
// CHECK:   %[[VAL_219:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_0]], i64 0, i64 1
// CHECK:   store i64 %[[VAL_209]], ptr %[[VAL_219]], align 8, !llvm.access.group !4
// CHECK:   %[[VAL_220:.*]] = getelementptr inbounds [2 x i64], ptr %[[VAL_0]], i64 0, i64 0
// CHECK:   %[[VAL_221:.*]] = call i32 @__kmpc_global_thread_num(ptr @1), !llvm.access.group !4
// CHECK:   call void @__kmpc_doacross_post(ptr @1, i32 %[[VAL_221]], ptr %[[VAL_220]]), !llvm.access.group !4
// CHECK:   br label %[[VAL_222:.*]]
// CHECK: omp.region.cont50:
// CHECK:   br label %[[VAL_198]]
// CHECK: omp_loop.inc44:
// CHECK:   %[[VAL_197]] = add nuw i32 %[[VAL_196]], 1
// CHECK:   br label %[[VAL_195]], !llvm.loop !5
// CHECK: omp_loop.exit45:
// CHECK:   br label %[[VAL_181]]
// CHECK: omp_loop.after46:
// CHECK:   br label %[[VAL_223:.*]]
// CHECK: omp.region.cont37:
// CHECK:   ret void
// CHECK: omp_loop.preheader.outer.cond:
// CHECK:   %[[VAL_224:.*]] = call i32 @__kmpc_dispatch_next_4u(ptr @1, i32 %[[VAL_44]], ptr %[[VAL_15]], ptr %[[VAL_16]], ptr %[[VAL_17]], ptr %[[VAL_18]])
// CHECK:   %[[VAL_225:.*]] = icmp ne i32 %[[VAL_224]], 0
// CHECK:   %[[VAL_226:.*]] = load i32, ptr %[[VAL_16]], align 4
// CHECK:   %[[VAL_47]] = sub i32 %[[VAL_226]], 1
// CHECK:   br i1 %[[VAL_225]], label %[[VAL_62]], label %[[VAL_227:.*]]
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_atomic_read(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = load atomic i32, ptr %[[VAL_2:.*]] monotonic, align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_3:.*]], align 4
// CHECK:   %[[VAL_4:.*]] = load atomic i32, ptr %[[VAL_2]] seq_cst, align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_5:.*]] = load atomic i32, ptr %[[VAL_2]] acquire, align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   store i32 %[[VAL_5]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = load atomic i32, ptr %[[VAL_2]] monotonic, align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_3]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_read_implicit_cast(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca float, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca double, i64 1, align 8
// CHECK:   %[[VAL_3:.*]] = alloca [2 x {{[{]}} float, float }], i64 1, align 8
// CHECK:   %[[VAL_4:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} float, float }, ptr %[[VAL_3]], i64 0
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   call void @__atomic_load(i64 8, ptr %[[VAL_5]], ptr %[[VAL_0]], i32 0)
// CHECK:   %[[VAL_7:.*]] = load {{[{]}} float, float }, ptr %[[VAL_0]], align 8
// CHECK:   store {{[{]}} float, float } %[[VAL_7]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_8:.*]] = load atomic i32, ptr %[[VAL_1]] monotonic, align 4
// CHECK:   %[[VAL_9:.*]] = bitcast i32 %[[VAL_8]] to float
// CHECK:   store float %[[VAL_9]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_10:.*]] = load atomic i32, ptr %[[VAL_4]] monotonic, align 4
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_11:.*]] = load atomic i64, ptr %[[VAL_2]] monotonic, align 4
// CHECK:   %[[VAL_12:.*]] = bitcast i64 %[[VAL_11]] to double
// CHECK:   store double %[[VAL_12]], ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_13:.*]] = load atomic i32, ptr %[[VAL_4]] monotonic, align 4
// CHECK:   store i32 %[[VAL_13]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_14:.*]] = load atomic i64, ptr %[[VAL_2]] monotonic, align 4
// CHECK:   %[[VAL_15:.*]] = bitcast i64 %[[VAL_14]] to double
// CHECK:   store double %[[VAL_15]], ptr %[[VAL_4]], align 8
// CHECK:   %[[VAL_16:.*]] = load atomic i32, ptr %[[VAL_1]] monotonic, align 4
// CHECK:   %[[VAL_17:.*]] = bitcast i32 %[[VAL_16]] to float
// CHECK:   store float %[[VAL_17]], ptr %[[VAL_4]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_atomic_write(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   store atomic i32 %[[VAL_1:.*]], ptr %[[VAL_2:.*]] monotonic, align 4
// CHECK:   store atomic i32 %[[VAL_1]], ptr %[[VAL_2]] seq_cst, align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   store atomic i32 %[[VAL_1]], ptr %[[VAL_2]] release, align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   store atomic i32 %[[VAL_1]], ptr %[[VAL_2]] monotonic, align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_update(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = load atomic i32, ptr %[[VAL_3:.*]] monotonic, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_5:.*]] = phi i32 [ %[[VAL_2]], %[[VAL_1]] ], [ %[[VAL_6:.*]], %[[VAL_4]] ]
// CHECK:   %[[VAL_7:.*]] = mul i32 %[[VAL_5]], %[[VAL_8:.*]]
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_10:.*]] = cmpxchg ptr %[[VAL_3]], i32 %[[VAL_5]], i32 %[[VAL_9]] monotonic monotonic, align 4
// CHECK:   %[[VAL_6]] = extractvalue {{[{]}} i32, i1 } %[[VAL_10]], 0
// CHECK:   %[[VAL_11:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_10]], 1
// CHECK:   br i1 %[[VAL_11]], label %[[VAL_12:.*]], label %[[VAL_4]]
// CHECK: .atomic.exit:
// CHECK:   %[[VAL_13:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_8]] monotonic, align 4
// CHECK:   %[[VAL_14:.*]] = add i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_write(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_2:.*]] = alloca float, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = alloca float, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   %[[VAL_5:.*]] = load float, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = insertvalue {{[{]}} float, float } undef, float %[[VAL_5]], 0
// CHECK:   %[[VAL_7:.*]] = insertvalue {{[{]}} float, float } %[[VAL_6]], float 0.000000e+00, 1
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   store {{[{]}} float, float } %[[VAL_7]], ptr %[[VAL_1]], align 4
// CHECK:   call void @__atomic_store(i64 8, ptr %[[VAL_4]], ptr %[[VAL_1]], i32 0)
// CHECK:   store {{[{]}} float, float } {{[{]}} float 1.000000e+00, float 1.000000e+00 }, ptr %[[VAL_0]], align 4
// CHECK:   call void @__atomic_store(i64 8, ptr %[[VAL_4]], ptr %[[VAL_0]], i32 0)
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPomp_atomic_update_complex(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   %[[VAL_3:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   call void @__atomic_load(i64 8, ptr %[[VAL_3]], ptr %[[VAL_0]], i32 0)
// CHECK:   %[[VAL_5:.*]] = load {{[{]}} float, float }, ptr %[[VAL_0]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_7:.*]] = phi {{[{]}} float, float } [ %[[VAL_5]], %[[VAL_4]] ], [ %[[VAL_8:.*]], %[[VAL_6]] ]
// CHECK:   %[[VAL_9:.*]] = extractvalue {{[{]}} float, float } %[[VAL_7]], 0
// CHECK:   %[[VAL_10:.*]] = extractvalue {{[{]}} float, float } %[[VAL_7]], 1
// CHECK:   %[[VAL_11:.*]] = fadd contract float %[[VAL_9]], 1.000000e+00
// CHECK:   %[[VAL_12:.*]] = fadd contract float %[[VAL_10]], 1.000000e+00
// CHECK:   %[[VAL_13:.*]] = insertvalue {{[{]}} float, float } undef, float %[[VAL_11]], 0
// CHECK:   %[[VAL_14:.*]] = insertvalue {{[{]}} float, float } %[[VAL_13]], float %[[VAL_12]], 1
// CHECK:   store {{[{]}} float, float } %[[VAL_14]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_15:.*]] = call i1 @__atomic_compare_exchange(i64 8, ptr %[[VAL_3]], ptr %[[VAL_0]], ptr %[[VAL_1]], i32 0, i32 0)
// CHECK:   %[[VAL_8]] = load {{[{]}} float, float }, ptr %[[VAL_0]], align 4
// CHECK:   br i1 %[[VAL_15]], label %[[VAL_16:.*]], label %[[VAL_6]]
// CHECK: .atomic.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPomp_atomic_capture_complex(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   %[[VAL_3:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   store {{[{]}} float, float } {{[{]}} float 2.000000e+00, float 2.000000e+00 }, ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   call void @__atomic_load(i64 8, ptr %[[VAL_3]], ptr %[[VAL_0]], i32 0)
// CHECK:   %[[VAL_5:.*]] = load {{[{]}} float, float }, ptr %[[VAL_0]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_7:.*]] = phi {{[{]}} float, float } [ %[[VAL_5]], %[[VAL_4]] ], [ %[[VAL_8:.*]], %[[VAL_6]] ]
// CHECK:   %[[VAL_9:.*]] = extractvalue {{[{]}} float, float } %[[VAL_7]], 0
// CHECK:   %[[VAL_10:.*]] = extractvalue {{[{]}} float, float } %[[VAL_7]], 1
// CHECK:   %[[VAL_11:.*]] = fadd contract float %[[VAL_9]], 1.000000e+00
// CHECK:   %[[VAL_12:.*]] = fadd contract float %[[VAL_10]], 1.000000e+00
// CHECK:   %[[VAL_13:.*]] = insertvalue {{[{]}} float, float } undef, float %[[VAL_11]], 0
// CHECK:   %[[VAL_14:.*]] = insertvalue {{[{]}} float, float } %[[VAL_13]], float %[[VAL_12]], 1
// CHECK:   store {{[{]}} float, float } %[[VAL_14]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_15:.*]] = call i1 @__atomic_compare_exchange(i64 8, ptr %[[VAL_3]], ptr %[[VAL_0]], ptr %[[VAL_1]], i32 0, i32 0)
// CHECK:   %[[VAL_8]] = load {{[{]}} float, float }, ptr %[[VAL_0]], align 4
// CHECK:   br i1 %[[VAL_15]], label %[[VAL_16:.*]], label %[[VAL_6]]
// CHECK: .atomic.exit:
// CHECK:   store {{[{]}} float, float } %[[VAL_14]], ptr %[[VAL_2]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_read_complex(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} float, float }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   call void @__atomic_load(i64 8, ptr %[[VAL_2]], ptr %[[VAL_0]], i32 0)
// CHECK:   %[[VAL_4:.*]] = load {{[{]}} float, float }, ptr %[[VAL_0]], align 8
// CHECK:   store {{[{]}} float, float } %[[VAL_4]], ptr %[[VAL_1]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_update_ordering(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = load atomic i32, ptr %[[VAL_3:.*]] monotonic, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_5:.*]] = phi i32 [ %[[VAL_2]], %[[VAL_1]] ], [ %[[VAL_6:.*]], %[[VAL_4]] ]
// CHECK:   %[[VAL_7:.*]] = shl i32 %[[VAL_8:.*]], %[[VAL_5]]
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_10:.*]] = cmpxchg ptr %[[VAL_3]], i32 %[[VAL_5]], i32 %[[VAL_9]] monotonic monotonic, align 4
// CHECK:   %[[VAL_6]] = extractvalue {{[{]}} i32, i1 } %[[VAL_10]], 0
// CHECK:   %[[VAL_11:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_10]], 1
// CHECK:   br i1 %[[VAL_11]], label %[[VAL_12:.*]], label %[[VAL_4]]
// CHECK: .atomic.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_update_ordering(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = load atomic i32, ptr %[[VAL_3:.*]] monotonic, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_5:.*]] = phi i32 [ %[[VAL_2]], %[[VAL_1]] ], [ %[[VAL_6:.*]], %[[VAL_4]] ]
// CHECK:   %[[VAL_7:.*]] = shl i32 %[[VAL_5]], %[[VAL_8:.*]]
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_10:.*]] = cmpxchg ptr %[[VAL_3]], i32 %[[VAL_5]], i32 %[[VAL_9]] monotonic monotonic, align 4
// CHECK:   %[[VAL_6]] = extractvalue {{[{]}} i32, i1 } %[[VAL_10]], 0
// CHECK:   %[[VAL_11:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_10]], 1
// CHECK:   br i1 %[[VAL_11]], label %[[VAL_12:.*]], label %[[VAL_4]]
// CHECK: .atomic.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_update_intrinsic(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = load atomic i32, ptr %[[VAL_4:.*]] monotonic, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_6:.*]] = phi i32 [ %[[VAL_3]], %[[VAL_2]] ], [ %[[VAL_7:.*]], %[[VAL_5]] ]
// CHECK:   %[[VAL_8:.*]] = call i32 @llvm.smax.i32(i32 %[[VAL_6]], i32 %[[VAL_9:.*]])
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_11:.*]] = cmpxchg ptr %[[VAL_4]], i32 %[[VAL_6]], i32 %[[VAL_10]] monotonic monotonic, align 4
// CHECK:   %[[VAL_7]] = extractvalue {{[{]}} i32, i1 } %[[VAL_11]], 0
// CHECK:   %[[VAL_12:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_11]], 1
// CHECK:   br i1 %[[VAL_12]], label %[[VAL_13:.*]], label %[[VAL_5]]
// CHECK: .atomic.exit:
// CHECK:   %[[VAL_14:.*]] = load atomic i32, ptr %[[VAL_4]] monotonic, align 4
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: .atomic.cont3:
// CHECK:   %[[VAL_16:.*]] = phi i32 [ %[[VAL_14]], %[[VAL_13]] ], [ %[[VAL_17:.*]], %[[VAL_15]] ]
// CHECK:   %[[VAL_18:.*]] = call i32 @llvm.umax.i32(i32 %[[VAL_16]], i32 %[[VAL_9]])
// CHECK:   store i32 %[[VAL_18]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_19:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_20:.*]] = cmpxchg ptr %[[VAL_4]], i32 %[[VAL_16]], i32 %[[VAL_19]] monotonic monotonic, align 4
// CHECK:   %[[VAL_17]] = extractvalue {{[{]}} i32, i1 } %[[VAL_20]], 0
// CHECK:   %[[VAL_21:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_20]], 1
// CHECK:   br i1 %[[VAL_21]], label %[[VAL_22:.*]], label %[[VAL_15]]
// CHECK: .atomic.exit2:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @atomic_update_cmpxchg(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load float, ptr %[[VAL_2:.*]], align 4
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_4:.*]] = load atomic i32, ptr %[[VAL_5:.*]] monotonic, align 4
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_7:.*]] = phi i32 [ %[[VAL_4]], %[[VAL_3]] ], [ %[[VAL_8:.*]], %[[VAL_6]] ]
// CHECK:   %[[VAL_9:.*]] = sitofp i32 %[[VAL_7]] to float
// CHECK:   %[[VAL_10:.*]] = fadd float %[[VAL_9]], %[[VAL_1]]
// CHECK:   %[[VAL_11:.*]] = fptosi float %[[VAL_10]] to i32
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_12:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_13:.*]] = cmpxchg ptr %[[VAL_5]], i32 %[[VAL_7]], i32 %[[VAL_12]] monotonic monotonic, align 4
// CHECK:   %[[VAL_8]] = extractvalue {{[{]}} i32, i1 } %[[VAL_13]], 0
// CHECK:   %[[VAL_14:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_13]], 1
// CHECK:   br i1 %[[VAL_14]], label %[[VAL_15:.*]], label %[[VAL_6]]
// CHECK: .atomic.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_capture_prefix_update(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_9:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_11:.*]] = atomicrmw add ptr %[[VAL_12:.*]], i32 %[[VAL_13:.*]] monotonic, align 4
// CHECK:   %[[VAL_14:.*]] = add i32 %[[VAL_11]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_14]], ptr %[[VAL_15:.*]], align 4
// CHECK:   %[[VAL_16:.*]] = atomicrmw sub ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_16]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_17]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_18:.*]] = atomicrmw and ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_19:.*]] = and i32 %[[VAL_18]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_19]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_20:.*]] = atomicrmw or ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_21:.*]] = or i32 %[[VAL_20]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_21]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_22:.*]] = atomicrmw xor ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_23:.*]] = xor i32 %[[VAL_22]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_23]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_24:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_26:.*]] = phi i32 [ %[[VAL_24]], %[[VAL_10]] ], [ %[[VAL_27:.*]], %[[VAL_25]] ]
// CHECK:   %[[VAL_28:.*]] = mul i32 %[[VAL_26]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_29:.*]] = load i32, ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_30:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_26]], i32 %[[VAL_29]] monotonic monotonic, align 4
// CHECK:   %[[VAL_27]] = extractvalue {{[{]}} i32, i1 } %[[VAL_30]], 0
// CHECK:   %[[VAL_31:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_30]], 1
// CHECK:   br i1 %[[VAL_31]], label %[[VAL_32:.*]], label %[[VAL_25]]
// CHECK: .atomic.exit:
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_33:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: .atomic.cont3:
// CHECK:   %[[VAL_35:.*]] = phi i32 [ %[[VAL_33]], %[[VAL_32]] ], [ %[[VAL_36:.*]], %[[VAL_34]] ]
// CHECK:   %[[VAL_37:.*]] = sdiv i32 %[[VAL_35]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_37]], ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_38:.*]] = load i32, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_39:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_35]], i32 %[[VAL_38]] monotonic monotonic, align 4
// CHECK:   %[[VAL_36]] = extractvalue {{[{]}} i32, i1 } %[[VAL_39]], 0
// CHECK:   %[[VAL_40:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_39]], 1
// CHECK:   br i1 %[[VAL_40]], label %[[VAL_41:.*]], label %[[VAL_34]]
// CHECK: .atomic.exit2:
// CHECK:   store i32 %[[VAL_37]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_42:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: .atomic.cont7:
// CHECK:   %[[VAL_44:.*]] = phi i32 [ %[[VAL_42]], %[[VAL_41]] ], [ %[[VAL_45:.*]], %[[VAL_43]] ]
// CHECK:   %[[VAL_46:.*]] = udiv i32 %[[VAL_44]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_46]], ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_47:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_48:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_44]], i32 %[[VAL_47]] monotonic monotonic, align 4
// CHECK:   %[[VAL_45]] = extractvalue {{[{]}} i32, i1 } %[[VAL_48]], 0
// CHECK:   %[[VAL_49:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_48]], 1
// CHECK:   br i1 %[[VAL_49]], label %[[VAL_50:.*]], label %[[VAL_43]]
// CHECK: .atomic.exit6:
// CHECK:   store i32 %[[VAL_46]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_51:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: .atomic.cont11:
// CHECK:   %[[VAL_53:.*]] = phi i32 [ %[[VAL_51]], %[[VAL_50]] ], [ %[[VAL_54:.*]], %[[VAL_52]] ]
// CHECK:   %[[VAL_55:.*]] = shl i32 %[[VAL_53]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_55]], ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_56:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_57:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_53]], i32 %[[VAL_56]] monotonic monotonic, align 4
// CHECK:   %[[VAL_54]] = extractvalue {{[{]}} i32, i1 } %[[VAL_57]], 0
// CHECK:   %[[VAL_58:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_57]], 1
// CHECK:   br i1 %[[VAL_58]], label %[[VAL_59:.*]], label %[[VAL_52]]
// CHECK: .atomic.exit10:
// CHECK:   store i32 %[[VAL_55]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_60:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: .atomic.cont15:
// CHECK:   %[[VAL_62:.*]] = phi i32 [ %[[VAL_60]], %[[VAL_59]] ], [ %[[VAL_63:.*]], %[[VAL_61]] ]
// CHECK:   %[[VAL_64:.*]] = lshr i32 %[[VAL_62]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_64]], ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_65:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_66:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_62]], i32 %[[VAL_65]] monotonic monotonic, align 4
// CHECK:   %[[VAL_63]] = extractvalue {{[{]}} i32, i1 } %[[VAL_66]], 0
// CHECK:   %[[VAL_67:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_66]], 1
// CHECK:   br i1 %[[VAL_67]], label %[[VAL_68:.*]], label %[[VAL_61]]
// CHECK: .atomic.exit14:
// CHECK:   store i32 %[[VAL_64]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_69:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_70:.*]]
// CHECK: .atomic.cont19:
// CHECK:   %[[VAL_71:.*]] = phi i32 [ %[[VAL_69]], %[[VAL_68]] ], [ %[[VAL_72:.*]], %[[VAL_70]] ]
// CHECK:   %[[VAL_73:.*]] = ashr i32 %[[VAL_71]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_73]], ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_74:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_75:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_71]], i32 %[[VAL_74]] monotonic monotonic, align 4
// CHECK:   %[[VAL_72]] = extractvalue {{[{]}} i32, i1 } %[[VAL_75]], 0
// CHECK:   %[[VAL_76:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_75]], 1
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_70]]
// CHECK: .atomic.exit18:
// CHECK:   store i32 %[[VAL_73]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_78:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_79:.*]]
// CHECK: .atomic.cont23:
// CHECK:   %[[VAL_80:.*]] = phi i32 [ %[[VAL_78]], %[[VAL_77]] ], [ %[[VAL_81:.*]], %[[VAL_79]] ]
// CHECK:   %[[VAL_82:.*]] = call i32 @llvm.smax.i32(i32 %[[VAL_80]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_82]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_83:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_84:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_80]], i32 %[[VAL_83]] monotonic monotonic, align 4
// CHECK:   %[[VAL_81]] = extractvalue {{[{]}} i32, i1 } %[[VAL_84]], 0
// CHECK:   %[[VAL_85:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_84]], 1
// CHECK:   br i1 %[[VAL_85]], label %[[VAL_86:.*]], label %[[VAL_79]]
// CHECK: .atomic.exit22:
// CHECK:   store i32 %[[VAL_82]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_87:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_88:.*]]
// CHECK: .atomic.cont27:
// CHECK:   %[[VAL_89:.*]] = phi i32 [ %[[VAL_87]], %[[VAL_86]] ], [ %[[VAL_90:.*]], %[[VAL_88]] ]
// CHECK:   %[[VAL_91:.*]] = call i32 @llvm.smin.i32(i32 %[[VAL_89]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_91]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_92:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_93:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_89]], i32 %[[VAL_92]] monotonic monotonic, align 4
// CHECK:   %[[VAL_90]] = extractvalue {{[{]}} i32, i1 } %[[VAL_93]], 0
// CHECK:   %[[VAL_94:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_93]], 1
// CHECK:   br i1 %[[VAL_94]], label %[[VAL_95:.*]], label %[[VAL_88]]
// CHECK: .atomic.exit26:
// CHECK:   store i32 %[[VAL_91]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_96:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_97:.*]]
// CHECK: .atomic.cont31:
// CHECK:   %[[VAL_98:.*]] = phi i32 [ %[[VAL_96]], %[[VAL_95]] ], [ %[[VAL_99:.*]], %[[VAL_97]] ]
// CHECK:   %[[VAL_100:.*]] = call i32 @llvm.umax.i32(i32 %[[VAL_98]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_100]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_101:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_102:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_98]], i32 %[[VAL_101]] monotonic monotonic, align 4
// CHECK:   %[[VAL_99]] = extractvalue {{[{]}} i32, i1 } %[[VAL_102]], 0
// CHECK:   %[[VAL_103:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_102]], 1
// CHECK:   br i1 %[[VAL_103]], label %[[VAL_104:.*]], label %[[VAL_97]]
// CHECK: .atomic.exit30:
// CHECK:   store i32 %[[VAL_100]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_105:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_106:.*]]
// CHECK: .atomic.cont35:
// CHECK:   %[[VAL_107:.*]] = phi i32 [ %[[VAL_105]], %[[VAL_104]] ], [ %[[VAL_108:.*]], %[[VAL_106]] ]
// CHECK:   %[[VAL_109:.*]] = call i32 @llvm.umin.i32(i32 %[[VAL_107]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_109]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_110:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_111:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_107]], i32 %[[VAL_110]] monotonic monotonic, align 4
// CHECK:   %[[VAL_108]] = extractvalue {{[{]}} i32, i1 } %[[VAL_111]], 0
// CHECK:   %[[VAL_112:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_111]], 1
// CHECK:   br i1 %[[VAL_112]], label %[[VAL_113:.*]], label %[[VAL_106]]
// CHECK: .atomic.exit34:
// CHECK:   store i32 %[[VAL_109]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_114:.*]] = atomicrmw fadd ptr %[[VAL_115:.*]], float %[[VAL_116:.*]] monotonic, align 4
// CHECK:   %[[VAL_117:.*]] = fadd float %[[VAL_114]], %[[VAL_116]]
// CHECK:   store float %[[VAL_117]], ptr %[[VAL_118:.*]], align 4
// CHECK:   %[[VAL_119:.*]] = atomicrmw fsub ptr %[[VAL_115]], float %[[VAL_116]] monotonic, align 4
// CHECK:   %[[VAL_120:.*]] = fsub float %[[VAL_119]], %[[VAL_116]]
// CHECK:   store float %[[VAL_120]], ptr %[[VAL_118]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_atomic_capture_postfix_update(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_9:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_11:.*]] = atomicrmw add ptr %[[VAL_12:.*]], i32 %[[VAL_13:.*]] monotonic, align 4
// CHECK:   %[[VAL_14:.*]] = add i32 %[[VAL_11]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_15:.*]], align 4
// CHECK:   %[[VAL_16:.*]] = atomicrmw sub ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_16]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_16]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_18:.*]] = atomicrmw and ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_19:.*]] = and i32 %[[VAL_18]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_18]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_20:.*]] = atomicrmw or ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_21:.*]] = or i32 %[[VAL_20]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_20]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_22:.*]] = atomicrmw xor ptr %[[VAL_12]], i32 %[[VAL_13]] monotonic, align 4
// CHECK:   %[[VAL_23:.*]] = xor i32 %[[VAL_22]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_24:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_26:.*]] = phi i32 [ %[[VAL_24]], %[[VAL_10]] ], [ %[[VAL_27:.*]], %[[VAL_25]] ]
// CHECK:   %[[VAL_28:.*]] = mul i32 %[[VAL_26]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_29:.*]] = load i32, ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_30:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_26]], i32 %[[VAL_29]] monotonic monotonic, align 4
// CHECK:   %[[VAL_27]] = extractvalue {{[{]}} i32, i1 } %[[VAL_30]], 0
// CHECK:   %[[VAL_31:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_30]], 1
// CHECK:   br i1 %[[VAL_31]], label %[[VAL_32:.*]], label %[[VAL_25]]
// CHECK: .atomic.exit:
// CHECK:   store i32 %[[VAL_26]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_33:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: .atomic.cont3:
// CHECK:   %[[VAL_35:.*]] = phi i32 [ %[[VAL_33]], %[[VAL_32]] ], [ %[[VAL_36:.*]], %[[VAL_34]] ]
// CHECK:   %[[VAL_37:.*]] = sdiv i32 %[[VAL_35]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_37]], ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_38:.*]] = load i32, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_39:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_35]], i32 %[[VAL_38]] monotonic monotonic, align 4
// CHECK:   %[[VAL_36]] = extractvalue {{[{]}} i32, i1 } %[[VAL_39]], 0
// CHECK:   %[[VAL_40:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_39]], 1
// CHECK:   br i1 %[[VAL_40]], label %[[VAL_41:.*]], label %[[VAL_34]]
// CHECK: .atomic.exit2:
// CHECK:   store i32 %[[VAL_35]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_42:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: .atomic.cont7:
// CHECK:   %[[VAL_44:.*]] = phi i32 [ %[[VAL_42]], %[[VAL_41]] ], [ %[[VAL_45:.*]], %[[VAL_43]] ]
// CHECK:   %[[VAL_46:.*]] = udiv i32 %[[VAL_44]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_46]], ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_47:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_48:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_44]], i32 %[[VAL_47]] monotonic monotonic, align 4
// CHECK:   %[[VAL_45]] = extractvalue {{[{]}} i32, i1 } %[[VAL_48]], 0
// CHECK:   %[[VAL_49:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_48]], 1
// CHECK:   br i1 %[[VAL_49]], label %[[VAL_50:.*]], label %[[VAL_43]]
// CHECK: .atomic.exit6:
// CHECK:   store i32 %[[VAL_44]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_51:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: .atomic.cont11:
// CHECK:   %[[VAL_53:.*]] = phi i32 [ %[[VAL_51]], %[[VAL_50]] ], [ %[[VAL_54:.*]], %[[VAL_52]] ]
// CHECK:   %[[VAL_55:.*]] = shl i32 %[[VAL_53]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_55]], ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_56:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_57:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_53]], i32 %[[VAL_56]] monotonic monotonic, align 4
// CHECK:   %[[VAL_54]] = extractvalue {{[{]}} i32, i1 } %[[VAL_57]], 0
// CHECK:   %[[VAL_58:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_57]], 1
// CHECK:   br i1 %[[VAL_58]], label %[[VAL_59:.*]], label %[[VAL_52]]
// CHECK: .atomic.exit10:
// CHECK:   store i32 %[[VAL_53]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_60:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: .atomic.cont15:
// CHECK:   %[[VAL_62:.*]] = phi i32 [ %[[VAL_60]], %[[VAL_59]] ], [ %[[VAL_63:.*]], %[[VAL_61]] ]
// CHECK:   %[[VAL_64:.*]] = lshr i32 %[[VAL_62]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_64]], ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_65:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_66:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_62]], i32 %[[VAL_65]] monotonic monotonic, align 4
// CHECK:   %[[VAL_63]] = extractvalue {{[{]}} i32, i1 } %[[VAL_66]], 0
// CHECK:   %[[VAL_67:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_66]], 1
// CHECK:   br i1 %[[VAL_67]], label %[[VAL_68:.*]], label %[[VAL_61]]
// CHECK: .atomic.exit14:
// CHECK:   store i32 %[[VAL_62]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_69:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_70:.*]]
// CHECK: .atomic.cont19:
// CHECK:   %[[VAL_71:.*]] = phi i32 [ %[[VAL_69]], %[[VAL_68]] ], [ %[[VAL_72:.*]], %[[VAL_70]] ]
// CHECK:   %[[VAL_73:.*]] = ashr i32 %[[VAL_71]], %[[VAL_13]]
// CHECK:   store i32 %[[VAL_73]], ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_74:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_75:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_71]], i32 %[[VAL_74]] monotonic monotonic, align 4
// CHECK:   %[[VAL_72]] = extractvalue {{[{]}} i32, i1 } %[[VAL_75]], 0
// CHECK:   %[[VAL_76:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_75]], 1
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_70]]
// CHECK: .atomic.exit18:
// CHECK:   store i32 %[[VAL_71]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_78:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_79:.*]]
// CHECK: .atomic.cont23:
// CHECK:   %[[VAL_80:.*]] = phi i32 [ %[[VAL_78]], %[[VAL_77]] ], [ %[[VAL_81:.*]], %[[VAL_79]] ]
// CHECK:   %[[VAL_82:.*]] = call i32 @llvm.smax.i32(i32 %[[VAL_80]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_82]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_83:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_84:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_80]], i32 %[[VAL_83]] monotonic monotonic, align 4
// CHECK:   %[[VAL_81]] = extractvalue {{[{]}} i32, i1 } %[[VAL_84]], 0
// CHECK:   %[[VAL_85:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_84]], 1
// CHECK:   br i1 %[[VAL_85]], label %[[VAL_86:.*]], label %[[VAL_79]]
// CHECK: .atomic.exit22:
// CHECK:   store i32 %[[VAL_80]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_87:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_88:.*]]
// CHECK: .atomic.cont27:
// CHECK:   %[[VAL_89:.*]] = phi i32 [ %[[VAL_87]], %[[VAL_86]] ], [ %[[VAL_90:.*]], %[[VAL_88]] ]
// CHECK:   %[[VAL_91:.*]] = call i32 @llvm.smin.i32(i32 %[[VAL_89]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_91]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_92:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_93:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_89]], i32 %[[VAL_92]] monotonic monotonic, align 4
// CHECK:   %[[VAL_90]] = extractvalue {{[{]}} i32, i1 } %[[VAL_93]], 0
// CHECK:   %[[VAL_94:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_93]], 1
// CHECK:   br i1 %[[VAL_94]], label %[[VAL_95:.*]], label %[[VAL_88]]
// CHECK: .atomic.exit26:
// CHECK:   store i32 %[[VAL_89]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_96:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_97:.*]]
// CHECK: .atomic.cont31:
// CHECK:   %[[VAL_98:.*]] = phi i32 [ %[[VAL_96]], %[[VAL_95]] ], [ %[[VAL_99:.*]], %[[VAL_97]] ]
// CHECK:   %[[VAL_100:.*]] = call i32 @llvm.umax.i32(i32 %[[VAL_98]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_100]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_101:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_102:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_98]], i32 %[[VAL_101]] monotonic monotonic, align 4
// CHECK:   %[[VAL_99]] = extractvalue {{[{]}} i32, i1 } %[[VAL_102]], 0
// CHECK:   %[[VAL_103:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_102]], 1
// CHECK:   br i1 %[[VAL_103]], label %[[VAL_104:.*]], label %[[VAL_97]]
// CHECK: .atomic.exit30:
// CHECK:   store i32 %[[VAL_98]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_105:.*]] = load atomic i32, ptr %[[VAL_12]] monotonic, align 4
// CHECK:   br label %[[VAL_106:.*]]
// CHECK: .atomic.cont35:
// CHECK:   %[[VAL_107:.*]] = phi i32 [ %[[VAL_105]], %[[VAL_104]] ], [ %[[VAL_108:.*]], %[[VAL_106]] ]
// CHECK:   %[[VAL_109:.*]] = call i32 @llvm.umin.i32(i32 %[[VAL_107]], i32 %[[VAL_13]])
// CHECK:   store i32 %[[VAL_109]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_110:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_111:.*]] = cmpxchg ptr %[[VAL_12]], i32 %[[VAL_107]], i32 %[[VAL_110]] monotonic monotonic, align 4
// CHECK:   %[[VAL_108]] = extractvalue {{[{]}} i32, i1 } %[[VAL_111]], 0
// CHECK:   %[[VAL_112:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_111]], 1
// CHECK:   br i1 %[[VAL_112]], label %[[VAL_113:.*]], label %[[VAL_106]]
// CHECK: .atomic.exit34:
// CHECK:   store i32 %[[VAL_107]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_114:.*]] = atomicrmw fadd ptr %[[VAL_115:.*]], float %[[VAL_116:.*]] monotonic, align 4
// CHECK:   %[[VAL_117:.*]] = fadd float %[[VAL_114]], %[[VAL_116]]
// CHECK:   store float %[[VAL_114]], ptr %[[VAL_118:.*]], align 4
// CHECK:   %[[VAL_119:.*]] = atomicrmw fsub ptr %[[VAL_115]], float %[[VAL_116]] monotonic, align 4
// CHECK:   %[[VAL_120:.*]] = fsub float %[[VAL_119]], %[[VAL_116]]
// CHECK:   store float %[[VAL_119]], ptr %[[VAL_118]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_atomic_capture_misc(
// CHECK:   %[[VAL_0:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = atomicrmw xchg ptr %[[VAL_3:.*]], i32 %[[VAL_4:.*]] monotonic, align 4
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_5:.*]], align 4
// CHECK:   %[[VAL_6:.*]] = load atomic i32, ptr %[[VAL_7:.*]] monotonic, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: .atomic.cont:
// CHECK:   %[[VAL_9:.*]] = phi i32 [ %[[VAL_6]], %[[VAL_1]] ], [ %[[VAL_10:.*]], %[[VAL_8]] ]
// CHECK:   %[[VAL_11:.*]] = bitcast i32 %[[VAL_9]] to float
// CHECK:   store float %[[VAL_12:.*]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_13:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_14:.*]] = cmpxchg ptr %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_13]] monotonic monotonic, align 4
// CHECK:   %[[VAL_10]] = extractvalue {{[{]}} i32, i1 } %[[VAL_14]], 0
// CHECK:   %[[VAL_15:.*]] = extractvalue {{[{]}} i32, i1 } %[[VAL_14]], 1
// CHECK:   br i1 %[[VAL_15]], label %[[VAL_16:.*]], label %[[VAL_8]]
// CHECK: .atomic.exit:
// CHECK:   store float %[[VAL_11]], ptr %[[VAL_17:.*]], align 4
// CHECK:   %[[VAL_18:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_4]] seq_cst, align 4
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], %[[VAL_4]]
// CHECK:   store i32 %[[VAL_18]], ptr %[[VAL_5]], align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   %[[VAL_20:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_4]] acquire, align 4
// CHECK:   %[[VAL_21:.*]] = add i32 %[[VAL_20]], %[[VAL_4]]
// CHECK:   store i32 %[[VAL_20]], ptr %[[VAL_5]], align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   %[[VAL_22:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_4]] release, align 4
// CHECK:   %[[VAL_23:.*]] = add i32 %[[VAL_22]], %[[VAL_4]]
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_5]], align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   %[[VAL_24:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_4]] monotonic, align 4
// CHECK:   %[[VAL_25:.*]] = add i32 %[[VAL_24]], %[[VAL_4]]
// CHECK:   store i32 %[[VAL_24]], ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_26:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_4]] acq_rel, align 4
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], %[[VAL_4]]
// CHECK:   store i32 %[[VAL_26]], ptr %[[VAL_5]], align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_sections_empty(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_sections_trivial(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_section_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_7]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_10:.*]] = sub i32 %[[VAL_9]], %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = add i32 %[[VAL_10]], 1
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_section_loop.header:
// CHECK:   %[[VAL_13:.*]] = phi i32 [ 0, %[[VAL_6]] ], [ %[[VAL_14:.*]], %[[VAL_15:.*]] ]
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_section_loop.cond:
// CHECK:   %[[VAL_17:.*]] = icmp ult i32 %[[VAL_13]], %[[VAL_11]]
// CHECK:   br i1 %[[VAL_17]], label %[[VAL_18:.*]], label %[[VAL_19:.*]]
// CHECK: omp_section_loop.body:
// CHECK:   %[[VAL_20:.*]] = add i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   %[[VAL_21:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 0
// CHECK:   switch i32 %[[VAL_22]], label %[[VAL_23:.*]] [
// CHECK:   i32 0, label %[[VAL_24:.*]]
// CHECK:   i32 1, label %[[VAL_25:.*]]
// CHECK:   ]
// CHECK: omp_section_loop.body.case:
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp.section.region:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.case1:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp.section.region3:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.sections.after:
// CHECK:   br label %[[VAL_15]]
// CHECK: omp_section_loop.inc:
// CHECK:   %[[VAL_14]] = add nuw i32 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_12]]
// CHECK: omp_section_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_7]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_sections(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_section_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   store i32 2, ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_7]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_10:.*]] = sub i32 %[[VAL_9]], %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = add i32 %[[VAL_10]], 1
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_section_loop.header:
// CHECK:   %[[VAL_13:.*]] = phi i32 [ 0, %[[VAL_6]] ], [ %[[VAL_14:.*]], %[[VAL_15:.*]] ]
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_section_loop.cond:
// CHECK:   %[[VAL_17:.*]] = icmp ult i32 %[[VAL_13]], %[[VAL_11]]
// CHECK:   br i1 %[[VAL_17]], label %[[VAL_18:.*]], label %[[VAL_19:.*]]
// CHECK: omp_section_loop.body:
// CHECK:   %[[VAL_20:.*]] = add i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   %[[VAL_21:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 0
// CHECK:   switch i32 %[[VAL_22]], label %[[VAL_23:.*]] [
// CHECK:   i32 0, label %[[VAL_24:.*]]
// CHECK:   i32 1, label %[[VAL_25:.*]]
// CHECK:   i32 2, label %[[VAL_26:.*]]
// CHECK:   ]
// CHECK: omp_section_loop.body.case:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.section.region:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.case1:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.section.region3:
// CHECK:   call void @bar(i32 %[[VAL_30:.*]])
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.case4:
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp.section.region6:
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_30]], %[[VAL_34:.*]]
// CHECK:   store i32 %[[VAL_33]], ptr %[[VAL_35:.*]], align 4
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.sections.after:
// CHECK:   br label %[[VAL_15]]
// CHECK: omp_section_loop.inc:
// CHECK:   %[[VAL_14]] = add nuw i32 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_12]]
// CHECK: omp_section_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_7]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_sections_runtime_flags(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_section_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_7]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_10:.*]] = sub i32 %[[VAL_9]], %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = add i32 %[[VAL_10]], 1
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_section_loop.header:
// CHECK:   %[[VAL_13:.*]] = phi i32 [ 0, %[[VAL_6]] ], [ %[[VAL_14:.*]], %[[VAL_15:.*]] ]
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_section_loop.cond:
// CHECK:   %[[VAL_17:.*]] = icmp ult i32 %[[VAL_13]], %[[VAL_11]]
// CHECK:   br i1 %[[VAL_17]], label %[[VAL_18:.*]], label %[[VAL_19:.*]]
// CHECK: omp_section_loop.body:
// CHECK:   %[[VAL_20:.*]] = add i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   %[[VAL_21:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 0
// CHECK:   switch i32 %[[VAL_22]], label %[[VAL_23:.*]] [
// CHECK:   i32 0, label %[[VAL_24:.*]]
// CHECK:   i32 1, label %[[VAL_25:.*]]
// CHECK:   ]
// CHECK: omp_section_loop.body.case:
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp.section.region:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.case1:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp.section.region3:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.sections.after:
// CHECK:   br label %[[VAL_15]]
// CHECK: omp_section_loop.inc:
// CHECK:   %[[VAL_14]] = add nuw i32 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_12]]
// CHECK: omp_section_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_7]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_sections_with_clauses(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_section_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_7]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_10:.*]] = sub i32 %[[VAL_9]], %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = add i32 %[[VAL_10]], 1
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_section_loop.header:
// CHECK:   %[[VAL_13:.*]] = phi i32 [ 0, %[[VAL_6]] ], [ %[[VAL_14:.*]], %[[VAL_15:.*]] ]
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_section_loop.cond:
// CHECK:   %[[VAL_17:.*]] = icmp ult i32 %[[VAL_13]], %[[VAL_11]]
// CHECK:   br i1 %[[VAL_17]], label %[[VAL_18:.*]], label %[[VAL_19:.*]]
// CHECK: omp_section_loop.body:
// CHECK:   %[[VAL_20:.*]] = add i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   %[[VAL_21:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 0
// CHECK:   switch i32 %[[VAL_22]], label %[[VAL_23:.*]] [
// CHECK:   i32 0, label %[[VAL_24:.*]]
// CHECK:   i32 1, label %[[VAL_25:.*]]
// CHECK:   ]
// CHECK: omp_section_loop.body.case:
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp.section.region:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.case1:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp.section.region3:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.sections.after:
// CHECK:   br label %[[VAL_15]]
// CHECK: omp_section_loop.inc:
// CHECK:   %[[VAL_14]] = add nuw i32 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_12]]
// CHECK: omp_section_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @repeated_successor(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i64 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i64 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_9]], i64 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_12:.*]], i64 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i64 %[[VAL_13]], i64 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i64 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i64 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i64 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i64 1, i64 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i64 0, i64 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_24:.*]] = sub i64 %[[VAL_22]], 1
// CHECK:   store i64 %[[VAL_24]], ptr %[[VAL_2]], align 4
// CHECK:   store i64 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_25:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_25]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i64 1, i64 1)
// CHECK:   %[[VAL_26:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i64 %[[VAL_27]], %[[VAL_26]]
// CHECK:   %[[VAL_29:.*]] = add i64 %[[VAL_28]], 1
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_31:.*]] = phi i64 [ 0, %[[VAL_23]] ], [ %[[VAL_32:.*]], %[[VAL_33:.*]] ]
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_35:.*]] = icmp ult i64 %[[VAL_31]], %[[VAL_29]]
// CHECK:   br i1 %[[VAL_35]], label %[[VAL_36:.*]], label %[[VAL_37:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_38:.*]] = add i64 %[[VAL_31]], %[[VAL_26]]
// CHECK:   %[[VAL_39:.*]] = mul i64 %[[VAL_38]], %[[VAL_8]]
// CHECK:   %[[VAL_40:.*]] = add i64 %[[VAL_39]], %[[VAL_13]]
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.loop_nest.region3:
// CHECK:   %[[VAL_42:.*]] = phi i64 [ %[[VAL_12]], %[[VAL_41]] ]
// CHECK:   br label %[[VAL_43:.*]], !llvm_forwarding_branch !{{[0-9]+}}
// CHECK: omp.loop_nest.region2:
// CHECK:   %[[VAL_44:.*]] = phi i64 [ %[[VAL_42]], %[[VAL_45:.*]] ], [ %[[VAL_13]], %[[VAL_41]] ]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br i1 %[[VAL_47:.*]], label %[[VAL_43]], label %[[VAL_45]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_32]] = add nuw i64 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_30]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_25]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_25]])
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @single(
// CHECK:   %[[VAL_0:.*]] = sub i32 %[[VAL_1:.*]], %[[VAL_2:.*]]
// CHECK:   store i32 %[[VAL_0]], ptr %[[VAL_3:.*]], align 4
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_single(ptr @1, i32 %[[VAL_4]])
// CHECK:   %[[VAL_6:.*]] = icmp ne i32 %[[VAL_5]], 0
// CHECK:   br i1 %[[VAL_6]], label %[[VAL_7:.*]], label %[[VAL_8:.*]]
// CHECK: omp_region.body:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.single.region:
// CHECK:   %[[VAL_10:.*]] = add i32 %[[VAL_1]], %[[VAL_2]]
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_region.finalize:
// CHECK:   call void @__kmpc_end_single(ptr @1, i32 %[[VAL_4]])
// CHECK:   br label %[[VAL_8]]
// CHECK: omp_region.end:
// CHECK:   %[[VAL_13:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_13]])
// CHECK:   %[[VAL_14:.*]] = mul i32 %[[VAL_1]], %[[VAL_2]]
// CHECK:   store i32 %[[VAL_14]], ptr %[[VAL_3]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @single_nowait(
// CHECK:   %[[VAL_0:.*]] = sub i32 %[[VAL_1:.*]], %[[VAL_2:.*]]
// CHECK:   store i32 %[[VAL_0]], ptr %[[VAL_3:.*]], align 4
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_single(ptr @1, i32 %[[VAL_4]])
// CHECK:   %[[VAL_6:.*]] = icmp ne i32 %[[VAL_5]], 0
// CHECK:   br i1 %[[VAL_6]], label %[[VAL_7:.*]], label %[[VAL_8:.*]]
// CHECK: omp_region.body:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.single.region:
// CHECK:   %[[VAL_10:.*]] = add i32 %[[VAL_1]], %[[VAL_2]]
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_region.finalize:
// CHECK:   call void @__kmpc_end_single(ptr @1, i32 %[[VAL_4]])
// CHECK:   br label %[[VAL_8]]
// CHECK: omp_region.end:
// CHECK:   %[[VAL_13:.*]] = mul i32 %[[VAL_1]], %[[VAL_2]]
// CHECK:   store i32 %[[VAL_13]], ptr %[[VAL_3]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @single_copyprivate(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   store i32 0, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_single(ptr @1, i32 %[[VAL_1]])
// CHECK:   %[[VAL_3:.*]] = icmp ne i32 %[[VAL_2]], 0
// CHECK:   br i1 %[[VAL_3]], label %[[VAL_4:.*]], label %[[VAL_5:.*]]
// CHECK: omp_region.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.single.region:
// CHECK:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_8:.*]], align 4
// CHECK:   %[[VAL_9:.*]] = add i32 %[[VAL_7]], %[[VAL_7]]
// CHECK:   store i32 %[[VAL_9]], ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_10:.*]] = load float, ptr %[[VAL_11:.*]], align 4
// CHECK:   %[[VAL_12:.*]] = fadd float %[[VAL_10]], %[[VAL_10]]
// CHECK:   store float %[[VAL_12]], ptr %[[VAL_11]], align 4
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp_region.finalize:
// CHECK:   store i32 1, ptr %[[VAL_0]], align 4
// CHECK:   call void @__kmpc_end_single(ptr @1, i32 %[[VAL_1]])
// CHECK:   br label %[[VAL_5]]
// CHECK: omp_region.end:
// CHECK:   %[[VAL_15:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_16:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   call void @__kmpc_copyprivate(ptr @1, i32 %[[VAL_15]], i64 0, ptr %[[VAL_8]], ptr @copy_i32, i32 %[[VAL_16]])
// CHECK:   %[[VAL_17:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_18:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   call void @__kmpc_copyprivate(ptr @1, i32 %[[VAL_17]], i64 0, ptr %[[VAL_11]], ptr @copy_f32, i32 %[[VAL_18]])
// CHECK:   ret void
// CHECK: }
// CHECK: @_QFsubEx = internal global i32 undef
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @_QFsubEx.cache = common global ptr null, align 8
// CHECK-LABEL: define void @omp_threadprivate(
// CHECK:   %[[VAL_0:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_1:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @1, i32 %[[VAL_0]], ptr @_QFsubEx, i64 4, ptr @_QFsubEx.cache)
// CHECK:   store i32 1, ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @omp_threadprivate..omp_par)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.exit:
// CHECK:   store i32 3, ptr %[[VAL_1]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_threadprivate..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region2:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @1, i32 %[[VAL_7]], ptr @_QFsubEx, i64 4, ptr @_QFsubEx.cache)
// CHECK:   store i32 2, ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_detach(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 65, i64 40, i64 0, ptr @omp_task_detach..omp_par)
// CHECK:   %[[VAL_7:.*]] = call ptr @__kmpc_task_allow_completion_event(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   %[[VAL_8:.*]] = ptrtoint ptr %[[VAL_7]] to i64
// CHECK:   store i64 %[[VAL_8]], ptr %[[VAL_9:.*]], align 4
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_detach..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 1, i64 40, i64 0, ptr @omp_task..omp_par)
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = add i32 %[[VAL_3]], %[[VAL_3]]
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_attrs(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 1, i64 40, i64 0, ptr @omp_task_attrs..omp_par)
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_attrs..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_with_deps(
// CHECK:   %[[VAL_0:.*]] = alloca [1 x %[[VAL_1:.*]]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca [1 x %[[VAL_1]]], align 8
// CHECK:   %[[VAL_3:.*]] = alloca [1 x %[[VAL_1]]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca [1 x %[[VAL_1]]], align 8
// CHECK:   %[[VAL_5:.*]] = alloca [1 x %[[VAL_1]]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_12:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_11]], i32 1, i64 40, i64 0, ptr @omp_task_with_deps..omp_par)
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_0]], i64 0, i64 0
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_13]], i32 0, i32 0
// CHECK:   %[[VAL_15:.*]] = ptrtoint ptr %[[VAL_16:.*]] to i64
// CHECK:   store i64 %[[VAL_15]], ptr %[[VAL_14]], align 4
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_13]], i32 0, i32 1
// CHECK:   store i64 8, ptr %[[VAL_17]], align 4
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_13]], i32 0, i32 2
// CHECK:   store i8 1, ptr %[[VAL_18]], align 1
// CHECK:   %[[VAL_19:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_11]], ptr %[[VAL_12]], i32 1, ptr %[[VAL_0]], i32 0, ptr null)
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: task.exit:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp.private.init3:
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp.private.copy2:
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp.task.start1:
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: codeRepl41:
// CHECK:   %[[VAL_25:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_26:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_25]], i32 1, i64 40, i64 0, ptr @omp_task_with_deps..omp_par.1)
// CHECK:   %[[VAL_27:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_2]], i64 0, i64 0
// CHECK:   %[[VAL_28:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_27]], i32 0, i32 0
// CHECK:   %[[VAL_29:.*]] = ptrtoint ptr %[[VAL_16]] to i64
// CHECK:   store i64 %[[VAL_29]], ptr %[[VAL_28]], align 4
// CHECK:   %[[VAL_30:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_27]], i32 0, i32 1
// CHECK:   store i64 8, ptr %[[VAL_30]], align 4
// CHECK:   %[[VAL_31:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_27]], i32 0, i32 2
// CHECK:   store i8 3, ptr %[[VAL_31]], align 1
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_25]], ptr %[[VAL_26]], i32 1, ptr %[[VAL_2]], i32 0, ptr null)
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: task.exit4:
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.private.init13:
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp.private.copy12:
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp.task.start11:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: codeRepl44:
// CHECK:   %[[VAL_38:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_39:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_38]], i32 1, i64 40, i64 0, ptr @omp_task_with_deps..omp_par.2)
// CHECK:   %[[VAL_40:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_3]], i64 0, i64 0
// CHECK:   %[[VAL_41:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_40]], i32 0, i32 0
// CHECK:   %[[VAL_42:.*]] = ptrtoint ptr %[[VAL_16]] to i64
// CHECK:   store i64 %[[VAL_42]], ptr %[[VAL_41]], align 4
// CHECK:   %[[VAL_43:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_40]], i32 0, i32 1
// CHECK:   store i64 8, ptr %[[VAL_43]], align 4
// CHECK:   %[[VAL_44:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_40]], i32 0, i32 2
// CHECK:   store i8 3, ptr %[[VAL_44]], align 1
// CHECK:   %[[VAL_45:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_38]], ptr %[[VAL_39]], i32 1, ptr %[[VAL_3]], i32 0, ptr null)
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: task.exit14:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.private.init23:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp.private.copy22:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.task.start21:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: codeRepl47:
// CHECK:   %[[VAL_51:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_52:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_51]], i32 1, i64 40, i64 0, ptr @omp_task_with_deps..omp_par.3)
// CHECK:   %[[VAL_53:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_4]], i64 0, i64 0
// CHECK:   %[[VAL_54:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_53]], i32 0, i32 0
// CHECK:   %[[VAL_55:.*]] = ptrtoint ptr %[[VAL_16]] to i64
// CHECK:   store i64 %[[VAL_55]], ptr %[[VAL_54]], align 4
// CHECK:   %[[VAL_56:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_53]], i32 0, i32 1
// CHECK:   store i64 8, ptr %[[VAL_56]], align 4
// CHECK:   %[[VAL_57:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_53]], i32 0, i32 2
// CHECK:   store i8 4, ptr %[[VAL_57]], align 1
// CHECK:   %[[VAL_58:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_51]], ptr %[[VAL_52]], i32 1, ptr %[[VAL_4]], i32 0, ptr null)
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: task.exit24:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.private.init33:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.private.copy32:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: omp.task.start31:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: codeRepl50:
// CHECK:   %[[VAL_64:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_65:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_64]], i32 1, i64 40, i64 0, ptr @omp_task_with_deps..omp_par.4)
// CHECK:   %[[VAL_66:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_5]], i64 0, i64 0
// CHECK:   %[[VAL_67:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_66]], i32 0, i32 0
// CHECK:   %[[VAL_68:.*]] = ptrtoint ptr %[[VAL_16]] to i64
// CHECK:   store i64 %[[VAL_68]], ptr %[[VAL_67]], align 4
// CHECK:   %[[VAL_69:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_66]], i32 0, i32 1
// CHECK:   store i64 8, ptr %[[VAL_69]], align 4
// CHECK:   %[[VAL_70:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_66]], i32 0, i32 2
// CHECK:   store i8 8, ptr %[[VAL_70]], align 1
// CHECK:   %[[VAL_71:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_64]], ptr %[[VAL_65]], i32 1, ptr %[[VAL_5]], i32 0, ptr null)
// CHECK:   br label %[[VAL_72:.*]]
// CHECK: task.exit34:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_with_deps..omp_par.4(
// CHECK: task.alloca36:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body35:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region38:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont37:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit34.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_with_deps..omp_par.3(
// CHECK: task.alloca26:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body25:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region28:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont27:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit24.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_with_deps..omp_par.2(
// CHECK: task.alloca16:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body15:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region18:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont17:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit14.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_with_deps..omp_par.1(
// CHECK: task.alloca6:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body5:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region8:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit4.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_with_deps..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = add i32 %[[VAL_3]], %[[VAL_3]]
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_task_with_size(
// CHECK:   %[[VAL_0:.*]] = alloca [1 x %[[VAL_1:.*]]], align 8
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.task.start:
// CHECK:   %[[VAL_6:.*]] = zext i32 %[[VAL_7:.*]] to i64
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_9:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_10:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_9]], i32 1, i64 40, i64 0, ptr @omp_task_with_size..omp_par)
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_0]], i64 0, i64 0
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_11]], i32 0, i32 0
// CHECK:   %[[VAL_13:.*]] = ptrtoint ptr %[[VAL_14:.*]] to i64
// CHECK:   store i64 %[[VAL_13]], ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_11]], i32 0, i32 1
// CHECK:   store i64 %[[VAL_6]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_11]], i32 0, i32 2
// CHECK:   store i8 1, ptr %[[VAL_16]], align 1
// CHECK:   %[[VAL_17:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_9]], ptr %[[VAL_10]], i32 1, ptr %[[VAL_0]], i32 0, ptr null)
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_with_size..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: target triple = "x86_64-unknown-linux-gnu"
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i32, i32, ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = sub i32 %[[VAL_2:.*]], %[[VAL_3:.*]]
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_10:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i32 %[[VAL_2]], ptr %[[VAL_10]], align 4
// CHECK:   %[[VAL_11:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i32 %[[VAL_3]], ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_12:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_4]], ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_14:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_13]], i32 1, i64 40, i64 16, ptr @omp_task..omp_par)
// CHECK:   %[[VAL_15:.*]] = load ptr, ptr %[[VAL_14]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_15]], ptr align 1 %[[VAL_0]], i64 16, i1 false)
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_13]], ptr %[[VAL_14]])
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: task.exit:
// CHECK:   %[[VAL_18:.*]] = mul i32 %[[VAL_2]], %[[VAL_3]]
// CHECK:   store i32 %[[VAL_18]], ptr %[[VAL_4]], align 4
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task..omp_par(
// CHECK: task.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   %[[VAL_7:.*]] = load ptr, ptr %[[VAL_6]], align 8, !align !1
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_10:.*]] = add i32 %[[VAL_3]], %[[VAL_5]]
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @par_task_(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_7:.*]], ptr %[[VAL_6]], align 8
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_9:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_8]], i32 1, i64 40, i64 8, ptr @par_task_..omp_par.1)
// CHECK:   %[[VAL_10:.*]] = load ptr, ptr %[[VAL_9]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_10]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_8]], ptr %[[VAL_9]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @par_task_..omp_par.1(
// CHECK: task.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load ptr, ptr %[[VAL_2]], align 8, !align !1
// CHECK:   %[[VAL_4:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_9:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_4]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_3]], ptr %[[VAL_9]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @par_task_..omp_par, ptr %[[VAL_4]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @par_task_..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region2:
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @task(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_3:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.private.copy1:
// CHECK:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_8:.*]], align 4
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_11:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_3]], ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_13:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_12]], i32 1, i64 40, i64 8, ptr @task..omp_par)
// CHECK:   %[[VAL_14:.*]] = load ptr, ptr %[[VAL_13]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_14]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_15:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_12]], ptr %[[VAL_13]])
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @task..omp_par(
// CHECK: task.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load ptr, ptr %[[VAL_2]], align 8, !align !1
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: task.body:
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.task.region:
// CHECK:   call void @foo(ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   call void @destroy(ptr %[[VAL_5]])
// CHECK:   tail call void @free(ptr %[[VAL_3]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_taskgroup(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_1]])
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.taskgroup.region:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: taskgroup.exit:
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_1]])
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_taskgroup_task(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i32, i32, ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_2]])
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.taskgroup.region2:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.taskgroup.region:
// CHECK:   %[[VAL_6:.*]] = alloca i8, align 1
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: taskgroup.exit:
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_2]])
// CHECK:   ret void
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_12:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_13:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_12]], i32 1, i64 40, i64 0, ptr @omp_taskgroup_task..omp_par)
// CHECK:   %[[VAL_14:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_12]], ptr %[[VAL_13]])
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: task.exit:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.private.init6:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.private.copy5:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.task.start4:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: codeRepl15:
// CHECK:   %[[VAL_20:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i32 %[[VAL_21:.*]], ptr %[[VAL_20]], align 4
// CHECK:   %[[VAL_22:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i32 %[[VAL_23:.*]], ptr %[[VAL_22]], align 4
// CHECK:   %[[VAL_24:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_25:.*]], ptr %[[VAL_24]], align 8
// CHECK:   %[[VAL_26:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_27:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_26]], i32 1, i64 40, i64 16, ptr @omp_taskgroup_task..omp_par.1)
// CHECK:   %[[VAL_28:.*]] = load ptr, ptr %[[VAL_27]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_28]], ptr align 1 %[[VAL_0]], i64 16, i1 false)
// CHECK:   %[[VAL_29:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_26]], ptr %[[VAL_27]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: task.exit7:
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: }
// CHECK-LABEL: define internal void @omp_taskgroup_task..omp_par.1(
// CHECK: task.alloca9:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i32, i32, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   %[[VAL_7:.*]] = load ptr, ptr %[[VAL_6]], align 8, !align !1
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.body8:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.task.region11:
// CHECK:   call void @bar(i32 %[[VAL_3]], i32 %[[VAL_5]], ptr %[[VAL_7]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont10:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: task.exit7.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_taskgroup_task..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @_QPomp_task_priority(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_7]], i32 33, i64 40, i64 0, ptr @_QPomp_task_priority..omp_par)
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds {{[{]}} ptr }, ptr %[[VAL_8]], i32 0, i32 0
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds {{[{]}} ptr, ptr, i32, ptr, ptr }, ptr %[[VAL_9]], i32 0, i32 4
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds {{[{]}} ptr, ptr }, ptr %[[VAL_10]], i32 0, i32 0
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_12:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_7]], ptr %[[VAL_8]])
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: task.exit:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.private.init3:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.private.copy2:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.task.start1:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: codeRepl11:
// CHECK:   %[[VAL_18:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_19:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_18]], i32 35, i64 40, i64 0, ptr @_QPomp_task_priority..omp_par.1)
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds {{[{]}} ptr }, ptr %[[VAL_19]], i32 0, i32 0
// CHECK:   %[[VAL_21:.*]] = getelementptr inbounds {{[{]}} ptr, ptr, i32, ptr, ptr }, ptr %[[VAL_20]], i32 0, i32 4
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds {{[{]}} ptr, ptr }, ptr %[[VAL_21]], i32 0, i32 0
// CHECK:   store i32 4, ptr %[[VAL_22]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_18]], ptr %[[VAL_19]])
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: task.exit4:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPomp_task_priority..omp_par.1(
// CHECK: task.alloca6:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body5:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region8:
// CHECK:   call void @test_02()
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit4.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPomp_task_priority..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   call void @test_01()
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_opaque_pointers(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = load atomic i32, ptr %[[VAL_2:.*]] monotonic, align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_3:.*]], align 4
// CHECK:   %[[VAL_4:.*]] = atomicrmw add ptr %[[VAL_3]], i32 %[[VAL_5:.*]] acq_rel, align 4
// CHECK:   %[[VAL_6:.*]] = add i32 %[[VAL_4]], %[[VAL_5]]
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_2]], align 4
// CHECK:   call void @__kmpc_flush(ptr @1)
// CHECK:   ret void
// CHECK: }
// CHECK: @__omp_rtl_debug_kind = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_assume_teams_oversubscription = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_assume_threads_oversubscription = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_assume_no_thread_state = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_assume_no_nested_parallelism = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_debug_kind = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_teams_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_threads_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_thread_state = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_nested_parallelism = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_debug_kind = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_teams_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_threads_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_thread_state = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_nested_parallelism = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_debug_kind = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_teams_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_threads_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_thread_state = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_nested_parallelism = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_debug_kind = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_teams_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_threads_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_thread_state = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_nested_parallelism = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_debug_kind = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_teams_oversubscription = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_assume_threads_oversubscription = weak_odr hidden constant i32 0
// CHECK: @__omp_rtl_assume_no_thread_state = weak_odr hidden constant i32 1
// CHECK: @__omp_rtl_assume_no_nested_parallelism = weak_odr hidden constant i32 0
// CHECK-LABEL: define void @filter_nohost(
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @filter_host(
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @filter_nohost(
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @filter_host(
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @filter_nohost(
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @filter_nohost(
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_untied(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 0, i64 40, i64 0, ptr @omp_task_untied..omp_par)
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_untied..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_mergeable(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 5, i64 40, i64 0, ptr @omp_task_mergeable..omp_par)
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_mergeable..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_final(
// CHECK:   call void @foo_before()
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_7:.*]], i32 2, i32 0
// CHECK:   %[[VAL_8:.*]] = or i32 %[[VAL_6]], 1
// CHECK:   %[[VAL_9:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 %[[VAL_8]], i64 40, i64 0, ptr @omp_task_final..omp_par)
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_9]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: task.exit:
// CHECK:   call void @foo_after()
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_final..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @omp_task_if(
// CHECK:   call void @foo_before()
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_5]], i32 1, i64 40, i64 0, ptr @omp_task_if..omp_par)
// CHECK:   br i1 %[[VAL_7:.*]], label %[[VAL_8:.*]], label %[[VAL_9:.*]]
// CHECK: 3:
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: 5:
// CHECK:   call void @__kmpc_omp_task_begin_if0(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   call void @omp_task_if..omp_par(i32 %[[VAL_5]])
// CHECK:   call void @__kmpc_omp_task_complete_if0(ptr @1, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_11]]
// CHECK: 6:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: if.end:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: task.exit:
// CHECK:   call void @foo_after()
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_task_if..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @distribute(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_5:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_3]], align 4
// CHECK:   store i64 31, ptr %[[VAL_4]], align 4
// CHECK:   store i64 1, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_10]], i32 92, ptr %[[VAL_2]], ptr %[[VAL_3]], ptr %[[VAL_4]], ptr %[[VAL_5]], i64 1, i64 1)
// CHECK:   %[[VAL_11:.*]] = load i64, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_12:.*]] = load i64, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_13:.*]] = sub i64 %[[VAL_12]], %[[VAL_11]]
// CHECK:   %[[VAL_14:.*]] = add i64 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_16:.*]] = phi i64 [ 0, %[[VAL_9]] ], [ %[[VAL_17:.*]], %[[VAL_18:.*]] ]
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_20:.*]] = icmp ult i64 %[[VAL_16]], %[[VAL_14]]
// CHECK:   br i1 %[[VAL_20]], label %[[VAL_21:.*]], label %[[VAL_22:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_23:.*]] = add i64 %[[VAL_16]], %[[VAL_11]]
// CHECK:   %[[VAL_24:.*]] = mul i64 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = add i64 %[[VAL_24]], 10
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_18]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_17]] = add nuw i64 %[[VAL_16]], 1
// CHECK:   br label %[[VAL_15]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_10]])
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: distribute.exit:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @distribute_wsloop(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 3, ptr @distribute_wsloop..omp_par, i32 %[[VAL_3:.*]], i32 %[[VAL_4:.*]], i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca4:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_18:.*]], 0
// CHECK:   %[[VAL_19:.*]] = sub i32 0, %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_19]], i32 %[[VAL_18]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_22:.*]], i32 %[[VAL_23:.*]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_23]], i32 %[[VAL_22]]
// CHECK:   %[[VAL_25:.*]] = sub nsw i32 %[[VAL_24]], %[[VAL_21]]
// CHECK:   %[[VAL_26:.*]] = icmp sle i32 %[[VAL_24]], %[[VAL_21]]
// CHECK:   %[[VAL_27:.*]] = sub i32 %[[VAL_25]], 1
// CHECK:   %[[VAL_28:.*]] = udiv i32 %[[VAL_27]], %[[VAL_20]]
// CHECK:   %[[VAL_29:.*]] = add i32 %[[VAL_28]], 1
// CHECK:   %[[VAL_30:.*]] = icmp ule i32 %[[VAL_25]], %[[VAL_20]]
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i32 1, i32 %[[VAL_29]]
// CHECK:   %[[VAL_32:.*]] = select i1 %[[VAL_26]], i32 0, i32 %[[VAL_31]]
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = sub i32 %[[VAL_32]], 1
// CHECK:   store i32 %[[VAL_34]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_35:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_35]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_36:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_37:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_38:.*]] = sub i32 %[[VAL_37]], %[[VAL_36]]
// CHECK:   %[[VAL_39:.*]] = add i32 %[[VAL_38]], 1
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_41:.*]] = phi i32 [ 0, %[[VAL_33]] ], [ %[[VAL_42:.*]], %[[VAL_43:.*]] ]
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_45:.*]] = icmp ult i32 %[[VAL_41]], %[[VAL_39]]
// CHECK:   br i1 %[[VAL_45]], label %[[VAL_46:.*]], label %[[VAL_47:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_35]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_35]])
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_56:.*]] = add i32 %[[VAL_41]], %[[VAL_36]]
// CHECK:   %[[VAL_57:.*]] = mul i32 %[[VAL_56]], %[[VAL_18]]
// CHECK:   %[[VAL_58:.*]] = add i32 %[[VAL_57]], %[[VAL_23]]
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_43]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_42]] = add nuw i32 %[[VAL_41]], 1
// CHECK:   br label %[[VAL_40]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @nested_task_with_deps(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca [1 x %[[VAL_3:.*]]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_7:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_2]], ptr %[[VAL_7]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @nested_task_with_deps..omp_par.1, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @nested_task_with_deps..omp_par.1(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.private.init2:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_17:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_16]], i32 1, i64 40, i64 0, ptr @nested_task_with_deps..omp_par)
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds [1 x %[[VAL_19:.*]]], ptr %[[VAL_2]], i64 0, i64 0
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_19]], ptr %[[VAL_18]], i32 0, i32 0
// CHECK:   %[[VAL_21:.*]] = ptrtoint ptr %[[VAL_7]] to i64
// CHECK:   store i64 %[[VAL_21]], ptr %[[VAL_20]], align 4
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds nuw %[[VAL_19]], ptr %[[VAL_18]], i32 0, i32 1
// CHECK:   store i64 8, ptr %[[VAL_22]], align 4
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_19]], ptr %[[VAL_18]], i32 0, i32 2
// CHECK:   store i8 3, ptr %[[VAL_23]], align 1
// CHECK:   %[[VAL_24:.*]] = call i32 @__kmpc_omp_task_with_deps(ptr @1, i32 %[[VAL_16]], ptr %[[VAL_17]], i32 1, ptr %[[VAL_2]], i32 0, ptr null)
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: task.exit:
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @nested_task_with_deps..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
