#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([prebsd], [0.1], [david@clearbrookdistillery.com])
AC_CONFIG_SRCDIR([fetch])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_LN_S

# prebsd is a set of scripts (fetch, boot.py); nothing is compiled.  These
# are runtime dependencies, reported as hints only.
AC_PATH_PROG([PYTHON3], [python3], [no])
AC_PATH_PROG([CURL], [curl], [no])
AS_IF([test "x$PYTHON3" = "xno"], [AC_MSG_WARN([python3 not found; boot.py needs it at runtime])])
AS_IF([test "x$CURL" = "xno"], [AC_MSG_WARN([curl not found; fetch needs it at runtime])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
