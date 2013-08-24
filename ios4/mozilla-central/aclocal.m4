dnl
dnl Local autoconf macros used with mozilla
dnl The contents of this file are under the Public Domain.
dnl 

builtin(include, build/autoconf/glib.m4)dnl
builtin(include, build/autoconf/libIDL.m4)dnl
builtin(include, build/autoconf/nspr.m4)dnl
builtin(include, build/autoconf/nss.m4)dnl
builtin(include, build/autoconf/pkg.m4)dnl
builtin(include, build/autoconf/freetype2.m4)dnl
builtin(include, build/autoconf/codeset.m4)dnl
builtin(include, build/autoconf/altoptions.m4)dnl
builtin(include, build/autoconf/mozprog.m4)dnl
builtin(include, build/autoconf/acwinpaths.m4)dnl

MOZ_PROG_CHECKMSYS()

# Read the user's .mozconfig script.  We can't do this in
# configure.in: autoconf puts the argument parsing code above anything
# expanded from configure.in, and we need to get the configure options
# from .mozconfig in place before that argument parsing code.
MOZ_READ_MOZCONFIG(.)
