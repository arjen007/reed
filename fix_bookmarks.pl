#!/usr/bin/perl -w
# This script will update your bookmarks file from Reed 4.x's to Reed 5.x's
# file format.

open BOOK, "$ENV{HOME}/.reed_bookmarks";
@book = <BOOK>;
close BOOK;
open BOOK, ">$ENV{HOME}/.reed_bookmarks";
foreach (@book) {
 if (/(.*);(\d+) (.*)$/) { print BOOK "$1\t$2\t$3\n"; }
 else { print BOOK $_; }
}
close BOOK;
