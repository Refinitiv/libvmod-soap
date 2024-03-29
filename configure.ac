AC_PREREQ([2.69])
AC_INIT([libvmod-soap],[1.0],[],[vmod-soap])
AC_COPYRIGHT([Copyright (c) 2019, Refinitiv])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR(src/vmod_soap.vcc)
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 -Wall -Werror foreign parallel-tests])
AM_SILENT_RULES([yes])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

LT_PREREQ([2.2.6])
LT_INIT([dlopen disable-static])

AX_PTHREAD(,[AC_MSG_ERROR([Could not configure pthreads support])])

LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_ARG_WITH([rst2man],
	AS_HELP_STRING(
		[--with-rst2man=PATH],
		[Location of rst2man (auto)]),
	[RST2MAN="$withval"],
	[AC_CHECK_PROGS(RST2MAN, [rst2man rst2man.py], [])])

m4_ifndef([VARNISH_PREREQ], AC_MSG_ERROR([Need varnish.m4 -- see README.md]))

VARNISH_PREREQ([4.1], [5.0.0])
VARNISH_VMODS([soap])

AC_SUBST([VARNISHSRC])
AC_ARG_VAR([VARNISHSRC], [path to Varnish source tree])
AS_IF([test "x${VARNISHSRC}" = "x" ], AC_MSG_FAILURE([\$VARNISHSRC is not set]), AC_MSG_RESULT([yes]))
AC_CHECK_FILE([$VARNISHSRC/include/vrt.h],
        [],
        [AC_MSG_FAILURE([\$VARNISHSRC "$VARNISHSRC" is not a Varnish source directory])]
)

VMOD_TESTS="$(cd $srcdir/src && echo tests/*.vtc)"
AC_SUBST(VMOD_TESTS)

PKG_CHECK_VAR([LIBVARNISHAPI_LIBDIR], [varnishapi], [libdir])
AC_SUBST([VARNISH_LIBRARY_PATH],
	[$LIBVARNISHAPI_LIBDIR:$LIBVARNISHAPI_LIBDIR/varnish])

PKG_CHECK_EXISTS([apr-1], [apr_found=1], [AC_MSG_ERROR([Unable to find apr-devel])])
PKG_CHECK_EXISTS([libxml-2.0], [xml_found=1], [AC_MSG_ERROR([Unable to find libxml2-devel])])

AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
