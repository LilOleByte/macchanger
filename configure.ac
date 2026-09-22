AC_INIT([macchanger], [1.8.0])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_INSTALL

AC_CHECK_HEADERS([sys/random.h])
AC_CHECK_FUNCS([getrandom])

AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
doc/Makefile])
AC_OUTPUT
