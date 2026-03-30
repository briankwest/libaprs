lib_LTLIBRARIES = libaprs.la

libaprs_la_SOURCES = \
	aprs.c \
	aprsis.c \
	ax25.c \
	error.c \
	kiss.c \
	modem.c \
	station.c \
	transport.c \
	wav.c

libaprs_la_CFLAGS = \
	-std=c99 -D_POSIX_C_SOURCE=200809L \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include

libaprs_la_LDFLAGS = -version-info 0:0:0
libaprs_la_LIBADD = -lm -lpthread
