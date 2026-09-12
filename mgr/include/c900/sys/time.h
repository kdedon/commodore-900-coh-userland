/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/* sys/time.h -- COHERENT 3.2 splits struct timeval into <sys/select.h> and
   leaves its own <sys/time.h> unguarded; including both is an error.  This
   shim routes both spellings to the guarded one.  */
#ifndef C900_SYS_TIME_H
#define C900_SYS_TIME_H
#include <sys/select.h>
#endif
