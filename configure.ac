#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([LJQO Plugin], [0.3], [alange0001@gmail.com])
AC_COPYRIGHT([Copyright 2009-2014, Adriano Lange])
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADER([include/ljqo_config.h])
AC_PREFIX_DEFAULT(/usr/local/pgsql)

# Checks for programs.
AC_PROG_CC([cc gcc])
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([limits.h])

AC_ARG_WITH([pgsql],
	    [AC_HELP_STRING([--with-pgsql=PREFIX],[PostgreSQL installation prefix])],
	    [arg_pgsql=$withval],
	    [arg_pgsql=""])

if test -z "$arg_pgsql"; then
	AC_PATH_PROG(PG_CONFIG, pg_config)
	if test ! -z "$PG_CONFIG"; then
		pg_includedir="`$PG_CONFIG --includedir-server`"
		pg_libdir="`$PG_CONFIG --pkglibdir`"
	fi
fi
if test -z "$pg_includedir"; then
	for i in $arg_pgsql $prefix /usr; do
		for j in $i/bin $i; do
			AC_PATH_PROG(PG_CONFIG, pg_config, "", "$j")
			if test ! -z "$PG_CONFIG"; then
				pg_includedir="`$PG_CONFIG --includedir-server`"
				pg_libdir="`$PG_CONFIG --pkglibdir`"
				j_ok="1";
				break;
			else
				j_ok="0";
			fi
		done
		if test "$j_ok" == "1"; then
			break;
		fi
	done
fi

if test ! -z "$pg_includedir"; then
	CPPFLAGS="$CPPFLAGS -I$pg_includedir"
fi
AC_CHECK_HEADERS([postgres.h],
				 [],
				 [AC_MSG_FAILURE([Cannot find PostgreSQL header files!
Use --with-pgsql=PREFIX option to specify PostgreSQL installation prefix.])])


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
AC_HEADER_STDC

# Optional Features
AC_MSG_CHECKING([whether to build with optimizer evaluation support])
AC_ARG_ENABLE(opte,
  [  --enable-opte           enable optimizer evaluation support],
  [ 
    AC_DEFINE([ENABLE_OPTE], 1, [Define to 1 if you want optimizer evaluation support. (--enable-opte)])
    AC_SUBST(OPTE_SUBDIR, opte)
    AC_SUBST(OPTE_OBJ, opte/libopte.la)
  ])
AC_MSG_RESULT([$enable_opte])
AC_MSG_CHECKING([whether to build with debuggraph support])
AC_ARG_ENABLE(debuggraph,
  [  --enable-debuggraph     enable debuggraph support],
  [ 
    AC_DEFINE([ENABLE_DEBUGGRAPH], 1, [Define to 1 if you want debuggraph support. (--enable-debuggraph)])
    AC_SUBST(DEBUGGRAPH_SUBDIR, debuggraph)
    AC_SUBST(DEBUGGRAPH_OBJ, debuggraph/libdebuggraph.la)
  ])
AC_MSG_RESULT([$enable_debuggraph])


# Setting directories
AC_SUBST(libdir,"$pg_libdir")

# 
AC_CONFIG_FILES([
	Makefile
	include/Makefile
	src/Makefile
	src/sdp/Makefile
	src/twopo/Makefile
	src/opte/Makefile
	src/debuggraph/Makefile
])
AC_OUTPUT

