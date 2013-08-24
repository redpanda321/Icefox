##########################################################################################
#
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   2/10/00 attinasi
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

# 1) Load Mozilla with an argument indicating where to find the URLs to cycle through
# 2) Uncombine the logfile into a log for each site
# 3) Run the script genfromlogs.pl to process the logs and generate the chart
# 4) Rename the chart to the ID provided
# 5) Run the script history.pl to generate the history chart
# 6) Move the files to a directory just for them...

$UseViewer = 1;    # set to 0 to use Mozilla
$UseClockTime=0;   # if third argument is 'clock' this gets set to 1

# Get and check the arguments
@ARGV;
$ID = $ARGV[0];
$bldRoot = $ARGV[1];
$clockTimeArg = $ARGV[2];
$profID = $ARGV[3];
$arg=0;
$cmdLineArg[$arg++] = "-f ";
##$cmdLineArg[$arg++] = "S:\\mozilla\\tools\\performance\\layout\\40-url-dup.txt ";
$cmdLineArg[$arg++] = "D:\\mozilla\\tools\\performance\\layout\\40-url.txt ";
if($UseViewer==0){
  $cmdLineArg[$arg++] = "-ftimeout ";
  $cmdLineArg[$arg++] = "10 ";
  $cmdLineArg[$arg++] = "-P ";
  $cmdLineArg[$arg++] = "$profID ";
}

if($UseViewer==0){
  if(!$ID || !$bldRoot || !$profID || !$clockTimeArg){
    die "ID, Build Root, Profile Name, and clock OR CPU must be provided. \n - Example: 'perl perf.pl Daily_021400 s:\\moz\\daily\\0214  clock|cpu Attinasi' \n";
  }
} else {
  if(!$ID || !$bldRoot || !$clockTimeArg){
    die "ID, Build Root, and clock OR CPU must be provided. \n - Example: 'perl perf.pl Daily_021400 s:\\moz\\daily\\0214 clock|CPU' \n";
  }
}

# build up a full path and argument strings for the invocation
$mozPath;
$logName;
if($UseViewer==0){
  $mozPath=$bldRoot."\\bin\\Mozilla.exe";
} else {
  $mozPath=$bldRoot."\\bin\\Viewer.exe";
}
$logName="Logs\\".$ID."-combined.dat";

-e $mozPath or die "$mozPath could not be found\n";

if($clockTimeArg eq "clock"){
  $UseClockTime=1;
  print( "Charting Clock time...\n");
  die "Clock Time Does Not Yet Work! <sorry>";
} else {
  print( "Charting CPU time...\n");
}

# run it
$commandLine = $mozPath." ".$cmdLineArg[0].$cmdLineArg[1].$cmdLineArg[2].$cmdLineArg[3].$cmdLineArg[4].$cmdLineArg[5]."> $logName";
print("Running $commandLine\n");
system ("$commandLine") == 0 or die "Cannot invoke Mozilla\n";

# uncombine the output file
print("Breaking result into log files...\n");
system( ("perl", "uncombine.pl", "$logName") ) == 0 or die "Error uncombining the output\n";

# generate the chart from the logs
if($UseClockTime==1){
  print("Generating performance table... (ClockTime)\n");
  system( ("perl", "genfromlogs.pl", "$ID", "$bldRoot", "clock") ) == 0 or die "Error generating the clock-time chart from the logs\n";
} else {
  print("Generating performance table...(CPUTime)\n");
  system( ("perl", "genfromlogs.pl", "$ID", "$bldRoot") ) == 0 or die "Error generating the cpu-time chart from the logs\n";
}

# rename the output
system( ("copy", "table.html", "Tables\\$ID\.html") ) == 0 or die "Error copying table.html to Tables\\$ID\.html\n";

# run the history script
print("Generating history table...\n");
system( ("perl", "history.pl") ) == 0 or die "Could not execute the history.pl script\n";

# rename the output
system( ("copy", "TrendTable.html", "Tables\\$ID-TrendTable.html") ) == 0 or die "could not copy TrendTable.html to Tables\\$ID-TrendTable.html\n";

# save off the files
system( ("mkdir", "Logs\\$ID") );
system( ("copy", "Logs\\*.*", "Logs\\$ID\\*.*") ) == 0 or die "Cannot copy logfiles to Logs\\$ID\\*.*\n";
system( ("copy", "history.txt", "Logs\\$ID-history.txt") ) == 0 or die "Cannot copy $ID_history.txt to Logs\n";

print("perf.pl DONE!\n");
