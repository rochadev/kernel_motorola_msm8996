/*
 * Modified 1998-2001, 2003
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 *
 * Unfortunately, this file is being included by bits/signal.h in
 * glibc-2.x.  Hence the #ifdef __KERNEL__ ugliness.
 */
#ifndef _ASM_IA64_SIGNAL_H
#define _ASM_IA64_SIGNAL_H

#include <uapi/asm/signal.h>


#define _NSIG		64
#define _NSIG_BPW	64
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

# ifndef __ASSEMBLY__

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

typedef unsigned long old_sigset_t;

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	sigset_t sa_mask;		/* mask last for extensibility */
};

#  include <asm/sigcontext.h>

# endif /* !__ASSEMBLY__ */
#endif /* _ASM_IA64_SIGNAL_H */
