SUBDIRS = sdp twopo @OPTE_SUBDIR@ @DEBUGGRAPH_SUBDIR@
INCLUDES = -I$(top_srcdir)/include
lib_LTLIBRARIES = libljqo.la
libljqo_la_SOURCES = ljqo.c
libljqo_la_LIBADD = sdp/libsdp.la twopo/libtwopo.la @OPTE_OBJ@ @DEBUGGRAPH_OBJ@ @LIBOBJS@
libljqo_la_LDFLAGS = -version-info 0:0:0 -module
