#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([LJQO Plugin], [0.1], [alange0001@gmail.com])
AC_COPYRIGHT([Copyright 2009, Adriano Lange])
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
	    [arg_pgsql=$prefix])

for pgdir in $arg_pgsql $prefix; do
	for pginclude in $pgdir/include $pgdir/include/server $pgdir/include/postgresql/server; do
		AC_MSG_CHECKING([for postgres.h in "$pginclude"])
		if [ test -f "$pginclude/postgres.h" ]; then
			AC_MSG_RESULT([yes]);
			CPPFLAGS="$CPPFLAGS -I$pginclude";
			pginclude_ok="1";
			break;
		else
			pginclude_ok="0";
			AC_MSG_RESULT([no]);
		fi
	done
	if [ test "$pginclude_ok" == "1" ]; then
		break;
	fi
done

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
    AC_DEFINE([ENABLE_OPTE], 1, [Define to 1 if you optimizer evaluation support. (--enable-opte)])
    AC_SUBST(OPTE_SUBDIR, opte)
    AC_SUBST(OPTE_OBJ, opte/libopte.la)
  ])
AC_MSG_RESULT([$enable_opte])


# Setting directories
AC_SUBST(libdir,$pgdir/lib/plugins)
AC_SUBST(pgdir,$pgdir/)

# 
AC_CONFIG_FILES([
	Makefile
	include/Makefile
	src/Makefile
	src/twopo/Makefile
	src/opte/Makefile
])
AC_OUTPUT

