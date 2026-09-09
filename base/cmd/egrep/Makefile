# The 3.x/4.0 egrep is one self-contained source: the NFA, the equivalence
# classes and the DFA that were separate files in the 1985 version are all in
# egrep.c.  COHERENT selects <access.h> and the -A option's call out to me(1).
egrep: egrep.c
	cc -DCOHERENT -o egrep egrep.c
