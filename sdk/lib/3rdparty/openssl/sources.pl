#!/usr/bin/env perl
# PROJECT:     LiberNT Build System
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     List the C sources of OpenSSL libraries, or its generated files, in a configured build directory
# COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>

use strict;
use warnings;

my ($config, @libraries) = @ARGV;
die "usage: sources.pl <configured build directory> generated|<library>...\n" unless defined $config && @libraries;
unshift @INC, $config;
require configdata;
my $info = do { no warnings "once"; \%configdata::unified_info };

my %seen;
sub sources {
    my ($item) = @_;
    my @result;
    return @result if $seen{$item}++;
    foreach my $source (@{$info->{sources}{$item} // []}) {
        if ($source =~ /\.(?:o|a)$/) {
            push @result, sources($source);
        } elsif ($source =~ /\.c$/) {
            push @result, $source;
        }
    }
    if ($item =~ /\.a$/) {
        push @result, sources($_) foreach grep { /\.a$/ } @{$info->{depends}{$item} // []};
    }
    return @result;
}

if ($libraries[0] eq "generated") {
    print "$_\n" foreach sort grep { /\.[ch]$/ } keys %{$info->{generate}};
} else {
    print "$_\n" foreach map { sources($_) } @libraries;
}
