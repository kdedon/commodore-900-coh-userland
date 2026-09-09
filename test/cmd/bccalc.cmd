# bccalc.cmd -- does bc(1) compute the right digits, and does dc(1) still work?
#
#	EMUWAIT=3600 hostbuild/emu-run.sh tests/cmd/bccalc.cmd
#
# It is a long run: sixteen bc and dc invocations and ninety-odd shell lines,
# each of which the emulator feeds a byte at a time and then waits for a fresh
# prompt.  Twenty minutes on an idle machine, and two or three times that with
# other lanes' emulators on the same cores, which is what EMUWAIT is for.
#
# bc is a multiple-precision calculator, so it is gated on arithmetic whose
# answers are known off the machine -- 2^200, 30!, 1/7 to thirty places -- and
# not on whether it starts.  Nothing else in the image exercises libmp at all.
#
# THE GUEST DOES ITS OWN COMPARING.  Each case writes bc's output to /oN, the
# expected digits to /eN, and cmp(1) decides; a case prints PASS-N or FAIL-N.
# Sixteen cases, sixteen PASS lines.  Count them at the START of a line: the
# transcript echoes the `echo ... > /bN' that wrote each script as well as the
# result, so an unanchored grep matches the setup and not the answer.  A case
# that prints neither PASS nor FAIL did not finish -- silence is a failure.
#
# THE CONTROLS ARE LOAD-BEARING.  Cases 1, 7, 12 and 15 -- 2+2, the C `0x'
# escape, reverse-Polish addition, and A-F as digits in dc -- pass on both the
# 0.7.3 and the relicD lineage.  Without them a bc that had merely stopped
# computing at all would take the whole file down and prove nothing about which
# source it was built from.
#
# THREE CASES ARE THE ADOPTION, and they FAIL on the 0.7.3 lineage:
#   case 6   `ibase=16; 0FF' is 255.  The older getnum() takes a leading `0' as
#            an octal escape whatever ibase is, so it reads 0FF in base 8 with
#            F worth fifteen and answers 135.  The escape is now taken only
#            when ibase<=10, where it is unambiguous.  The FIRST line of that
#            case, `ibase=16; FF', is the control for the one local edit made on
#            top of the adopted source: relicD's lexer starts a name at any
#            letter, so FF is an undefined VARIABLE there and the answer is a
#            silent 0.  A name begins with a lower-case letter here, as both the
#            0.7.3 lexer and the Lexicon have it, so FF is 255.
#   case 10  `quit' in a branch that is never taken does not exit.  The older
#            grammar returned from yyparse() the moment `quit' was REDUCED, so
#            everything after it in the file was never read.
#   case 11  a negative exponent computes.  The older bcexp() has no case for
#            one and loops on a negative shift count.
#
# Case 16 needs /usr, which rc has not mounted in single user, so it mounts it
# itself; bc -l reads /usr/lib/lib.b.  If the mount fails the case says SKIP-16
# rather than FAIL-16 -- a library that is not on the disk is not a bug in bc --
# so a run with fifteen PASS lines and a SKIP-16 is a pass.
echo == 1 CONTROL: it computes at all
echo '2+2' > /b1
bc < /b1 > /o1
echo 4 > /e1
cmp /o1 /e1 && echo PASS-1 || echo FAIL-1
echo == 2 multiple precision: 2^200, sixty-one digits
echo '2^200' > /b2
bc < /b2 > /o2
echo 1606938044258990275541962092341162602522202993782792835301376 > /e2
cmp /o2 /e2 && echo PASS-2 || echo FAIL-2
echo == 3 multiple precision: 30! by a for loop
echo 'n=1' > /b3
echo 'for (i=1; i<=30; i=i+1) n=n*i' >> /b3
echo 'n' >> /b3
bc < /b3 > /o3
echo 265252859812191058636308480000000 > /e3
cmp /o3 /e3 && echo PASS-3 || echo FAIL-3
echo == 4 scale: 1/7 to thirty places
echo 'scale=30' > /b4
echo '1/7' >> /b4
bc < /b4 > /o4
echo .142857142857142857142857142857 > /e4
cmp /o4 /e4 && echo PASS-4 || echo FAIL-4
echo == 5 obase: 255 in hex, 10 in binary
echo 'obase=16' > /b5
echo '255' >> /b5
bc < /b5 > /o5
echo 'obase=2' > /b5b
echo '10' >> /b5b
bc < /b5b >> /o5
echo FF > /e5
echo 1010 >> /e5
cmp /o5 /e5 && echo PASS-5 || echo FAIL-5
echo == 6 ibase=16 -- ADOPTION: 0FF is 255, not 135 in base 8
echo 'ibase=16' > /b6
echo 'FF' >> /b6
echo '0FF' >> /b6
bc < /b6 > /o6
echo 255 > /e6
echo 255 >> /e6
cmp /o6 /e6 && echo PASS-6 || echo FAIL-6
echo == 7 CONTROL: the C 0x and 0 escapes at ibase 10
echo '0x1F' > /b7
echo '010' >> /b7
bc < /b7 > /o7
echo 31 > /e7
echo 8 >> /e7
cmp /o7 /e7 && echo PASS-7 || echo FAIL-7
echo == 8 sqrt, length, scale
# length() is the length of the mantissa in BYTES here, which is what both
# lineages compute and what bcmch.h documents the LENGTH opcode to do; 12345
# fits in two.  The Lexicon's bc entry does not mention length() at all, so
# there is no documented contract this contradicts.
echo 'scale=10' > /b8
echo 'sqrt(2)' >> /b8
echo 'length(12345)' >> /b8
echo 'scale=3' >> /b8
echo 'x=1/3' >> /b8
echo 'scale(x)' >> /b8
bc < /b8 > /o8
echo 1.4142135623 > /e8
echo 2 >> /e8
echo 3 >> /e8
cmp /o8 /e8 && echo PASS-8 || echo FAIL-8
echo == 9 a function definition, a while loop, an if and a break
# The body opens on its own line: a definition all on one line is a syntax
# error in both lineages, and the Lexicon writes it this way too.
echo 'define f(x) {' > /b9
echo 'return(x*x);' >> /b9
echo '}' >> /b9
echo 'f(12)' >> /b9
echo 'i=0' >> /b9
echo 's=0' >> /b9
echo 'while (i < 10) { i=i+1; if (i == 5) { continue; } s=s+i; }' >> /b9
echo 's' >> /b9
echo 'j=0' >> /b9
echo 'for (i=1; i<=100; i=i+1) { j=j+1; if (j == 7) { break; } }' >> /b9
echo 'j' >> /b9
bc < /b9 > /o9
echo 144 > /e9
echo 50 >> /e9
echo 7 >> /e9
cmp /o9 /e9 && echo PASS-9 || echo FAIL-9
echo == 10 ADOPTION: quit in a branch that is never taken does not exit
echo 'if (0 == 1) { quit; }' > /b10
echo '1+1' >> /b10
bc < /b10 > /o10
echo 2 > /e10
cmp /o10 /e10 && echo PASS-10 || echo FAIL-10
echo == 11 ADOPTION: a negative exponent
echo 'scale=4' > /b11
echo '2^-3' >> /b11
bc < /b11 > /o11
echo .1250 > /e11
cmp /o11 /e11 && echo PASS-11 || echo FAIL-11
echo == 12 CONTROL: dc reverse-Polish arithmetic
echo '5 3 + p' > /d12
dc < /d12 > /o12
echo 8 > /e12
cmp /o12 /e12 && echo PASS-12 || echo FAIL-12
echo == 13 dc multiple precision and a register store/load
echo '2 200 ^ p' > /d13
echo '7 sa 3 la * p' >> /d13
dc < /d13 > /o13
echo 1606938044258990275541962092341162602522202993782792835301376 > /e13
echo 21 >> /e13
cmp /o13 /e13 && echo PASS-13 || echo FAIL-13
echo == 14 dc: ibase 16, and 0FF is 255 there too
echo '16 i FF p 0FF p' > /d14
dc < /d14 > /o14
echo 255 > /e14
echo 255 >> /e14
cmp /o14 /e14 && echo PASS-14 || echo FAIL-14
echo == 15 CONTROL: in dc the letters are digits whatever the base
echo 'A p F p' > /d15
dc < /d15 > /o15
echo 10 > /e15
echo 15 >> /e15
cmp /o15 /e15 && echo PASS-15 || echo FAIL-15
echo == 16 bc -l reads the math library off /usr
# /usr/lib/lib.b defines exp, ln, sin, cos, atan and j, and its own pi to a
# hundred places -- not the single-letter names other bc libraries use.  The
# values are the library's own truncations at scale 5, which is why exp(1) ends
# 24 and not 28.  Reading it also exercises several autos per define, `*=',
# `+=' and `++', which nothing else here does.
/etc/mount /dev/hd6 /usr
echo 'scale=5' > /b16
echo 'exp(1)' >> /b16
echo 'ln(100)' >> /b16
echo 'sin(0)' >> /b16
echo 'cos(0)' >> /b16
echo 'atan(1)' >> /b16
echo 2.71824 > /e16
echo 4.60417 >> /e16
echo 0 >> /e16
echo 1 >> /e16
echo .78536 >> /e16
if test -r /usr/lib/lib.b
then bc -l < /b16 > /o16
	cmp /o16 /e16 && echo PASS-16 || echo FAIL-16
else echo SKIP-16
fi
echo == ALLDONE
