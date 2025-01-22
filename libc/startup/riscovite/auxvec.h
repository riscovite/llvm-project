#ifndef LLVM_LIBC_STARTUP_DISCOVITE_AUXVEC_H
#define LLVM_LIBC_STARTUP_DISCOVITE_AUXVEC_H

/* Symbolic values for the entries in the auxiliary table
   put on the initial stack */
#define AT_NULL   0	/* end of vector */
#define AT_IGNORE 1	/* entry should be ignored */
#define AT_EXECFD 2	/* file descriptor of program */
#define AT_PHDR   3	/* program headers for program */
#define AT_PHENT  4	/* size of program header entry */
#define AT_PHNUM  5	/* number of program headers */
#define AT_BASE   7	/* base address of interpreter */

#endif

