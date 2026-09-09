#!/usr/bin/perl
# deansi-prep.pl -- give implicit-int function definitions an explicit `int' return
# type so deansi.pl recognises them.  deansi keys on a type-bearing token
# before the name; Micropolis writes `DoDisasters(void)' with no return type at
# all, which deansi cannot tell from a macro invocation.
use strict; use warnings;
my $src = do { local $/; <> };

# ---- // comments -> /* */.  cc0 is K&R and has no // .  Walk a state machine
# so a "//" inside a string literal or an existing comment is left alone, and
# so is a trailing "*/" -bearing line (nesting would break).
{
	my $out = ''; my $st = 0;	# 0 code 1 // 2 /* */ 3 "" 4 ''
	my $n = length $src;
	for (my $i = 0; $i < $n; $i++) {
		my $c = substr($src, $i, 1);
		my $d = $i + 1 < $n ? substr($src, $i + 1, 1) : '';
		if ($st == 0) {
			if ($c eq '/' && $d eq '/') { $out .= '/*'; $st = 1; $i++; next; }
			if ($c eq '/' && $d eq '*') { $out .= '/*'; $st = 2; $i++; next; }
			$st = 3 if $c eq '"';
			$st = 4 if $c eq "'";
		} elsif ($st == 1) {
			if ($c eq "\n") { $out .= " */\n"; $st = 0; next; }
			$c = ' ' if $c eq '*' && $d eq '/';	# no nesting
		} elsif ($st == 2) {
			if ($c eq '*' && $d eq '/') { $out .= '*/'; $st = 0; $i++; next; }
		} else {
			my $q = $st == 3 ? '"' : "'";
			if ($c eq '\\') { $out .= $c . $d; $i++; next; }
			$st = 0 if $c eq $q;
		}
		$out .= $c;
	}
	$out .= " */\n" if $st == 1;
	$src = $out;
}

my @l = split /^/, $src;
for (my $i = 0; $i < @l; $i++) {
	next unless $l[$i] =~ /^([A-Za-z_]\w*)\s*\(/;
	next if $1 =~ /^(if|for|while|switch|do|return|sizeof|else|ARGS)$/;
	# previous non-blank line must not end like a return type or continue a
	# statement; and the line above must not be inside a parameter list.
	my $p = $i ? $l[$i-1] : "\n";
	next if $p =~ /[\w*,(]\s*$/;
	# find the end of the parameter list (this line or a continuation)
	my $j = $i; my $txt = $l[$i];
	while ($txt !~ /\)/ && $j + 1 < @l && $j - $i < 12) { $j++; $txt .= $l[$j]; }
	$txt =~ s{/\*.*?\*/}{}gs;		# a trailing comment is not syntax
	$txt =~ s/\s+$//;
	next unless $txt =~ /^[A-Za-z_]\w*\s*\((.*?)\)$/s;
	my $inner = $1;
	next if $inner =~ /;/;
	# a definition, not a call: the next non-blank line opens a block
	my $k = $j + 1;
	$k++ while $k < @l && $l[$k] =~ /^\s*$/;
	next unless $k < @l && $l[$k] =~ /^\s*\{/;
	$l[$i] = "int " . $l[$i];
}
print @l;
