#!/usr/bin/perl
#log file parser

use strict;
use warnings;


sub custom_print
{
	my ($indent,$f,$a,$n) = @_;
	while($indent!=0)
	{
		$indent = $indent - 1;
		print " ";
	}
	if(defined $n)
	{
		print "|".$f."(".$a.") ".$n."\n";
	}else
	{
		my $r = $f;
		print "^^ ".$r."\n"
	}
}



my ($filename,$exefilename) = @ARGV;

die("./process_txlbr.pl logfile exefilename\n") if not defined $filename;
die("./process_txlbr.pl logfile exefilename\n") if not defined $exefilename;


open(my $fh,'<:encoding(UTF-8)',$filename)
	|| die "Could not open file '$filename' $!";

my $indent=0;

# store addr2line result in hash map
my %addr2line_hash;

while (my $line = <$fh>)
{
	chomp($line);
	if($line =~ /^0x/)
	{
		my($from,$to) = split('->',$line);
		#print $from.'-----'.$to."\n";
		my $afo = $addr2line_hash{$from};
		my $ato = $addr2line_hash{$to};
		if(not defined $afo)
		{
			$afo = qx(addr2line -f -e $exefilename -a $from);
			$addr2line_hash{$from} = $afo;
		}
		if( not defined $ato)
		{
			$ato = qx(addr2line -f -e $exefilename -a $to);
			$addr2line_hash{$to} = $ato;
		}
		
		my ($faddr,$ffunc,$fflno) = split(/\n/,$afo);
		my ($taddr,$tfunc,$tflno) = split(/\n/,$ato);
		$indent = $indent + 2;
		custom_print($indent,$ffunc,$faddr,$fflno);
		custom_print($indent,$tfunc,$taddr,$tflno);
	}elsif ($line =~ /^IN_TSX/)
	{
		custom_print($indent,$line);
	}elsif ($line =~ /^MISPRED/)
	{
		custom_print($indent,$line);
	}else
	{
		$indent = 0;
		print " ^^ $line\n";
	}
}


