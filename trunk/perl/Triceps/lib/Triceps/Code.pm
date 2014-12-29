#
# (C) Copyright 2011-2014 Sergey A. Babkin.
# This file is a part of Triceps.
# See the file COPYRIGHT for the copyright notice and license information
#
# The parsing of brace-quoted lines.

package Triceps::Code;

sub CLONE_SKIP { 1; }

our $VERSION = 'v2.0.0';

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw(
	compile
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

use Carp;

# Compile the code in either of Triceps-standard representations.
# Receives a code reference or a string.
# If the argument is an undef or a code reference, returns it unchanged
# (if the code is mandatory, checking it for undef is caller's responsibility).
# If the argument is a string, encloses it in "sub { ... }"
# and compiles. Either way, the result will be a code reference.
# Confesses on the compilation errors. The code description is used
# in the confession message.
sub compile # ( $code_ref_or_string, $optional_code_description )
{
	no warnings 'all'; # shut up the warnings from eval
	return $_[0] if (!defined $_[0] || ref $_[0] eq 'CODE');
	my $descr = $_[1] ? $_[1] : "Code snippet";
	
	if (ref \$_[0] ne 'SCALAR') {
		Carp::confess("$descr: code must be a source code string or a reference to Perl function");
	}

	my $src = "sub {\n" . $_[0] . "\n}\n";
	my $code = eval $src;

	if (!$code) {
		# $@ alerady includes \n, so don't add another one after it
		Carp::confess(
			"$descr: failed to compile the source code\n"
			. "Compilation error: $@The source code was:\n$src");
	}

	# XXX This is a cryptic message in case if the user gets something very
	# wrong with the brace balance. But it matches the message from PerlCallback.
	if (ref $code ne 'CODE') {
		Carp::confess("$descr: code must be a source code string or a reference to Perl function");
	}
	return $code
}

# Left-align the source code by removing the excess whitespace on the
# left (which tends to get produced in the auto-generated code)
# and then adding the required indentation.
# The excess whitespace gets detected by the first two lines.
# The tabs get replaced with two spaces ("  ") each.
#
# $code - the source code to align; it will also have \n at the end removed
#     if it was there; and any empty lines at the front will be removed as well
# $indent - indentation to prepend to all the lines after removing the
#     auto-detected indentation
# $tab - each tab (\t) will be replaced to this string, or it's taken as
#     two spaces "  " if empty
sub alignsrc # ($code, $indent, [ $tab ])
{
	my ($code, $indent, $tab) = @_;
	my @ci;

	chomp $code;
	$code =~ s/^(\s*\n)+//;
	$tab = "  " if ($tab eq "");

	# find the indentation on the first and last lines
	$code =~ /^(\s*)/;
	push @ci, $1;
	$code =~ /^.*\n(\s*)/;
	push @ci, $1;
	$code =~ /^.*\n.*\n(\s*)/;
	push @ci, $1;
	$code =~ /\n(\s*).*$/;
	push @ci, $1;
	$code =~ /\n(\s*).*\n.*$/;
	push @ci, $1;

	if (0) {
		print "first '$ci[0]'\n";
		print "second '$ci[1]'\n";
		print "third '$ci[2]'\n";
		print "belast '$ci[4]'\n"; # the before-last line
		print "last '$ci[3]'\n";
	}

	# find the smallest non-empty indentation (assume that the tabs and spaces
	# are used consistently, and if in doubt, prefer tabs)
	my $oldind = '';
	foreach $i (@ci) {
		next if ($i eq '');
		if ($oldind eq '') {
			$oldind = $i;
			next;
		}
		if ($oldind =~ /^ / && $i =~ /^\t/) {
			$oldind = $i;
			next;
		}
		if (length($i) < length($oldind)) {
			$oldind = $i;
			next;
		}
	}

	#print "oldind '$oldind'\n";

	$code =~ s/^$oldind//gm;
	$code =~ s/^/$indent/gm;
	$code =~ s/\t/$tab/g;
	return $code
}
