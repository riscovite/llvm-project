//===----- RISCovite system function numbers --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSFUNCS_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_RISCOVITE_SYSFUNCS_H

// NOTE: This file intentionally includes only comments and preprocessor
// directives that expand to plain constant numbers so that it can potentially
// be used by both C/C++ and assembly language callers.
//
// sysfunc_types.h is a supporting header that defines C-friendly types for
// use by library code that is interacting with the system functions.

//// core functions
#define RISCOVITE_SYS_SET_HEAP_SIZE (0x010)
#define RISCOVITE_SYS_CLOSE (0x020)
#define RISCOVITE_SYS_SELECT_INTERFACE (0x030)
#define RISCOVITE_SYS_SET_TIMER_INTERRUPT (0x040)
#define RISCOVITE_SYS_GET_CURRENT_TIMESTAMP (0x050)
#define RISCOVITE_SYS_SET_INTERRUPT_PRIORITY_FLOOR (0x060)
#define RISCOVITE_SYS_CREATE_MEMORY_BLOCK (0x070)
#define RISCOVITE_SYS_EXIT (0x800)
#define RISCOVITE_SYS_GET_HEAP_BASE (0x810)
#define RISCOVITE_SYS_GET_HEAP_SIZE (0x820)
#define RISCOVITE_SYS_SET_FAULT_HANDLER (0x830)
#define RISCOVITE_SYS_SET_INTERRUPT_STACK (0x840)
#define RISCOVITE_SYS_GET_INTERFACE (0x850)

//// io functions
#define RISCOVITE_SYS_READ (0x011)
#define RISCOVITE_SYS_WRITE (0x021)
#define RISCOVITE_SYS_SYNC (0x031)
#define RISCOVITE_SYS_OPEN (0x041)
#define RISCOVITE_SYS_OPEN_REPLACE (0x051)
#define RISCOVITE_SYS_OPEN_RESOURCE (0x061)
#define RISCOVITE_SYS_TRAVERSE (0x071)
#define RISCOVITE_SYS_TRAVERSE_REPLACE (0x081)
#define RISCOVITE_SYS_DUP (0x091)
#define RISCOVITE_SYS_READ_DIR (0x0a1)
#define RISCOVITE_SYS_RESET_READ_DIR (0x0b1)
#define RISCOVITE_SYS_DUP_REPLACE (0x0c1)
#define RISCOVITE_SYS_SEEK (0x0d1)
#define RISCOVITE_SYS_GET_DOS_PATH (0x801)

//// process functions
#define RISCOVITE_SYS_EXEC_CHILD (0x012)
#define RISCOVITE_SYS_EXEC_REPLACE (0x022)
#define RISCOVITE_SYS_MONITOR_CTX_CREATE (0x032)
#define RISCOVITE_SYS_MONITOR_CTX_LOAD_EXEC (0x042)

//// Misc constants used with system functions
#define RISCOVITE_OPEN_DIR (0x1)
#define RISCOVITE_OPEN_FILE (0x0)
#define RISCOVITE_OPEN_TO_READ (0x2)
#define RISCOVITE_OPEN_TO_WRITE (0x4)
#define RISCOVITE_OPEN_TO_READWRITE (0x6)
#define RISCOVITE_OPEN_TO_EXECUTE (0x8)
#define RISCOVITE_OPEN_APPEND (0x100)
#define RISCOVITE_OPEN_CREATE (0x10000)
#define RISCOVITE_OPEN_NONEXIST (0x20000)
#define RISCOVITE_OPEN_TRUNCATE (0x40000)
#define RISCOVITE_OPEN_CREATE_READONLY (0x1000000)
#define RISCOVITE_OPEN_CREATE_SYSTEM (0x2000000)
#define RISCOVITE_SEEK_SET (0x0)
#define RISCOVITE_SEEK_CUR (0x1)
#define RISCOVITE_SEEK_END (0x2)
#define RISCOVITE_TRAVERSE_REQ_EXIST (0x1)
#define RISCOVITE_TRAVERSE_REQ_DIR (0x3)
#define RISCOVITE_HND_STDIN (0x0)
#define RISCOVITE_HND_STDOUT (0x1)
#define RISCOVITE_HND_STDERR (0x2)
#define RISCOVITE_HND_CWD (0x3)

#endif
