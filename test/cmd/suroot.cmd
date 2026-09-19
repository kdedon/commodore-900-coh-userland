# suroot.cmd -- su(1) to an account with no password is a CONSOLE privilege.
#
#	hostbuild/emu-run.sh test/cmd/suroot.cmd
#
# /etc/passwd gives root an EMPTY password field, deliberately: a machine whose
# only input is the keyboard in front of it must never be locked out of itself.
# login(1) confines what that costs by refusing uid 0 on any line that is not one
# of this machine's own consoles.  su(1) is the other door to uid 0 and it asks
# for nothing at all when the field is empty, so it needs the same confinement:
# any session that reached a shell -- a network one included -- was one command
# away from root.
#
# THE TEST NEEDS A NON-ROOT SESSION, and emu-run.sh logs in as root.  `su
# guest' is how one is made: the outer su drops to uid 8 and runs the INNER su,
# which is the one under test.  Every case is therefore a nested su, and the
# answer is what the inner one did.
#
# THREE CASES, AND THE FIRST TWO ARE OPPOSITE ANSWERS TO THE SAME COMMAND:
#
#   T_CON     ON THE CONSOLE, su to root must still work.  This is the owner's
#             requirement and the reason the empty field exists; a change that
#             passed the refusal below and failed here would have locked the
#             machine out of itself.  The emulator's console is /dev/console,
#             which the kvcon driver answers (major 8), so this runs where a
#             person at the keyboard runs it.
#
#   T_OFF     THE SAME COMMAND WITH NO CONSOLE DESCRIPTOR must be refused.  Its
#             standard input is /dev/null and its output and error are a file --
#             none of the three is a character device on major 8, which is what a
#             session that arrived from somewhere else looks like.  The marker
#             the command would have printed is the failure: the file must hold
#             the refusal instead, and that is also WHERE the refusal is visible
#             to whoever ran it.
#
#   T_OTHER   AND THE GATE IS uid 0, NOT "no password".  The `who' account has an
#             empty field too and is not the super user, so su to it is unchanged
#             off the console.  Without this a refusal everywhere would pass the
#             case above and take something away that nobody asked to close.
#
# Nothing here needs the network, a pty or a second machine: the question is
# which device a descriptor is on, and the emulator has both kinds.
#
# For test/cmd/run.sh, one line per case, and each is a line only that case's
# right answer prints: ROOTOK on the console for T_CON; the refusal, read back
# out of the file, for T_OFF (a T_OFF that let the su through leaves ROOTOK in
# the file instead, and the refusal line is then missing); WHOOK for T_OTHER.
#% expect ^ROOTOK$
#% expect ^root has no password: su to it from the console only$
#% expect ^WHOOK$
#% expect ^T_DONE$
#% reject Segmentation violation
#% reject ^Panic:
echo T_CON
/bin/su guest /bin/su 0 /bin/echo ROOTOK
echo T_OFF
/bin/su guest /bin/su 0 /bin/echo ROOTOK >/tsu.out 2>&1 </dev/null
/bin/cat /tsu.out
echo T_OTHER
/bin/su guest /bin/su who /bin/echo WHOOK >/tsu2.out 2>&1 </dev/null
/bin/cat /tsu2.out
echo T_DONE
