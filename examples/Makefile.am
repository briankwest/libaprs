noinst_PROGRAMS = \
	example_parse_packet \
	example_build_position \
	example_kiss_bridge \
	aprs_decode \
	gen_test_wavs

AM_CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -I$(top_srcdir)/include -DLIBAPRS_GIT_HASH=\"$(GIT_HASH)\"
LDADD = $(top_builddir)/src/libaprs.la -lm

example_parse_packet_SOURCES = parse_packet.c
example_build_position_SOURCES = build_position.c
example_kiss_bridge_SOURCES = kiss_bridge.c
aprs_decode_SOURCES = aprs_decode.c
gen_test_wavs_SOURCES = gen_test_wavs.c

if HAVE_RTLSDR
noinst_PROGRAMS += aprs_sdr
aprs_sdr_SOURCES = aprs_sdr.c
aprs_sdr_CFLAGS = $(AM_CFLAGS) $(RTLSDR_CFLAGS)
aprs_sdr_LDADD = $(LDADD) $(RTLSDR_LIBS)
endif
