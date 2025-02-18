//===-- Implementation of abort -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/stdlib/_Exit.h"

#include "src/stdlib/abort.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, abort, ()) {
  LIBC_NAMESPACE::_Exit(127);
}

} // namespace LIBC_NAMESPACE_DECL
