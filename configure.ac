AC_PREREQ([2.69])
AC_INIT([libaprs], [1.0.2], [https://github.com/briankwest/libaprs/issues])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall dist-xz])

AM_PROG_AR
LT_INIT

AC_PROG_CC
AC_PROG_INSTALL

PKG_CHECK_MODULES([RTLSDR], [librtlsdr], [have_rtlsdr=yes], [have_rtlsdr=no])
AM_CONDITIONAL([HAVE_RTLSDR], [test "x$have_rtlsdr" = "xyes"])

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    include/Makefile
    examples/Makefile
    tests/Makefile
    libaprs.pc
])
AC_OUTPUT
