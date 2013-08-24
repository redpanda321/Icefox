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
# The Original Code is TraceMalloc.pm, released
# Nov 27, 2000.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 2000
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Chris Waterson <waterson@netscape.com>
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
package TraceMalloc;

use strict;

# Read in the type inference file and construct a network that we can
# use to match stack prefixes to types.
sub init_type_inference($) {
    my ($file) = @_;

    $::Fingerprints = { };

    open(TYPES, "<$file") || die "unable to open $::opt_types, $!";

  TYPE: while (<TYPES>) {
      next TYPE unless /<(.*)>/;
      my $type = $1;

      my $link = \%::Fingerprints;

    FRAME: while (<TYPES>) {
        chomp;
        last FRAME if /^$/;

        my $next = $link->{$_};
        if (! $next) {
            $next = $link->{$_} = {};
        }
        $link = $next;
    }

      $link->{'#type#'} = $type;

      last TYPE if eof;
  }
}

# Infer the type, trying to find the most specific type possible.
sub infer_type($) {
    my ($stack) = @_;

    my $link = \%::Fingerprints;
    my $last;
    my $type = 'void*';
  FRAME: foreach my $frame (@$stack) {
      last FRAME unless $link;

      $frame =~ s/\[.*\]$//; # ignore exact addresses, as they'll drift

      $last = $link;

      #
      # Remember this type, but keep going.  We use the longest match
      # we find, but substacks of longer matches will also match.
      #
      if ($last->{'#type#'}) {
          $type = $last->{'#type#'};
      }

      $link = $link->{$frame};

      if (! $link) {
        CHILD: foreach my $child (keys %$last) {
            next CHILD unless $child =~ /^~/;

            $child =~ s/^~//;

            if ($frame =~ $child) {
                $link = $last->{'~' . $child};
                last CHILD;
            }
          }
      }
  }

    return $type;
}


#----------------------------------------------------------------------
#
# Read in the output a trace malloc's dump. 
#
sub read {
    my ($callback, $noslop) = @_;

  OBJECT: while (<>) {
      # e.g., 0x0832FBD0 <void*> (80)
      next OBJECT unless /^0x(\S+) <(.*)> \((\d+)\)/;
      my ($addr, $type, $size) = (hex $1, $2, $3);

      my $object = { 'type' => $type, 'size' => $size };

      # Record the object's slots
      my @slots;

    SLOT: while (<>) {
        # e.g.,      0x00000000
        last SLOT unless /^\t0x(\S+)/;
        my $value = hex $1;

        # Ignore low bits, unless they've specified --noslop
        $value &= ~0x7 unless $noslop;

        $slots[$#slots + 1] = $value;
    }

      $object->{'slots'} = \@slots;

      # Record the stack by which the object was allocated
      my @stack;

      while (/^(.*)\[(.*) \+0x(\S+)\]$/) {
          # e.g., _dl_debug_message[/lib/ld-linux.so.2 +0x0000B858]
          my ($func, $lib, $off) = ($1, $2, hex $3);

          chomp;
          $stack[$#stack + 1] = $_;

          $_ = <>;
      }

      $object->{'stack'} = \@stack;

      $object->{'type'} = infer_type(\@stack)
          if $object->{'type'} eq 'void*';

      &$callback($object) if $callback;

      # Gotta check EOF explicitly...
      last OBJECT if eof;
  }
}

1;
__END__

=head1 NAME

TraceMalloc - Perl routines to deal with output from ``trace malloc''
and the Boehm GC

=head1 SYNOPSIS

    use TraceMalloc;

    TraceMalloc::init_type_inference("types.dat");
    TraceMalloc::read(0);

=head1 DESCRIPTION

=head1 EXAMPLES

=cut
