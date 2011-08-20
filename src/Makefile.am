plugin_LTLIBRARIES = libgstbs2b.la

# sources used to compile this plug-in
libgstbs2b_la_SOURCES = gstbs2b.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstbs2b_la_CFLAGS = $(GST_CFLAGS) $(BS2B_CFLAGS)
libgstbs2b_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(BS2B_LIBS)
libgstbs2b_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbs2b_la_LIBTOOLFLAGS = --tag=disable-static
