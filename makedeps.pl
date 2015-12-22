#!/usr/bin/perl

# Put the c file(s) containing the "main" function as a parameter. This script
# assumes the following dependencies:
#
# 1. Any object file depends on its corresponding c file, e.g. foo.o depends on
#    foo.c.
# 2. Any .c file depends on all the .h files it includes.
# 3. If foo.c file includes bar.h and bar.c exists, we assume that any program
#    depending on foo.o also depends on bar.o

my @programs = ();
my $includes_by_file = {}; # foo.c => [foo.h, bar.h, baz.h]
my %link_deps_by_o   = (); # foo.o => [bar.o, baz.o]

foreach my $program_c (@ARGV) {
	next unless $program_c =~ /^(.*)\.c$/;
	my $program = $1;
	push @programs, ($program);
	my @stack = ($program_c);
	while ($sourcefile = pop @stack) {
		next if exists $includes_by_file{$sourcefile};
		my $includes = [];
		open $fh, "<", $sourcefile;
		while (<$fh>) {
			if (/^\s*\#\s*include\s*"([^\"]+?)\.h"/) {
				my $h = "$1.h";
				push @$includes, ($h);
				push @stack, ($h);
				my $c = "$1.c";
				if ($c ne $sourcefile && -e $c) {
					push @stack, ($c);
				}
			}
		}
		close $fh;
		$includes_by_file->{$sourcefile} = $includes;
	}
}

# Print Makefile targets in appropriate order.

print "# Rules generated by $0\n\n";

# The 'all' target.
#print "# all: ", join(" ", @programs), "\n\n";

# A target for each program with .o deps.
print "# Linking dependencies\n";
foreach my $program (@programs) {
	# Find all .c files that this depends on. Then change them to .o files.
	my @deps_stack = ("$program.c");
	my @link_deps = ("$program.o");
	my %dep_handled = ();
	while (my $dep = pop @deps_stack) {
		next if exists $dep_handled{$dep};
		$dep_handled{$dep} = true;
		if ($dep =~ /^(.*)\.h$/ && -e "$1.c") {
			push @link_deps, ("$1.o");
		}
		$includes = $includes_by_file->{$dep};
		push @deps_stack, @$includes;
	}
	$link_deps_str = join(" ", @link_deps);
	print "$program: $link_deps_str\n";
	print "\t\$(CC) \$(LDFLAGS) -o $program \$^\n\n";
}

# Compile deps.
print "# Compilation dependencies\n";
foreach my $cfile (keys %$includes_by_file) {
	next unless $cfile =~ /^(.*?)\.c/;
	my $object = "$1.o";
	my %dep_handled = ();
	my @stack = ($cfile);
	while (my $dep = pop @stack) {
		next if exists $dep_handled{$dep};
		$dep_handled{$dep} = true;
		my $includes = $includes_by_file->{$dep};
		push @stack, @$includes;
	}
	print("$object: ", join(" ", keys %dep_handled), "\n");
}
