// RUN: llvm-tblgen -gen-global-isel -I %p/../../../include -I %p/../Common %s | FileCheck %s

include "llvm/Target/Target.td"
include "GlobalISelEmitterCommon.td"

def undef_tied : OperandWithDefaultOps<untyped, (ops (i32 undef_tied_input))> {
  let MIOperandInfo = (ops GPR32:$inactive);
}

let Constraints = "$opt.inactive = $rd" in
def I1 : I<(outs GPR32:$rd), (ins GPR32:$rs, undef_tied:$opt),
           [(set GPR32:$rd, (abs i32:$rs))]>;

// CHECK-LABEL: // (abs:{ *:[i32] } i32:{ *:[i32] }:$rs)  =>  (I1:{ *:[i32] } i32:{ *:[i32] }:$rs)
// CHECK-NEXT: GIR_MakeTempReg, /*TempRegID*/0, /*TypeID*/GILLT_s32,
// CHECK-NEXT: GIR_BuildMI, /*InsnID*/1, /*Opcode*/GIMT_Encode2(TargetOpcode::IMPLICIT_DEF),
// CHECK-NEXT: GIR_AddTempRegister, /*InsnID*/1, /*TempRegID*/0, /*TempRegFlags*/GIMT_Encode2(RegState::Define),
// CHECK-NEXT: GIR_BuildRootMI, /*Opcode*/GIMT_Encode2(MyTarget::I1),
// CHECK-NEXT: GIR_RootToRootCopy, /*OpIdx*/0, // DstI[rd]
// CHECK-NEXT: GIR_RootToRootCopy, /*OpIdx*/1, // rs
// CHECK-NEXT: GIR_AddSimpleTempRegister, /*InsnID*/0, /*TempRegID*/0,
// CHECK-NEXT: GIR_RootConstrainSelectedInstOperands,
// CHECK-NEXT: // GIR_Coverage, 0,
// CHECK-NEXT: GIR_EraseRootFromParent_Done,
