man_MANS = lsscsi.8
SUBDIRS =
EXTRA_DIST=autogen.sh

# added for Debian
prefix=/usr
mandir=${prefix}/share/man


bin_PROGRAMS = lsscsi

# INCLUDES = -I$(top_srcdir)/include

# AM_CFLAGS = -Wall -W
AM_CFLAGS = -Wall -W -pedantic -std=c99

lsscsi_SOURCES = lsscsi.c

