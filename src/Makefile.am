SUBDIRS = twopo @OPTE_SUBDIR@
INCLUDES = -I$(top_srcdir)/include
lib_LTLIBRARIES = libljqo.la
libljqo_la_SOURCES = ljqo.c
libljqo_la_LIBADD = twopo/libtwopo.la @OPTE_OBJ@ @LIBOBJS@
libljqo_la_LDFLAGS = -version-info 0:0:0 -module
