// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {dlti.dl_spec = #dlti.dl_spec<!llvm.ptr<270> = dense<32> : vector<4xi64>, !llvm.ptr<271> = dense<32> : vector<4xi64>, !llvm.ptr<272> = dense<64> : vector<4xi64>, i64 = dense<64> : vector<2xi64>, i128 = dense<128> : vector<2xi64>, f80 = dense<128> : vector<2xi64>, !llvm.ptr = dense<64> : vector<4xi64>, i1 = dense<8> : vector<2xi64>, i8 = dense<8> : vector<2xi64>, i16 = dense<16> : vector<2xi64>, i32 = dense<32> : vector<2xi64>, f16 = dense<16> : vector<2xi64>, f64 = dense<64> : vector<2xi64>, f128 = dense<128> : vector<2xi64>, "dlti.endianness" = "little", "dlti.mangling_mode" = "e", "dlti.legal_int_widths" = array<i32: 8, 16, 32, 64>, "dlti.stack_alignment" = 128 : i64>, fir.defaultkind = "a1c4d8i4l4r4", fir.kindmap = "", llvm.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128", llvm.ident = "flang version 22.0.0 (git@github.com:mjklemm/llvm-project.git 5d9164c24a474793ab325116c5f782dce0577574)", llvm.target_triple = "x86_64-unknown-linux-gnu", omp.is_gpu = false, omp.is_target_device = false, omp.requires = #omp<clause_requires none>, omp.target_triples = [], omp.version = #omp.version<version = 31>} {
  omp.private {type = private} @_QFnum_threads_arg_2Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_threads_const_2Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_threads_arg_8Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_threads_const_8Ei_private_i32 : i32
  omp.private {type = private} @_QFthread_limit_arg_2Ei_private_i32 : i32
  omp.private {type = private} @_QFthread_limit_const_2Ei_private_i32 : i32
  omp.private {type = private} @_QFthread_limit_arg_8Ei_private_i32 : i32
  omp.private {type = private} @_QFthread_limit_const_8Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_teams_arg_2Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_teams_const_2Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_teams_arg_8Ei_private_i32 : i32
  omp.private {type = private} @_QFnum_teams_const_8Ei_private_i32 : i32

  llvm.func @_QPnum_teams_const_8(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(137 : i64) : i64
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %4 = llvm.load %arg0 : !llvm.ptr -> i32
    %5 = omp.map.info var_ptr(%3 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %6 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg1, %4 -> %arg2, %0 -> %arg3, %1 -> %arg4 : i32, i32, i32, i64) map_entries(%5 -> %arg5, %6 -> %arg6 : !llvm.ptr, !llvm.ptr) {
      omp.teams num_teams( to %arg4 : i64) {
        omp.distribute private(@_QFnum_teams_const_8Ei_private_i32 %arg5 -> %arg7 : !llvm.ptr) {
          omp.loop_nest (%arg8) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
            llvm.store %arg8, %arg7 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_teams_arg_8(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}, %arg1: !llvm.ptr {fir.bindc_name = "t", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg0 : !llvm.ptr -> i32
    %4 = llvm.load %arg1 : !llvm.ptr -> i64
    %5 = omp.map.info var_ptr(%arg1 : !llvm.ptr, i64) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "t"}
    %6 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %7 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg2, %3 -> %arg3, %0 -> %arg4, %4 -> %arg5 : i32, i32, i32, i64) map_entries(%5 -> %arg6, %6 -> %arg7, %7 -> %arg8 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      omp.teams num_teams( to %arg5 : i64) {
        omp.distribute private(@_QFnum_teams_arg_8Ei_private_i32 %arg7 -> %arg9 : !llvm.ptr) {
          omp.loop_nest (%arg10) : i32 = (%arg2) to (%arg3) inclusive step (%arg4) {
            llvm.store %arg10, %arg9 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_teams_const_2(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(137 : i16) : i16
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %4 = llvm.load %arg0 : !llvm.ptr -> i32
    %5 = omp.map.info var_ptr(%3 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %6 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg1, %4 -> %arg2, %0 -> %arg3, %1 -> %arg4 : i32, i32, i32, i16) map_entries(%5 -> %arg5, %6 -> %arg6 : !llvm.ptr, !llvm.ptr) {
      omp.teams num_teams( to %arg4 : i16) {
        omp.distribute private(@_QFnum_teams_const_2Ei_private_i32 %arg5 -> %arg7 : !llvm.ptr) {
          omp.loop_nest (%arg8) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
            llvm.store %arg8, %arg7 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_teams_arg_2(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}, %arg1: !llvm.ptr {fir.bindc_name = "t", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg0 : !llvm.ptr -> i32
    %4 = llvm.load %arg1 : !llvm.ptr -> i16
    %5 = omp.map.info var_ptr(%arg1 : !llvm.ptr, i16) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "t"}
    %6 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %7 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg2, %3 -> %arg3, %0 -> %arg4, %4 -> %arg5 : i32, i32, i32, i16) map_entries(%5 -> %arg6, %6 -> %arg7, %7 -> %arg8 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      omp.teams num_teams( to %arg5 : i16) {
        omp.distribute private(@_QFnum_teams_arg_2Ei_private_i32 %arg7 -> %arg9 : !llvm.ptr) {
          omp.loop_nest (%arg10) : i32 = (%arg2) to (%arg3) inclusive step (%arg4) {
            llvm.store %arg10, %arg9 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPthread_limit_const_8(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg0 : !llvm.ptr -> i32
    %4 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %5 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg1, %3 -> %arg2, %0 -> %arg3 : i32, i32, i32) map_entries(%4 -> %arg4, %5 -> %arg5 : !llvm.ptr, !llvm.ptr) {
      %6 = llvm.mlir.constant(137 : i64) : i64
      omp.teams thread_limit(%6 : i64) {
        omp.distribute private(@_QFthread_limit_const_8Ei_private_i32 %arg4 -> %arg6 : !llvm.ptr) {
          omp.loop_nest (%arg7) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
            llvm.store %arg7, %arg6 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPthread_limit_arg_8(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}, %arg1: !llvm.ptr {fir.bindc_name = "t", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg0 : !llvm.ptr -> i32
    %4 = omp.map.info var_ptr(%arg1 : !llvm.ptr, i64) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "t"}
    %5 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %6 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg2, %3 -> %arg3, %0 -> %arg4 : i32, i32, i32) map_entries(%4 -> %arg5, %5 -> %arg6, %6 -> %arg7 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      %7 = llvm.load %arg5 : !llvm.ptr -> i64
      omp.teams thread_limit(%7 : i64) {
        omp.distribute private(@_QFthread_limit_arg_8Ei_private_i32 %arg6 -> %arg8 : !llvm.ptr) {
          omp.loop_nest (%arg9) : i32 = (%arg2) to (%arg3) inclusive step (%arg4) {
            llvm.store %arg9, %arg8 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPthread_limit_const_2(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg0 : !llvm.ptr -> i32
    %4 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %5 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg1, %3 -> %arg2, %0 -> %arg3 : i32, i32, i32) map_entries(%4 -> %arg4, %5 -> %arg5 : !llvm.ptr, !llvm.ptr) {
      %6 = llvm.mlir.constant(137 : i16) : i16
      omp.teams thread_limit(%6 : i16) {
        omp.distribute private(@_QFthread_limit_const_2Ei_private_i32 %arg4 -> %arg6 : !llvm.ptr) {
          omp.loop_nest (%arg7) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
            llvm.store %arg7, %arg6 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPthread_limit_arg_2(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}, %arg1: !llvm.ptr {fir.bindc_name = "t", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg0 : !llvm.ptr -> i32
    %4 = omp.map.info var_ptr(%arg1 : !llvm.ptr, i16) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "t"}
    %5 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %6 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg2, %3 -> %arg3, %0 -> %arg4 : i32, i32, i32) map_entries(%4 -> %arg5, %5 -> %arg6, %6 -> %arg7 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      %7 = llvm.load %arg5 : !llvm.ptr -> i16
      omp.teams thread_limit(%7 : i16) {
        omp.distribute private(@_QFthread_limit_arg_2Ei_private_i32 %arg6 -> %arg8 : !llvm.ptr) {
          omp.loop_nest (%arg9) : i32 = (%arg2) to (%arg3) inclusive step (%arg4) {
            llvm.store %arg9, %arg8 : i32, !llvm.ptr
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_threads_const_8(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(137 : i64) : i64
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %4 = llvm.load %arg0 : !llvm.ptr -> i32
    %5 = omp.map.info var_ptr(%3 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %6 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%1 -> %arg1, %4 -> %arg2, %1 -> %arg3, %0 -> %arg4 : i32, i32, i32, i64) map_entries(%5 -> %arg5, %6 -> %arg6 : !llvm.ptr, !llvm.ptr) {
      omp.teams {
        omp.parallel num_threads(%arg4 : i64) private(@_QFnum_threads_const_8Ei_private_i32 %arg5 -> %arg7 : !llvm.ptr) {
          omp.distribute {
            omp.wsloop {
              omp.loop_nest (%arg8) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
                llvm.store %arg8, %arg7 : i32, !llvm.ptr
                omp.yield
              }
            } {omp.composite}
          } {omp.composite}
          omp.terminator
        } {omp.composite}
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_threads_arg_8(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i64 {bindc_name = "t"} : (i64) -> !llvm.ptr
    %3 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %4 = llvm.load %2 : !llvm.ptr -> i64
    %5 = llvm.load %arg0 : !llvm.ptr -> i32
    %6 = omp.map.info var_ptr(%2 : !llvm.ptr, i64) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "t"}
    %7 = omp.map.info var_ptr(%3 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %8 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg1, %5 -> %arg2, %0 -> %arg3, %4 -> %arg4 : i32, i32, i32, i64) map_entries(%6 -> %arg5, %7 -> %arg6, %8 -> %arg7 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      omp.teams {
        omp.parallel num_threads(%arg4 : i64) private(@_QFnum_threads_arg_8Ei_private_i32 %arg6 -> %arg8 : !llvm.ptr) {
          omp.distribute {
            omp.wsloop {
              omp.loop_nest (%arg9) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
                llvm.store %arg9, %arg8 : i32, !llvm.ptr
                omp.yield
              }
            } {omp.composite}
          } {omp.composite}
          omp.terminator
        } {omp.composite}
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_threads_const_2(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(137 : i16) : i16
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %4 = llvm.load %arg0 : !llvm.ptr -> i32
    %5 = omp.map.info var_ptr(%3 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %6 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%1 -> %arg1, %4 -> %arg2, %1 -> %arg3, %0 -> %arg4 : i32, i32, i32, i16) map_entries(%5 -> %arg5, %6 -> %arg6 : !llvm.ptr, !llvm.ptr) {
      omp.teams {
        omp.parallel num_threads(%arg4 : i16) private(@_QFnum_threads_const_2Ei_private_i32 %arg5 -> %arg7 : !llvm.ptr) {
          omp.distribute {
            omp.wsloop {
              omp.loop_nest (%arg8) : i32 = (%arg1) to (%arg2) inclusive step (%arg3) {
                llvm.store %arg8, %arg7 : i32, !llvm.ptr
                omp.yield
              }
            } {omp.composite}
          } {omp.composite}
          omp.terminator
        } {omp.composite}
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }

  llvm.func @_QPnum_threads_arg_2(%arg0: !llvm.ptr {fir.bindc_name = "n", llvm.noalias, llvm.nocapture}, %arg1: !llvm.ptr {fir.bindc_name = "t", llvm.noalias, llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    %1 = llvm.mlir.constant(1 : i64) : i64
    %2 = llvm.alloca %1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %3 = llvm.load %arg1 : !llvm.ptr -> i16
    %4 = llvm.load %arg0 : !llvm.ptr -> i32
    %5 = omp.map.info var_ptr(%arg1 : !llvm.ptr, i16) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "t"}
    %6 = omp.map.info var_ptr(%2 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %7 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target host_eval(%0 -> %arg2, %4 -> %arg3, %0 -> %arg4, %3 -> %arg5 : i32, i32, i32, i16) map_entries(%5 -> %arg6, %6 -> %arg7, %7 -> %arg8 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      omp.teams {
        omp.parallel num_threads(%arg5 : i16) private(@_QFnum_threads_arg_2Ei_private_i32 %arg7 -> %arg9 : !llvm.ptr) {
          omp.distribute {
            omp.wsloop {
              omp.loop_nest (%arg10) : i32 = (%arg2) to (%arg3) inclusive step (%arg4) {
                llvm.store %arg10, %arg9 : i32, !llvm.ptr
                omp.yield
              }
            } {omp.composite}
          } {omp.composite}
          omp.terminator
        } {omp.composite}
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}

// CHECK: target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
// CHECK: target triple = "x86_64-unknown-linux-gnu"
// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @2 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @3 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @2 }, align 8
// CHECK: @4 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @5 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @6 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @7 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @8 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @9 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @10 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @11 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @12 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @13 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @14 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @15 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @16 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @17 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @18 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @19 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @20 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @21 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @22 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @23 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @24 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @2 }, align 8
// CHECK: @25 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @26 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @27 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @28 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @29 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @30 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @31 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @32 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK-LABEL: define void @_QPnum_teams_const_8(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_10:.*]] = sub nuw i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_10]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 0, i32 %[[VAL_13]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_8_l{{.*}}(i32 %[[VAL_3]], i64 %[[VAL_7]], i64 %[[VAL_9]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_teams_arg_8(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   %[[VAL_6:.*]] = load i64, ptr %[[VAL_7:.*]], align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_7]], align 8
// CHECK:   store i64 %[[VAL_9]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_11:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_12:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_13:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   store i32 %[[VAL_13]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_14:.*]] = load i64, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_15:.*]] = sub nuw i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ult i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_17:.*]] = udiv i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = add i32 %[[VAL_17]], 1
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_18]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_8_l{{.*}}(i32 %[[VAL_4]], i64 %[[VAL_6]], i64 %[[VAL_10]], i64 %[[VAL_12]], i64 %[[VAL_14]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_teams_const_2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_10:.*]] = sub nuw i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_10]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 0, i32 %[[VAL_13]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_2_l{{.*}}(i32 %[[VAL_3]], i64 %[[VAL_7]], i64 %[[VAL_9]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_teams_arg_2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   %[[VAL_6:.*]] = load i16, ptr %[[VAL_7:.*]], align 2
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_9:.*]] = load i16, ptr %[[VAL_7]], align 2
// CHECK:   store i16 %[[VAL_9]], ptr %[[VAL_0]], align 2
// CHECK:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_11:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_12:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_13:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   store i32 %[[VAL_13]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_14:.*]] = load i64, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_15:.*]] = sub nuw i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ult i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_17:.*]] = udiv i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = add i32 %[[VAL_17]], 1
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_18]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_2_l{{.*}}(i32 %[[VAL_4]], i16 %[[VAL_6]], i64 %[[VAL_10]], i64 %[[VAL_12]], i64 %[[VAL_14]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPthread_limit_const_8(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_10:.*]] = sub nuw i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_10]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 0, i32 %[[VAL_13]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_8_l{{.*}}(i32 %[[VAL_3]], i64 %[[VAL_7]], i64 %[[VAL_9]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPthread_limit_arg_8(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_8:.*]], align 8
// CHECK:   store i64 %[[VAL_7]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_11:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_12:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   store i32 %[[VAL_12]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_13:.*]] = load i64, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_14:.*]] = sub nuw i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_15:.*]] = icmp ult i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_16:.*]] = udiv i32 %[[VAL_14]], 1
// CHECK:   %[[VAL_17:.*]] = add i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_18:.*]] = select i1 %[[VAL_15]], i32 0, i32 %[[VAL_17]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_8_l{{.*}}(i32 %[[VAL_4]], i64 %[[VAL_9]], i64 %[[VAL_11]], i64 %[[VAL_13]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPthread_limit_const_2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_10:.*]] = sub nuw i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_10]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 0, i32 %[[VAL_13]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_2_l{{.*}}(i32 %[[VAL_3]], i64 %[[VAL_7]], i64 %[[VAL_9]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPthread_limit_arg_2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_7:.*]] = load i16, ptr %[[VAL_8:.*]], align 2
// CHECK:   store i16 %[[VAL_7]], ptr %[[VAL_0]], align 2
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_11:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_12:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   store i32 %[[VAL_12]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_13:.*]] = load i64, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_14:.*]] = sub nuw i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_15:.*]] = icmp ult i32 %[[VAL_4]], 1
// CHECK:   %[[VAL_16:.*]] = udiv i32 %[[VAL_14]], 1
// CHECK:   %[[VAL_17:.*]] = add i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_18:.*]] = select i1 %[[VAL_15]], i32 0, i32 %[[VAL_17]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_2_l{{.*}}(i32 %[[VAL_4]], i64 %[[VAL_9]], i64 %[[VAL_11]], i64 %[[VAL_13]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_threads_const_8(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_10:.*]] = sub nuw i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_10]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 0, i32 %[[VAL_13]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_8_l{{.*}}(i32 %[[VAL_3]], i64 %[[VAL_7]], i64 %[[VAL_9]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_threads_arg_8(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i64, i64 1, align 8
// CHECK:   %[[VAL_4:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_5:.*]] = load i64, ptr %[[VAL_3]], align 8
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_3]], align 8
// CHECK:   store i64 %[[VAL_9]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_11:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_12:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_13:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   store i32 %[[VAL_13]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_14:.*]] = load i64, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_15:.*]] = sub nuw i32 %[[VAL_6]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ult i32 %[[VAL_6]], 1
// CHECK:   %[[VAL_17:.*]] = udiv i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = add i32 %[[VAL_17]], 1
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_18]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_8_l{{.*}}(i32 %[[VAL_6]], i64 %[[VAL_5]], i64 %[[VAL_10]], i64 %[[VAL_12]], i64 %[[VAL_14]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_threads_const_2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_10:.*]] = sub nuw i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_3]], 1
// CHECK:   %[[VAL_12:.*]] = udiv i32 %[[VAL_10]], 1
// CHECK:   %[[VAL_13:.*]] = add i32 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 0, i32 %[[VAL_13]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_2_l{{.*}}(i32 %[[VAL_3]], i64 %[[VAL_7]], i64 %[[VAL_9]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPnum_threads_arg_2(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = load i16, ptr %[[VAL_5:.*]], align 2
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_9:.*]] = load i16, ptr %[[VAL_5]], align 2
// CHECK:   store i16 %[[VAL_9]], ptr %[[VAL_0]], align 2
// CHECK:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_11:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_12:.*]] = load i64, ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_13:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   store i32 %[[VAL_13]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_14:.*]] = load i64, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_15:.*]] = sub nuw i32 %[[VAL_6]], 1
// CHECK:   %[[VAL_16:.*]] = icmp ult i32 %[[VAL_6]], 1
// CHECK:   %[[VAL_17:.*]] = udiv i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = add i32 %[[VAL_17]], 1
// CHECK:   %[[VAL_19:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_18]]
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_2_l{{.*}}(i32 %[[VAL_6]], i16 %[[VAL_4]], i64 %[[VAL_10]], i64 %[[VAL_12]], i64 %[[VAL_14]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_8_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_7]], i32 137, i32 0)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_8_l{{.*}}..omp_par, i32 %[[VAL_9:.*]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_8_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_8_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_9:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   %[[VAL_10:.*]] = trunc i64 %[[VAL_11:.*]] to i32
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_9]], i32 %[[VAL_10]], i32 0)
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_8_l{{.*}}..omp_par, i32 %[[VAL_13:.*]])
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_8_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_2_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_7]], i32 137, i32 0)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_2_l{{.*}}..omp_par, i32 %[[VAL_9:.*]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_const_2_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_2_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_9:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   %[[VAL_10:.*]] = sext i16 %[[VAL_11:.*]] to i32
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_9]], i32 %[[VAL_10]], i32 0)
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_2_l{{.*}}..omp_par, i32 %[[VAL_13:.*]])
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_teams_arg_2_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_8_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_7]], i32 0, i32 137)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_8_l{{.*}}..omp_par, i32 %[[VAL_9:.*]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_8_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_8_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_9:.*]] = load i64, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_10:.*]] = trunc i64 %[[VAL_9]] to i32
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_11]], i32 0, i32 %[[VAL_10]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_8_l{{.*}}..omp_par, i32 %[[VAL_13:.*]])
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_8_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_2_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_7]], i32 0, i32 137)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_2_l{{.*}}..omp_par, i32 %[[VAL_9:.*]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_const_2_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_2_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_9:.*]] = load i16, ptr %[[VAL_0]], align 2
// CHECK:   %[[VAL_10:.*]] = sext i16 %[[VAL_9]] to i32
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_teams(ptr @3, i32 %[[VAL_11]], i32 0, i32 %[[VAL_10]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_2_l{{.*}}..omp_par, i32 %[[VAL_13:.*]])
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPthread_limit_arg_2_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: distribute.alloca:
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.distribute.region:
// CHECK:   %[[VAL_13:.*]] = select i1 false, i32 %[[VAL_14:.*]], i32 1
// CHECK:   %[[VAL_15:.*]] = select i1 false, i32 1, i32 %[[VAL_14]]
// CHECK:   %[[VAL_16:.*]] = sub nsw i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_15]], %[[VAL_13]]
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_16]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 0, i32 %[[VAL_19]]
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   store i32 %[[VAL_22]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_23]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_24:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_26:.*]] = sub i32 %[[VAL_25]], %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_29:.*]] = phi i32 [ 0, %[[VAL_21]] ], [ %[[VAL_30:.*]], %[[VAL_31:.*]] ]
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_33:.*]] = icmp ult i32 %[[VAL_29]], %[[VAL_27]]
// CHECK:   br i1 %[[VAL_33]], label %[[VAL_34:.*]], label %[[VAL_35:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_23]])
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_41:.*]] = add i32 %[[VAL_29]], %[[VAL_24]]
// CHECK:   %[[VAL_42:.*]] = mul i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_43]], ptr %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_31]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_30]] = add nuw i32 %[[VAL_29]], 1
// CHECK:   br label %[[VAL_28]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_8_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_8_l{{.*}}..omp_par.1, i32 %[[VAL_8:.*]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_8_l{{.*}}..omp_par.1(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_threads(ptr @3, i32 %[[VAL_3]], i32 137)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_8_l{{.*}}..omp_par, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_8_l{{.*}}..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region5:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.after_alloca8:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.after_alloca6:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 %[[VAL_20:.*]], i32 1
// CHECK:   %[[VAL_21:.*]] = select i1 false, i32 1, i32 %[[VAL_20]]
// CHECK:   %[[VAL_22:.*]] = sub nsw i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_23:.*]] = icmp slt i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_24:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_25:.*]] = add i32 %[[VAL_24]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_23]], i32 0, i32 %[[VAL_25]]
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i32 %[[VAL_26]], 1
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_7]], align 4
// CHECK:   store i32 1, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_29:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_29]], i32 34, ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], ptr %[[VAL_8]], i32 1, i32 1)
// CHECK:   %[[VAL_30:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = sub i32 %[[VAL_31]], %[[VAL_30]]
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_35:.*]] = phi i32 [ 0, %[[VAL_27]] ], [ %[[VAL_36:.*]], %[[VAL_37:.*]] ]
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_39:.*]] = icmp ult i32 %[[VAL_35]], %[[VAL_33]]
// CHECK:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_29]])
// CHECK:   call void @__kmpc_barrier(ptr @24, i32 %[[VAL_29]])
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_50:.*]] = add i32 %[[VAL_35]], %[[VAL_30]]
// CHECK:   %[[VAL_51:.*]] = mul i32 %[[VAL_50]], 1
// CHECK:   %[[VAL_52:.*]] = add i32 %[[VAL_51]], 1
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_52]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.region.cont10:
// CHECK:   br label %[[VAL_37]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_36]] = add nuw i32 %[[VAL_35]], 1
// CHECK:   br label %[[VAL_34]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_8_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 2, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_8_l{{.*}}..omp_par.2, i64 %[[VAL_10:.*]], i32 %[[VAL_11:.*]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_8_l{{.*}}..omp_par.2(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   %[[VAL_4:.*]] = trunc i64 %[[VAL_5:.*]] to i32
// CHECK:   call void @__kmpc_push_num_threads(ptr @3, i32 %[[VAL_3]], i32 %[[VAL_4]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_8_l{{.*}}..omp_par, i32 %[[VAL_7:.*]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_8_l{{.*}}..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region5:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.after_alloca8:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.after_alloca6:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 %[[VAL_20:.*]], i32 1
// CHECK:   %[[VAL_21:.*]] = select i1 false, i32 1, i32 %[[VAL_20]]
// CHECK:   %[[VAL_22:.*]] = sub nsw i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_23:.*]] = icmp slt i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_24:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_25:.*]] = add i32 %[[VAL_24]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_23]], i32 0, i32 %[[VAL_25]]
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i32 %[[VAL_26]], 1
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_7]], align 4
// CHECK:   store i32 1, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_29:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_29]], i32 34, ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], ptr %[[VAL_8]], i32 1, i32 1)
// CHECK:   %[[VAL_30:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = sub i32 %[[VAL_31]], %[[VAL_30]]
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_35:.*]] = phi i32 [ 0, %[[VAL_27]] ], [ %[[VAL_36:.*]], %[[VAL_37:.*]] ]
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_39:.*]] = icmp ult i32 %[[VAL_35]], %[[VAL_33]]
// CHECK:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_29]])
// CHECK:   call void @__kmpc_barrier(ptr @24, i32 %[[VAL_29]])
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_50:.*]] = add i32 %[[VAL_35]], %[[VAL_30]]
// CHECK:   %[[VAL_51:.*]] = mul i32 %[[VAL_50]], 1
// CHECK:   %[[VAL_52:.*]] = add i32 %[[VAL_51]], 1
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_52]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.region.cont10:
// CHECK:   br label %[[VAL_37]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_36]] = add nuw i32 %[[VAL_35]], 1
// CHECK:   br label %[[VAL_34]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_2_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_2_l{{.*}}..omp_par.3, i32 %[[VAL_8:.*]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_2_l{{.*}}..omp_par.3(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_push_num_threads(ptr @3, i32 %[[VAL_3]], i32 137)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_2_l{{.*}}..omp_par, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_const_2_l{{.*}}..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region5:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.after_alloca8:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.after_alloca6:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 %[[VAL_20:.*]], i32 1
// CHECK:   %[[VAL_21:.*]] = select i1 false, i32 1, i32 %[[VAL_20]]
// CHECK:   %[[VAL_22:.*]] = sub nsw i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_23:.*]] = icmp slt i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_24:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_25:.*]] = add i32 %[[VAL_24]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_23]], i32 0, i32 %[[VAL_25]]
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i32 %[[VAL_26]], 1
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_7]], align 4
// CHECK:   store i32 1, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_29:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_29]], i32 34, ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], ptr %[[VAL_8]], i32 1, i32 1)
// CHECK:   %[[VAL_30:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = sub i32 %[[VAL_31]], %[[VAL_30]]
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_35:.*]] = phi i32 [ 0, %[[VAL_27]] ], [ %[[VAL_36:.*]], %[[VAL_37:.*]] ]
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_39:.*]] = icmp ult i32 %[[VAL_35]], %[[VAL_33]]
// CHECK:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_29]])
// CHECK:   call void @__kmpc_barrier(ptr @24, i32 %[[VAL_29]])
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_50:.*]] = add i32 %[[VAL_35]], %[[VAL_30]]
// CHECK:   %[[VAL_51:.*]] = mul i32 %[[VAL_50]], 1
// CHECK:   %[[VAL_52:.*]] = add i32 %[[VAL_51]], 1
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_52]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.region.cont10:
// CHECK:   br label %[[VAL_37]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_36]] = add nuw i32 %[[VAL_35]], 1
// CHECK:   br label %[[VAL_34]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_2_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @3, i32 2, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_2_l{{.*}}..omp_par.4, i16 %[[VAL_10:.*]], i32 %[[VAL_11:.*]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_2_l{{.*}}..omp_par.4(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   %[[VAL_4:.*]] = zext i16 %[[VAL_5:.*]] to i32
// CHECK:   call void @__kmpc_push_num_threads(ptr @3, i32 %[[VAL_3]], i32 %[[VAL_4]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @3, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_2_l{{.*}}..omp_par, i32 %[[VAL_7:.*]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPnum_threads_arg_2_l{{.*}}..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca3:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region5:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.after_alloca8:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.region.after_alloca6:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 %[[VAL_20:.*]], i32 1
// CHECK:   %[[VAL_21:.*]] = select i1 false, i32 1, i32 %[[VAL_20]]
// CHECK:   %[[VAL_22:.*]] = sub nsw i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_23:.*]] = icmp slt i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_24:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_25:.*]] = add i32 %[[VAL_24]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_23]], i32 0, i32 %[[VAL_25]]
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i32 %[[VAL_26]], 1
// CHECK:   store i32 %[[VAL_28]], ptr %[[VAL_7]], align 4
// CHECK:   store i32 1, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_29:.*]] = call i32 @__kmpc_global_thread_num(ptr @3)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @3, i32 %[[VAL_29]], i32 34, ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], ptr %[[VAL_8]], i32 1, i32 1)
// CHECK:   %[[VAL_30:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = sub i32 %[[VAL_31]], %[[VAL_30]]
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_35:.*]] = phi i32 [ 0, %[[VAL_27]] ], [ %[[VAL_36:.*]], %[[VAL_37:.*]] ]
// CHECK:   br label %[[VAL_38:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_39:.*]] = icmp ult i32 %[[VAL_35]], %[[VAL_33]]
// CHECK:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @3, i32 %[[VAL_29]])
// CHECK:   call void @__kmpc_barrier(ptr @24, i32 %[[VAL_29]])
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_50:.*]] = add i32 %[[VAL_35]], %[[VAL_30]]
// CHECK:   %[[VAL_51:.*]] = mul i32 %[[VAL_50]], 1
// CHECK:   %[[VAL_52:.*]] = add i32 %[[VAL_51]], 1
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_52]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.region.cont10:
// CHECK:   br label %[[VAL_37]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_36]] = add nuw i32 %[[VAL_35]], 1
// CHECK:   br label %[[VAL_34]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
