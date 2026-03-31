ACLOCAL_AMFLAGS = -I m4

GIT_HASH := $(shell git -C $(srcdir) rev-parse --short HEAD 2>/dev/null || echo "unknown")
export GIT_HASH

SUBDIRS = src include examples tests

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libaprs.pc

EXTRA_DIST = \
	autogen.sh \
	README.md \
	CLAUDE.md \
	LICENSE
