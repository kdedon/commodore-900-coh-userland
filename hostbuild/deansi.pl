#!/usr/bin/perl
# deansi.pl -- convert ANSI C function definitions and prototype declarations
# to K&R ("traditional") form so the strict-K&R MWC cc0 can compile them.
# Purpose-built source-prep pass for the COHERENT/Z8001 ports (Minix inet,
# MGR).  Reads C source on stdin (or a file argument), writes converted
# source to stdout.
#
#   function DEFINITION   T name(T a, U *b) {   ->  T name(a, b) T a; U *b; {
#   prototype DECLARATION T name(T a, U b);     ->  T name();
#
# Left untouched: calls and control-flow, macro invocations, function-pointer
# typedefs and struct members, prototypes already wrapped in ARGS()/_PROTOTYPE()
# (those degrade to () through the preprocessor), varargs and already-K&R defs.
# When a header cannot be converted safely it is left verbatim.
use strict;
use warnings;

my $src = do { local $/; <> };
my $n = length $src;

# ---- masked copy: comments, string/char literals and cpp directive lines
# become spaces (newlines preserved, length preserved) so scanning sees only
# real top-level code and never a paren/brace/';' hiding in a literal or macro.
my $m = $src;
sub blank { my ($s, $e) = @_; for (my $x = $s; $x < $e; $x++) {
	substr($m, $x, 1) = ' ' if substr($m, $x, 1) ne "\n"; } }
{
	my $st = 0;			# 0 code 1 // 2 /* */ 3 "" 4 '' 5 cpp
	for (my $i = 0; $i < $n; $i++) {
		my $c = substr($m, $i, 1);
		my $d = $i + 1 < $n ? substr($m, $i + 1, 1) : '';
		if ($st == 0) {
			if    ($c eq '/' && $d eq '/') { blank($i, $i + 2); $st = 1; $i++; }
			elsif ($c eq '/' && $d eq '*') { blank($i, $i + 2); $st = 2; $i++; }
			elsif ($c eq '"')  { substr($m, $i, 1) = ' '; $st = 3; }
			elsif ($c eq "'")  { substr($m, $i, 1) = ' '; $st = 4; }
			elsif ($c eq '#' && (substr($src, 0, $i) =~ /(^|\n)[ \t]*$/)) {
				substr($m, $i, 1) = ' '; $st = 5; }
		} elsif ($st == 1) { $st = 0 if $c eq "\n"; blank($i, $i + 1) if $c ne "\n"; }
		elsif ($st == 2) {
			if ($c eq '*' && $d eq '/') { blank($i, $i + 2); $st = 0; $i++; }
			else { blank($i, $i + 1); } }
		elsif ($st == 3 || $st == 4) {
			my $q = $st == 3 ? '"' : "'";
			if ($c eq '\\') { blank($i, $i + 2); $i++; }
			elsif ($c eq $q) { substr($m, $i, 1) = ' '; $st = 0; }
			else { blank($i, $i + 1); } }
		elsif ($st == 5) {		# cpp directive: to physical eol, honoring \<nl>
			if ($c eq "\n" && substr($src, $i - 1, 1) ne '\\') { $st = 0; }
			else { blank($i, $i + 1) if $c ne "\n"; } }
	}
}

# cc0 (strict K&R) has no "void *": it is the generic pointer, which in K&R C
# is spelled "char *".  Rewrite "void" -> "char" wherever it is immediately a
# pointer ("void *"), in code only (mask keeps us out of strings/comments).
# "void" as a return type, parameter or (void) cast is left alone.  "void" and
# "char" are both 4 chars, so this is length-preserving and positions are kept.
while ($m =~ /\bvoid\b(?=\s*\*)/g) {
	my $at = $-[0];
	substr($src, $at, 4) = 'char';
	substr($m,   $at, 4) = 'char';
}

my %STOP = map { $_ => 1 } qw(if for while switch do return sizeof typedef
	ARGS _ARGS _PROTOTYPE PROTOTYPE _ARGS_ else);

# split a parameter list (inner text, no parens) at top-level commas
sub split_params {
	my ($p) = @_;
	my (@parts, $cur, $d); $cur = ''; $d = 0;
	for my $c (split //, $p) {
		if    ($c eq '(') { $d++; $cur .= $c; }
		elsif ($c eq ')') { $d--; $cur .= $c; }
		elsif ($c eq ',' && $d == 0) { push @parts, $cur; $cur = ''; }
		else  { $cur .= $c; }
	}
	push @parts, $cur if $cur =~ /\S/;
	map { s/^\s+|\s+$//g; $_ } @parts;
}

# ANSI param list -> (\@argnames, \@decls); undef on give-up (varargs/opaque)
sub knr {
	my ($p) = @_;
	$p =~ s/^\s+|\s+$//g;
	return [[], []] if $p eq '' || $p eq 'void';
	my (@names, @decls);
	my @parts = split_params($p);
	for my $part (@parts) {
		# K&R varargs: drop a trailing "..." (the body uses va_start); but a
		# lone "..." has no fixed param to hang the K&R list on -- give up.
		next if $part eq '...' && @parts > 1;
		return undef if $part eq '...';
		my $name;
		if    ($part =~ /\(\s*\*\s*(\w+)\s*\)/) { $name = $1; }		# fn ptr
		elsif ($part =~ /(\w+)\s*\[[^\]]*\]\s*$/) { $name = $1; }	# array
		elsif ($part =~ /(\w+)\s*$/) { $name = $1; }			# plain
		else  { return undef; }
		push @names, $name;
		push @decls, "$part;";
	}
	[\@names, \@decls];
}

# is this ANSI (needs conversion) vs already-K&R (bare name list)?
sub is_ansi {
	my ($p) = @_;
	$p =~ s/^\s+|\s+$//g;
	return 1 if $p eq 'void';
	return 0 if $p eq '';
	for my $part (split_params($p)) {
		return 1 unless $part =~ /^\w+$/;	# any typed/pointer/array part
	}
	0;
}

my @edits;				# [start, end, replacement]
my $bd = 0;				# brace depth
my $i = 0;
while ($i < $n) {
	my $c = substr($m, $i, 1);
	if ($c eq '{') { $bd++; $i++; next; }
	if ($c eq '}') { $bd-- if $bd > 0; $i++; next; }
	if ($bd == 0 && $c =~ /[A-Za-z_]/) {
		my $j = $i; $j++ while $j < $n && substr($m, $j, 1) =~ /\w/;
		my $name = substr($m, $i, $j - $i);
		my $k = $j; $k++ while $k < $n && substr($m, $k, 1) =~ /\s/;
		if ($k < $n && substr($m, $k, 1) eq '(' && !$STOP{$name}) {
			# A real definition/prototype has a return type right before the
			# name -- either earlier on the name's own line, or trailing the
			# line above.  A macro invocation `NAME(args);` has the name at a
			# statement start (nothing type-like before it, or a blank/cpp line
			# above).  Distinguish by context to avoid mangling macro calls.
			my $ls = rindex($m, "\n", $i - 1) + 1;		# start of name's line
			my $before = substr($m, $ls, $i - $ls);
			my $ctxstr = $before;				# the type-bearing text
			unless ($before =~ /\S/) {			# name at line start:
				my $pe = $ls - 1;			# fold in the line above
				my $ps = rindex($m, "\n", $pe - 1) + 1;
				$ctxstr = $pe > 0 ? substr($m, $ps, $pe - $ps) : '';
			}
			my $ctx = ($ctxstr =~ /[\w*]\s*$/) ? 1 : 0;	# ends like a return type
			my $tok0 = ($ctxstr =~ /(\w+)\s*\**\s*$/) ? $1 : '';
			if ($ctx && !$STOP{$tok0}
			    && substr($m, $ls, $k - $ls + 1) !~ /\b(ARGS|_PROTOTYPE)\s*\(/) {
				# match parens
				my ($d, $p) = (0, $k);
				while ($p < $n) {
					my $ch = substr($m, $p, 1);
					$d++ if $ch eq '(';
					$d-- if $ch eq ')';
					last if $d == 0;
					$p++;
				}
				if ($p < $n) {			# $p = closing paren
					my $q = $p + 1; $q++ while $q < $n && substr($m, $q, 1) =~ /\s/;
					my $nx = $q < $n ? substr($m, $q, 1) : '';
					my $inner = substr($src, $k + 1, $p - $k - 1);
					if ($nx eq '{') {		# DEFINITION
						if (is_ansi($inner)) {
							my $r = knr($inner);
							if ($r) {
								my ($names, $decls) = @$r;
								my $rep = '(' . join(', ', @$names) . ")\n"
									. join('', map { "$_\n" } @$decls);
								push @edits, [$k, $q, $rep];
							}
						}
					} elsif ($nx eq ';') {		# DECLARATION
						push @edits, [$k, $q, '()'] if $inner =~ /\S/;
					}
				}
			}
		}
		$i = $j; next;
	}
	$i++;
}

# apply right-to-left so positions stay valid
for my $e (sort { $b->[0] <=> $a->[0] } @edits) {
	substr($src, $e->[0], $e->[1] - $e->[0]) = $e->[2];
}
print $src;
