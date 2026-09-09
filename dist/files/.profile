export PATH=:/bin:/usr/bin:/etc
: 'The block below is the second half of /etc/shutdown.  init answers SIGHUP'
: 'by killing every process and spawning the single-user shell as a LOGIN'
: 'shell, which reads this file -- so this is where a shutdown finishes,'
: 'once nothing else is running and nothing else can hold a filesystem busy.'
: ''
: 'IT ACTS ONLY IF /etc/shutdown.lvl EXISTS, and nothing but /etc/shutdown'
: 'writes that file.  The guard is load-bearing rather than tidy: this file'
: 'is also root HOME/.profile, so a root login on any terminal in multi-user'
: 'reads it, and an unconditional unmount here would detach /usr under a'
: 'running system.  The 4.2 original could act unconditionally because its'
: 'half lived in /etc/.profile, which that init ran for the single-user'
: 'shell alone.'
: ''
: 'The level file is removed before it is acted on.  If anything below'
: 'fails the fall-through is an ordinary single-user shell, and a level left'
: 'behind would take the NEXT shell down as well.'
: ''
: 'IT MUST ALSO BE THIS BOOT LEVEL FILE, which is what the find is for.'
: 'init creates /etc/boottime once per boot (cmd/init.c), the same idiom'
: 'mount(1) uses to tell a stale /etc/mtab from a live one.  Without the'
: 'test, a machine that lost power part way through a shutdown would find'
: 'the level file on the next boot -- and since there is no /etc/brc, init'
: 'offers the single-user shell on EVERY boot, so this file would run, halt'
: 'the machine, and do it again on the boot after that.  A boot loop that'
: 'cannot be broken from the console is a far worse failure than the one an'
: 'unclean shutdown causes.'
: ''
: 'find -newer AND NOT test -nt.  /bin/test here is the 3.x yacc test'
: '(cmd/test/test.y), whose primaries are -r -w -f -d -s -t -z -n and the'
: 'six numeric comparisons: -nt is a 4.2 primary and cmd-4.2/test.c is held'
: 'but not built, so `test a -nt b` is a syntax error -- which is FALSE, so'
: 'the guard would have refused every shutdown rather than let one through.'
: 'A missing /etc/boottime makes find print nothing, which refuses too, and'
: 'that is the safe direction.'
: ''
: 'THE find SEARCHES /etc AND MATCHES BY NAME rather than naming the file:'
: 'this find (cmd/find) takes only directories as starting points and answers'
: '"not a directory" for a plain file, printing nothing -- which the -n test'
: 'reads as stale, so the whole shutdown would have stopped one step short of'
: 'halting and left the machine in single user instead.'
: ''
: 'THIS MACHINE HAS NO WARM RESTART, which is why the reboot level halts.'
: 'The Commodore 900 has no software reset: the boot ROM is fixed read-only'
: 'at physical 0 with no overlay register to switch, the Z8001 reset sequence'
: 'is a pin event that also resets the Z8010 MMU and the peripherals, and the'
: 'one restart interface the ROM declares -- rom_restart in struct romconf --'
: 'is never assigned by the ROM and reads 0.  So reboot says so and halts,'
: 'rather than jumping somewhere and hanging.'
: ''
: 'This shell has no comment lexer, so the explanation is in : arguments.'
if /bin/test -f /etc/shutdown.lvl
then
	fresh=`/bin/find /etc -name shutdown.lvl -newer /etc/boottime -print 2>/dev/null`
	if /bin/test -n "$fresh"
	then
		lvl=`/bin/cat /etc/shutdown.lvl`
		/bin/rm -f /etc/shutdown.lvl
		trap '' 1 2 3
		/etc/umount.all
		if /bin/test "$lvl" = reboot
		then
			echo 'reboot: this machine cannot restart itself in software.'
			echo 'reboot: halting instead -- press reset to restart.'
		fi
		if /bin/test "$lvl" = halt -o "$lvl" = reboot
		then
			/etc/halt
		fi
		trap 1 2 3
	else
		echo 'shutdown: ignoring a level file left over from an earlier boot'
		/bin/rm -f /etc/shutdown.lvl
	fi
fi
: 'TERM is set in /etc/profile, which every login shell reads first --'
: 'this file is only root login shell, not every one.'
: 'root reaches its own mail with mail(1); MAIL is what mail(1), the shell'
: 's checkmail() (sh.fns/exec2.c, run at every prompt), and mgrbiff read to'
: 'find the box.'
MAIL=/usr/spool/mail/root
export MAIL
