#
# (C) Copyright 2011-2012 Sergey A. Babkin.
# This file is a part of Triceps.
# See the file COPYRIGHT for the copyright notice and license information
#
# Simple "Hello world" examples for a table.

# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl Triceps.t'

#########################

# change 'tests => 1' to 'tests => last_test_to_print';

use ExtUtils::testlib;

use Test;
BEGIN { plan tests => 4 };
use Triceps;
ok(1); # If we made it this far, we're ok.

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

#########################
# helper functions to support either user i/o or i/o from vars

# vars to serve as input and output sources
my @input;
my $result;

# simulates user input: returns the next line or undef
sub readLine # ()
{
	$_ = shift @input;
	return $_;
}

# write a message to user
sub send # (@message)
{
	$result .= join('', @_);
}

# versions for the real user interaction
sub readLineX # ()
{
	$_ = <STDIN>;
	return $_;
}

sub sendX # (@message)
{
	print @_;
}

#########################
# Example with the direct table ops

sub helloWorldDirect()
{
	my $hwunit = Triceps::Unit->new("hwunit") or die "$!";
	my $rtCount = Triceps::RowType->new(
		address => "string",
		count => "int32",
	) or die "$!";

	my $ttCount = Triceps::TableType->new($rtCount)
		->addSubIndex("byAddress", 
			Triceps::IndexType->newHashed(key => [ "address" ])
		)
	or die "$!";
	$ttCount->initialize() or die "$!";

	my $tCount = $hwunit->makeTable($ttCount, &Triceps::EM_CALL, "tCount") or die "$!";

	while(&readLine()) {
		chomp;
		my @data = split(/\W+/);

		# the common part: find if there already is a count for this address
		my $pattern = $rtCount->makeRowHash(
			address => $data[1]
		) or die "$!";
		my $rhFound = $tCount->find($pattern) or die "$!";
		my $cnt = 0;
		if (!$rhFound->isNull()) {
			$cnt = $rhFound->getRow()->get("count");
		}

		if ($data[0] =~ /^hello$/i) {
			my $new = $rtCount->makeRowHash(
				address => $data[1],
				count => $cnt+1,
			) or die "$!";
			$tCount->insert($new) or die "$!";
		} elsif ($data[0] =~ /^count$/i) {
			&send("Received '", $data[1], "' ", $cnt + 0, " times\n");
		} elsif ($data[0] =~ /^dump$/i) {
			for (my $rhi = $tCount->begin(); !$rhi->isNull(); $rhi = $tCount->next($rhi)) {
				&send($rhi->getRow->printP(), "\n");
			}
		} elsif ($data[0] =~ /^delete$/i) {
			my $res = $tCount->deleteRow($rtCount->makeRowHash(
				address => $data[1],
			));
			die "$!" unless defined $res;
			&send("Address '", $data[1], "' is not found\n") unless $res;
		} elsif ($data[0] =~ /^remove$/i) {
			if (!$rhFound->isNull()) {
				$tCount->remove($rhFound) or die "$!";
			} else {
				&send("Address '", $data[1], "' is not found\n");
			}
		} elsif ($data[0] =~ /^clear$/i) {
			my $rhi = $tCount->begin(); 
			while (!$rhi->isNull()) {
				my $rhnext = $tCount->next($rhi);
				$tCount->remove($rhi) or die("$!");
				$rhi = $rhnext;
			}
		} else {
			&send("Unknown command '$data[0]'\n");
		}
	}
}

#########################
# test the last example

@input = (
	"Hello, table!\n",
	"Hello, world!\n",
	"Hello, table!\n",
	"count world\n",
	"Count table\n",
	"dump\n",
	"delete x\n",
	"delete world\n",
	"count world\n",
	"remove y\n",
	"remove table\n",
	"count table\n",
	"Hello, table!\n",
	"Hello, table!\n",
	"Hello, table!\n",
	"Hello, world!\n",
	"count table\n",
	"clear\n",
	"dump\n",
	"goodbye, world\n",
);
$result = undef;
&helloWorldDirect();
# XXX the result depends on the hashing order
ok($result, 
	"Received 'world' 1 times\n" .
	"Received 'table' 2 times\n" .
	"address=\"world\" count=\"1\" \n" .
	"address=\"table\" count=\"2\" \n" .
	"Address 'x' is not found\n" .
	"Received 'world' 0 times\n" .
	"Address 'y' is not found\n" .
	"Received 'table' 0 times\n" .
	"Received 'table' 3 times\n" .
	"Unknown command 'goodbye'\n"
);

#########################
# An example of a wrapper over the table class

package MyTable;
our @ISA = qw(Triceps::Table);

sub new # (class, unit, args of makeTable...)
{
	my $class = shift;
	my $unit = shift;
	my $self = $unit->makeTable(@_);
	return undef unless defined $self;
	bless $self, $class;
	return $self;
}

package main;

{
	my $hwunit = Triceps::Unit->new("hwunit") or die "$!";
	my $rtCount = Triceps::RowType->new(
		address => "string",
		count => "int32",
	) or die "$!";

	my $ttCount = Triceps::TableType->new($rtCount)
		->addSubIndex("byAddress", 
			Triceps::IndexType->newHashed(key => [ "address" ])
		)
	or die "$!";
	$ttCount->initialize() or die "$!";

	my $tCount = MyTable->new($hwunit, $ttCount, &Triceps::EM_CALL, "tCount") or die "$!";
	ok(ref $tCount, "MyTable");
}

#########################
# Example with the rowops used with the table.

sub helloWorldLabels()
{
	my $hwunit = Triceps::Unit->new("hwunit") or die "$!";
	my $rtCount = Triceps::RowType->new(
		address => "string",
		count => "int32",
	) or die "$!";

	my $ttCount = Triceps::TableType->new($rtCount)
		->addSubIndex("byAddress", 
			Triceps::IndexType->newHashed(key => [ "address" ])
		)
	or die "$!";
	$ttCount->initialize() or die "$!";

	my $tCount = $hwunit->makeTable($ttCount, &Triceps::EM_CALL, "tCount") or die "$!";

	my $lbPrintCount = $hwunit->makeLabel($tCount->getRowType(),
		"lbPrintCount", undef, sub { # (label, rowop)
			my ($label, $rowop) = @_;
			my $row = $rowop->getRow();
			&send(&Triceps::opcodeString($rowop->getOpcode), " '", 
				$row->get("address"), "', count ", $row->get("count"), "\n");
		} ) or die "$!";
	$tCount->getOutputLabel()->chain($lbPrintCount) or die "$!";

	# the updates will be sent here, for the tables to process
	my $lbTableInput = $tCount->getInputLabel();

	while(&readLine()) {
		chomp;
		my @data = split(/\W+/);

		# the common part: find if there already is a count for this address
		my $pattern = $rtCount->makeRowHash(
			address => $data[1]
		) or die "$!";
		my $rhFound = $tCount->find($pattern) or die "$!";
		my $cnt = 0;
		if (!$rhFound->isNull()) {
			$cnt = $rhFound->getRow()->get("count");
		}

		if ($data[0] =~ /^hello$/i) {
			$hwunit->schedule($lbTableInput->makeRowop(&Triceps::OP_INSERT,
				$lbTableInput->getType()->makeRowHash(
					address => $data[1],
					count => $cnt+1,
				))
			) or die "$!";
		} elsif ($data[0] =~ /^clear$/i) {
			$hwunit->schedule($lbTableInput->makeRowop(&Triceps::OP_DELETE,
				$lbTableInput->getType()->makeRowHash(address => $data[1]))
			) or die "$!";
		} else {
			&send("Unknown command '$data[0]'\n");
		}
		$hwunit->drainFrame();
	}
}

#########################
# test the last example

@input = (
	"Hello, table!\n",
	"Hello, world!\n",
	"Hello, table!\n",
	"clear, table\n",
	"Hello, table!\n",
	"goodbye, world\n",
);
$result = undef;
&helloWorldLabels();
ok($result, 
	"OP_INSERT 'table', count 1\n" .
	"OP_INSERT 'world', count 1\n" .
	"OP_DELETE 'table', count 1\n" .
	"OP_INSERT 'table', count 2\n" .
	"OP_DELETE 'table', count 2\n" .
	"OP_INSERT 'table', count 1\n" .
	"Unknown command 'goodbye'\n"
);
