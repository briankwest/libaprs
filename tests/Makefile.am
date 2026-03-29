check_PROGRAMS = test_libaprs

test_libaprs_SOURCES = \
	test_main.c \
	test_aprs.c \
	test_kiss.c \
	test_ax25.c \
	test_transport.c \
	test_aprsis.c \
	test_station.c \
	test_modem.c

test_libaprs_CFLAGS = \
	-std=c99 -D_POSIX_C_SOURCE=200809L \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include

test_libaprs_LDADD = $(top_builddir)/src/libaprs.la -lm

TESTS = test_libaprs
