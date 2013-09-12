#!/usr/bin/perl
#
#  gen_template.pl
#  Makes test case templates.
#  Takes two arguments:
#
#  -b : a bugnumber
#  -type : template type. {html|xhtml|xul|th}. defaults to html.
#
#  perl gen_template.pl -b 345876 -type xul
#
#  sends a test case template for bug 345876 to stdout
use FindBin;
use Getopt::Long;
GetOptions("b=i"=> \$bug_number,
           "type:s"=> \$template_type);

if ($template_type eq "xul") {
  $template_type = "$FindBin::RealBin/static/xul.template.txt";
} elsif ($template_type eq "xhtml") {
  $template_type = "$FindBin::RealBin/static/xhtml.template.txt";
} elsif ($template_type eq "chrome") {
  $template_type = "$FindBin::RealBin/static/chrome.template.txt";
} elsif ($template_type eq "th") {
  $template_type = "$FindBin::RealBin/static/th.template.txt";
} else {
  $template_type = "$FindBin::RealBin/static/test.template.txt";
}

open(IN,$template_type) or die("Failed to open myfile for reading.");
while((defined(IN)) && ($line = <IN>)) {
        $line =~ s/{BUGNUMBER}/$bug_number/g;
        print STDOUT $line;
}
close(IN);
