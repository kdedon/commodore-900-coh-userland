#!/usr/bin/perl
# deansi-msdos.pl -- run before deansi.pl on a source with #ifdef MSDOS arms.
# Drop the #ifdef MSDOS arm of every two-arm conditional.  The DOS spellings
# (backslash separators, "rb"/"wb" modes) are dead on this target, and both
# arms opening a brace defeats deansi.pl's brace-depth tracking.
my @l = <>; my @o; my $i = 0;
while ($i < @l) {
	if ($l[$i] =~ /^#ifdef\s+MSDOS\s*$/) {
		$i++;
		$i++ while $i < @l && $l[$i] !~ /^#(else|endif)/;
		if ($l[$i] =~ /^#else/) {
			$i++;
			while ($i < @l && $l[$i] !~ /^#endif/) { push @o, $l[$i]; $i++; }
		}
		$i++;			# the #endif
		next;
	}
	push @o, $l[$i++];
}
print @o;
