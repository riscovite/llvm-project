// RUN: %clangxx %s -### --target=riscv64-unknown-riscovite \
// RUN:     --sysroot=%S/platform 2>&1 | FileCheck %s
// CHECK: {{.*}}clang{{.*}}" "-cc1"
// CHECK: "-target-cpu" "generic-rv64"
// CHECK: "-target-feature" "+m"
// CHECK: "-target-feature" "+a"
// CHECK: "-target-feature" "+f"
// CHECK: "-target-feature" "+d"
// CHECK: "-target-feature" "+c"
// CHECK: "-target-feature" "+zicsr"
// CHECK: "-target-feature" "+zmmul"
// CHECK: "-target-feature" "+zaamo"
// CHECK: "-target-feature" "+zalrsc"
// CHECK: "-target-feature" "-b"
// CHECK: "-isysroot" "[[SYSROOT:[^"]+]]"
// CHECK: {{.*}}lld{{.*}}" "-flavor" "gnu"
// CHECK: "--sysroot=[[SYSROOT]]"
// CHECK-NOT: "-pie"
// CHECK-NOT: "-dynamic-linker"
// CHECK: Scrt1.o
// CHECK-NOT: crti.o
// CHECK-NOT: crtbegin.o
// CHECK: "-L[[SYSROOT]]/lib"
// CHECK: "-lc++"
// CHECK: "{{.*[/\\]}}libclang_rt.builtins.a"
// CHECK: "-lc"
// CHECK-NOT: crtend.o
// CHECK-NOT: crtn.o
