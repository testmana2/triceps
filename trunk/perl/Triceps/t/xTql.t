#
# (C) Copyright 2011-2012 Sergey A. Babkin.
# This file is a part of Triceps.
# See the file COPYRIGHT for the copyright notice and license information
#
# The example of queries in TQL (Triceps/Trivial Query Language).

#########################

use ExtUtils::testlib;

use Test;
BEGIN { plan tests => 2 };
use Triceps;
use Triceps::X::TestFeed qw(:all);
use Carp;
ok(1); # If we made it this far, we're ok.

use strict;

#########################
# The line-splitting.

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

#########################
# Common Triceps types.

# The basic table type to be used for querying.
# Represents the trades reports.
our $rtTrade = Triceps::RowType->new(
	id => "int32", # trade unique id
	symbol => "string", # symbol traded
	price => "float64",
	size => "float64", # number of shares traded
) or confess "$!";

our $ttWindow = Triceps::TableType->new($rtTrade)
	->addSubIndex("bySymbol", 
		Triceps::SimpleOrderedIndex->new(symbol => "ASC")
			->addSubIndex("last2",
				Triceps::IndexType->newFifo(limit => 2)
			)
	)
	or confess "$!";
$ttWindow->initialize() or confess "$!";

# Represents the static information about a company.
our $rtSymbol = Triceps::RowType->new(
	symbol => "string", # symbol name
	name => "string", # the official company name
	eps => "float64", # last quarter earnings per share
) or confess "$!";

our $ttSymbol = Triceps::TableType->new($rtSymbol)
	->addSubIndex("bySymbol", 
		Triceps::IndexType->newHashed(key => [ "symbol" ])
	)
	or confess "$!";
$ttSymbol->initialize() or confess "$!";

#########################
# Server with module version 1.

use Triceps::X::Braced qw(:all);
use Safe;

sub tqlRead # ($ctx, @args)
{
	my $ctx = shift;
	die "The read command may not be used in the middle of a pipeline.\n" 
		if (defined($ctx->{prev}));
	my $opts = {};
	# XXX add ways to unquote when option parsing?
	&Triceps::Opt::parse("read", $opts, {
		table => [ undef, \&Triceps::Opt::ck_mandatory ],
	}, @_);

	my $fret = $ctx->{fretDumps};
	my $tabname = bunquote($opts->{table});

	die ("Read found no such table '$tabname'\n")
		unless (exists $ctx->{tables}{$tabname});
	my $unit = $ctx->{u};
	my $table = $ctx->{tables}{$tabname};
	my $lab = $unit->makeDummyLabel($table->getRowType(), "lb" . $ctx->{id} . "read");
	$ctx->{next} = $lab;

	my $code = sub {
		Triceps::FnBinding::call(
			name => "bind" . $ctx->{id} . "read",
			unit => $unit,
			on => $fret,
			labels => [
				$tabname => $lab,
			],
			code => sub {
				$table->dumpAll();
			},
		);
	};
	push @{$ctx->{actions}}, $code;
}

sub tqlProject # ($ctx, @args)
{
	my $ctx = shift;
	die "The project command may not be used at the start of a pipeline.\n" 
		unless (defined($ctx->{prev}));
	my $opts = {};
	&Triceps::Opt::parse("project", $opts, {
		fields => [ undef, \&Triceps::Opt::ck_mandatory ],
	}, @_);
	
	my $patterns = split_braced_final($opts->{fields});

	my $rtIn = $ctx->{prev}->getRowType();
	my @inFields = $rtIn->getFieldNames();
	my @pairs =  &Triceps::Fields::filterToPairs("project", \@inFields, $patterns);
	my ($rtOut, $projectFunc) = &Triceps::Fields::makeTranslation(
		rowTypes => [ $rtIn ],
		filterPairs => [ \@pairs ],
	);

	my $unit = $ctx->{u};
	my $lab = $unit->makeDummyLabel($rtOut, "lb" . $ctx->{id} . "project");
	my $labin = $unit->makeLabel($rtIn, "lb" . $ctx->{id} . "project.in", undef, sub {
		$unit->call($lab->makeRowop($_[1]->getOpcode(), &$projectFunc($_[1]->getRow()) ));
	});
	$ctx->{prev}->chain($labin);
	$ctx->{next} = $lab;
}

sub tqlPrint # ($ctx, @args)
{
	my $ctx = shift;
	die "The print command may not be used at the start of a pipeline.\n" 
		unless (defined($ctx->{prev}));
	my $opts = {};
	&Triceps::Opt::parse("print", $opts, {
		tokenized => [ 1, undef ],
	}, @_);
	my $tokenized = bunquote($opts->{tokenized}) + 0;

	# XXX This gets the printed label name from the auto-generated label name,
	# which is not a good practice.
	# XXX Should have a custom query name somewhere in the context?
	my $prev = $ctx->{prev};
	if ($tokenized) {
		# print in the tokenized format
		my $lab = $ctx->{u}->makeLabel($prev->getRowType(), 
			"lb" . $ctx->{id} . "print", undef, sub {
				&Triceps::X::SimpleServer::outCurBuf($_[1]->printP() . "\n");
			});
		$prev->chain($lab);
	} else {
		my $lab = Triceps::X::SimpleServer::makeServerOutLabel($ctx->{prev});
	}

	# The end-of-data notification. It will run after the current pipeline
	# finishes.
	my $prevname = $prev->getName();
	push @{$ctx->{actions}}, sub {
		&Triceps::X::SimpleServer::outCurBuf("+EOD,OP_NOP,$prevname\n");
	};

	$ctx->{next} = undef; # end of the pipeline
}

sub tqlJoin # ($ctx, @args)
{
	my $ctx = shift;
	die "The join command may not be used at the start of a pipeline.\n" 
		unless (defined($ctx->{prev}));
	my $opts = {};
	&Triceps::Opt::parse("join", $opts, {
		table => [ undef, \&Triceps::Opt::ck_mandatory ],
		rightIdxPath => [ undef, \&Triceps::Opt::ck_mandatory ],
		by => [ undef, undef ],
		byLeft => [ undef, undef ],
		leftFields => [ undef, undef ],
		rightFields => [ undef, undef ],
		type => [ "inner", undef ],
	}, @_);

	my $tabname = bunquote($opts->{table});
	die ("Join found no such table '$tabname'\n")
		unless (exists $ctx->{tables}{$tabname});
	my $table = $ctx->{tables}{$tabname};

	&Triceps::Opt::checkMutuallyExclusive("join", 1, "by", $opts->{by}, "byLeft", $opts->{byLeft});
	my $by = split_braced_final($opts->{by});
	my $byLeft = split_braced_final($opts->{byLeft});

	my $rightIdxPath = split_braced_final($opts->{rightIdxPath});

	my $isLeft = 0; # default for inner join
	my $type = $opts->{type};
	if ($type eq "inner") {
		# already default
	} elsif ($type eq "left") {
		$isLeft = 1;
	} else {
		die "Unsupported value '$type' of option 'type'.\n"
	}

	my $leftFields = split_braced_final($opts->{leftFields});
	my $rightFields = split_braced_final($opts->{rightFields});

	# Build the filtering-out of the duplicate key fields on the right.
	# Similar to what JoinTwo does.
	my($rightIdxType, @rightkeys) = $table->getType()->findIndexKeyPath(@$rightIdxPath);
	if (!defined($rightFields)) {
		$rightFields = [ ".*" ]; # the implicit pass-all
	}
	unshift(@$rightFields, map("!$_", @rightkeys) );

	my $unit = $ctx->{u};
	my $join = Triceps::LookupJoin->new(
		name => "join" . $ctx->{id},
		unit => $unit,
		leftFromLabel => $ctx->{prev},
		rightTable => $table,
		rightIdxPath => $rightIdxPath,
		leftFields => $leftFields,
		rightFields => $rightFields,
		by => $by,
		byLeft => $byLeft,
		isLeft => $isLeft,
	);
	
	$ctx->{next} = $join->getOutputLabel();
}

# Replace a field name with the code that would get the field
# from a variable containing a row. The row definition in hash
# formatr is used to check up-front that the field exists.
sub replaceFieldRef # (\%def, $field)
{
	my $def = shift;
	my $field = shift;
	die "Unknown field '$field'; have fields: " . join(", ", keys %$def) . ".\n"
		unless (exists ${$def}{$field});
	#return '$_[0]->get("' . quotemeta($field) . '")';
	return '&Triceps::Row::get($_[0], "' . quotemeta($field) . '")';
}

sub tqlFilter # ($ctx, @args)
{
	my $ctx = shift;
	die "The filter command may not be used at the start of a pipeline.\n" 
		unless (defined($ctx->{prev}));
	my $opts = {};
	&Triceps::Opt::parse("filter", $opts, {
		expr => [ undef, \&Triceps::Opt::ck_mandatory ],
	}, @_);

	# Here only the keys (field names) will be important, the values
	# (field types) will be ignored.
	my $rt = $ctx->{prev}->getRowType();
	my %def = $rt->getdef();

	my $expr = bunquote($opts->{expr});
	$expr =~ s/\$\%(\w+)/&replaceFieldRef(\%def, $1)/ge;

	my $safe = new Safe; 
	# This allows for the exploits that run the process out of memory,
	# but the danger is in the highly useful functions, so better take this risk.
	$safe->permit(qw(:base_core :base_mem :base_math sprintf));
	$safe->share('Triceps::Row::get');

	my $compiled = $safe->reval("sub { $expr }", 1);
	die "$@" if($@);

	my $unit = $ctx->{u};
	my $lab = $unit->makeDummyLabel($rt, "lb" . $ctx->{id} . "filter");
	my $labin = $unit->makeLabel($rt, "lb" . $ctx->{id} . "filter.in", undef, sub {
		if (&$compiled($_[1]->getRow())) {
			$unit->call($lab->adopt($_[1]));
		}
	});
	$ctx->{prev}->chain($labin);
	$ctx->{next} = $lab;
}

	
our %tqlDispatch = (
	read => \&tqlRead,
	project => \&tqlProject,
	print => \&tqlPrint,
	join => \&tqlJoin,
	filter => \&tqlFilter,
);

sub tqlQuery # (\%tables, $fretDumps, $argline)
{
	my $tables = shift;
	my $fretDumps = shift;
	my $s = shift;

	$s =~ s/^([^,]*)(,|$)//; # skip the name of the label
	my $q = $1; # the name of the query itself
	#&Triceps::X::SimpleServer::outCurBuf("+DEBUGquery: $s\n");
	my @cmds = split_braced($s);
	if ($s ne '') {
		&Triceps::X::SimpleServer::outCurBuf("+ERROR,OP_INSERT,$q: mismatched braces in the trailing $s\n");
		return
	}

	# The context for the commands to build up an execution of a query.
	my $ctx = {};
	# The query will be built in a separate unit
	$ctx->{tables} = $tables;
	$ctx->{fretDumps} = $fretDumps;
	$ctx->{u} = Triceps::Unit->new("u_${q}");
	$ctx->{prev} = undef; # will contain the output of the previous command in the pipeline
	$ctx->{actions} = []; # code that will run the pipeline
	$ctx->{id} = 0; # a unique id for auto-generated objects

	my $cleaner = $ctx->{u}->makeClearingTrigger();
	if (! eval {
		foreach my $cmd (@cmds) {
			#&Triceps::X::SimpleServer::outCurBuf("+DEBUGcmd, $cmd\n");
			my @args = split_braced($cmd);
			my $argv0 = bunquote(shift @args);
			# The rest of @args do not get unquoted here!
			die "No such TQL command '$argv0'\n" unless exists $tqlDispatch{$argv0};
			# XXX do something better with the errors, show the failing command...
			$ctx->{id}++;
			&{$tqlDispatch{$argv0}}($ctx, @args);
			# Each command must set its result label (even if an undef) into
			# $ctx->{next}.
			die "Internal error in the command $argv0: missing result definition\n"
				unless (exists $ctx->{next});
			$ctx->{prev} = $ctx->{next};
			delete $ctx->{next};
		}
		if (defined $ctx->{prev}) {
			# implicitly print the result of the pipeline, no options
			&{$tqlDispatch{"print"}}($ctx);
		}

		# Now run the pipeline
		foreach my $code (@{$ctx->{actions}}) {
			&$code;
		}

		# Now run the pipeline
		1; # means that everything went OK
	}) {
		# XXX this won't work well with the multi-line errors
		&Triceps::X::SimpleServer::outCurBuf("+ERROR,OP_INSERT,$q: error: $@\n");
		return
	}
}

# Build an FnReturn with dump labels of all the tables.
# The labels in return will be named as their table.
# @param name - name for the FnReturn
# @param \%tables - a hash (table name => table object), the name from this
#     hash will be used for FnReturn (it should probably be the same as the
#     table name but no guarantees there).
sub collectDumps # ($name, \%tables)
{
	my $name = shift;
	my $tables = shift;
	my @labels;
	my ($k, $v);
	while (($k, $v) = each %$tables) {
		push @labels, $k, $v->getDumpLabel();
	}
	return Triceps::FnReturn->new(
		name => $name,
		labels => \@labels,
	);
}

sub runTqlQuery1
{

my $uTrades = Triceps::Unit->new("uTrades");
my $tWindow = $uTrades->makeTable($ttWindow, "EM_CALL", "tWindow")
	or confess "$!";
my $tSymbol = $uTrades->makeTable($ttSymbol, "EM_CALL", "tSymbol")
	or confess "$!";

# The information about tables, for querying.
my %tables;
$tables{$tWindow->getName()} = $tWindow;
$tables{$tSymbol->getName()} = $tSymbol;
my $fretDumps = collectDumps("fretDumps", \%tables);

my %dispatch;
$dispatch{$tWindow->getName()} = $tWindow->getInputLabel();
$dispatch{$tSymbol->getName()} = $tSymbol->getInputLabel();
$dispatch{"query"} = sub { tqlQuery(\%tables, $fretDumps, @_); };
$dispatch{"exit"} = \&Triceps::X::SimpleServer::exitFunc;

Triceps::X::DumbClient::run(\%dispatch);
};

# the same input and result gets reused mutiple times
my @inputQuery1 = (
	"tSymbol,OP_INSERT,AAA,Absolute Auto Analytics Inc,0.5\n",
	"tWindow,OP_INSERT,1,AAA,10,10\n",
	"tWindow,OP_INSERT,3,AAA,20,20\n",
	"tWindow,OP_INSERT,5,AAA,30,30\n",
	"query,{read table tSymbol}\n",
	"query,{read table tWindow} {project fields {symbol price}} {print tokenized 0}\n",
	"query,{read table tWindow} {project fields {symbol price}}\n",
	"query,{read table tWindow} {join table tSymbol rightIdxPath bySymbol byLeft {symbol}}\n",
	"query,{read table tWindow} {filter expr {\$%price == 20}}\n",
);
my $expectQuery1 = 
'> tSymbol,OP_INSERT,AAA,Absolute Auto Analytics Inc,0.5
> tWindow,OP_INSERT,1,AAA,10,10
> tWindow,OP_INSERT,3,AAA,20,20
> tWindow,OP_INSERT,5,AAA,30,30
> query,{read table tSymbol}
> query,{read table tWindow} {project fields {symbol price}} {print tokenized 0}
> query,{read table tWindow} {project fields {symbol price}}
> query,{read table tWindow} {join table tSymbol rightIdxPath bySymbol byLeft {symbol}}
> query,{read table tWindow} {filter expr {$%price == 20}}
lb1read OP_INSERT symbol="AAA" name="Absolute Auto Analytics Inc" eps="0.5" 
+EOD,OP_NOP,lb1read
lb2project,OP_INSERT,AAA,20
lb2project,OP_INSERT,AAA,30
+EOD,OP_NOP,lb2project
lb2project OP_INSERT symbol="AAA" price="20" 
lb2project OP_INSERT symbol="AAA" price="30" 
+EOD,OP_NOP,lb2project
join2.out OP_INSERT id="3" symbol="AAA" price="20" size="20" name="Absolute Auto Analytics Inc" eps="0.5" 
join2.out OP_INSERT id="5" symbol="AAA" price="30" size="30" name="Absolute Auto Analytics Inc" eps="0.5" 
+EOD,OP_NOP,join2.out
lb2filter OP_INSERT id="3" symbol="AAA" price="20" size="20" 
+EOD,OP_NOP,lb2filter
';

setInputLines(@inputQuery1);
&runTqlQuery1();
#print &getResultLines();
ok(&getResultLines(), $expectQuery1);

