#!/usr/bin/perl
#
# upload.pl - upload files to the xine "Download" page
#
# usage: upload.pl [-y] [-r<release-version>] [-s<section>] <file> [...]
#
# OPTIONS: release-version: the version number of this file. If omitted, the
#                           newest version on the page is used.
#                           Only the last -r option counts.
#
#          section: the heading under which this file should appear. If
#                   omitted, we'll try to autodetect it from the file suffix:
#                   .rpm    => "RPMs"
#                   .tar.gz => "source tarball"
#                   .deb    => "Debian"
#                   .dsc    => "Debian"
#
#          -y: don't ask for upload confirmation
#
# EXAMPLE:
# ./upload.pl -r1.0 xine_1.0_i386.deb -sSlackware xine-1.0.tar.gz
#

## begin configuration ######################################################

$UPLOAD_HOST  = "xine.sourceforge.net";
$UPLOAD_PATH  = "/home/groups/x/xi/xine/htdocs";
$INDEX_FILE   = "download.html";
$FILE_SUBDIR  = "files";
$TMP_DIR      = "/tmp/xine-upload.$$";
$PROGRAM_NAME = "xine";
$LINK_FONT    = '<font face="Verdana,Helvetica,Arial" size=4>';
$END_FONT     = '</font>';

## end of configuration #####################################################

mkdir $TMP_DIR, 0755 || die "could not mkdir $TMP_DIR";
mkdir "$TMP_DIR/$FILE_SUBDIR", 0755;

print "fetching old Download page...\n";
system ("scp $UPLOAD_HOST:$UPLOAD_PATH/$INDEX_FILE $TMP_DIR");
open(IN, "$TMP_DIR/$INDEX_FILE") || die "unable to open $TMP_DIR/$INDEX_FILE";
if (!read(IN, $thepage, 409600)) {
  die "unable to read $TMP_DIR/$INDEX_FILE";
} 
close(IN);

if ($thepage =~ /<h1>\s*$PROGRAM_NAME\s+(\S+)\s*<\/h1>/si) {
  $current_release=$1;
  print "current release is $current_release.\n";
}
else {
  $current_release="(unknown version)";
  print "WARNING: could not find current release in download page.\n";
}

## parse command line
$section  = "source tarball";
$release  = $current_release;
%files    = ();
$yes      = 0;

foreach (@ARGV) {
  if (/^\-r/) {
    $release = $_;
    $release =~ s/\-r//;
  }
  elsif (/^\-s/) {
    $section = $_;
    $section =~ s/\-s//;
  }
  elsif (/^\-y/) {
    $yes = 1;
  }
  else {
    die "$_: no such file" unless -f $_;
    $filen = $_;
    $filen =~ s/^.*?([^\/]*)$/\1/;
    $files{$section} .= " $filen"; 
    print "will add $filen to section $section.\n"; 
    system("cp $_ '$TMP_DIR/$FILE_SUBDIR'");
 }
}
print "using release id $release.\n";

## add new files to the page
sub ensureSection() { #$section, $release 
  $rel = $thepage;
  $rel =~ s/^.*<h1>\s*$PROGRAM_NAME\s+$release\s*<\/h1>//si;
  $rel =~ s/<h1>.*$//si;
  if ($rel =~ /<h2>\s*$section\s*<\/h2>/si) {
    return;
  }
  else {
    print "creating new section \"$section\" for release $release.\n";
    unless ($thepage =~ /<h1>\s*$PROGRAM_NAME\s+$release\s*<\/h1>/si) {
      $thepage = "<h1>$PROGRAM_NAME $release</h1>\n" . $thepage;
    }
    $newSection = "<h2>$section</h2>\n\n$LINK_FONT\n$END_FONT\n\n\n";
    $thepage =~ /^(.*<h1>\s*$PROGRAM_NAME\s+$release\s*<\/h1>.*?)(<h1>.*)$/is;
    $thepage = $1.$newSection.$2;
    return;
  }
}

print "editing Download page...\n";
foreach (keys(%files)) {
  $section = $_; 
  ensureSection();
  $newLinks = $files{$section};
  print "file list for \"$section\": $newLinks\n";
  $newLinks =~ s/ (\S*)/<a href=\"$FILE_SUBDIR\/\1\">\1<\/a> <br>\n/sg;
  $thepage =~ /^(.*<h1>\s*$PROGRAM_NAME\s+$release\s*<\/h1>.*?<h2>\s*$section\s*<\/h2>.*?)($END_FONT.*)$/is;
  $thepage = $1 . $newLinks . $2;
}

open(OUT, ">$TMP_DIR/$INDEX_FILE");
print OUT $thepage;
close(OUT);

unless ($yes) {
  print "\nAre you sure you want to upload files? [no] ";
  $answer = <STDIN>;
  $yes = 1 if $answer =~ /^y/i;
}

if ($yes) {
  system("scp -r '$TMP_DIR/$INDEX_FILE' '$TMP_DIR/$FILE_SUBDIR' '$UPLOAD_HOST:$UPLOAD_PATH'");
}

system("rm -r $TMP_DIR");
