# plugindir is set in configure

##############################################################################
# change libgstplugin.la to something more suitable, e.g. libmysomething.la  #
##############################################################################
plugin_LTLIBRARIES = libgstbs2b.la

##############################################################################
# for the next set of variables, rename the prefix if you renamed the .la,   #
#  e.g. libgstplugin_la_SOURCES => libmysomething_la_SOURCES                 #
#       libgstplugin_la_CFLAGS  => libmysomething_la_CFLAGS                  #
#       libgstplugin_la_LIBADD  => libmysomething_la_LIBADD                  #
#       libgstplugin_la_LDFLAGS => libmysomething_la_LDFLAGS                 #
##############################################################################

# sources used to compile this plug-in
libgstbs2b_la_SOURCES = gstbs2b.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstbs2b_la_CFLAGS = $(GST_CFLAGS)
libgstbs2b_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GSTCTRL_LIBS) -lgstaudio-$(GST_MAJORMINOR)
libgstbs2b_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lbs2b
libgstbs2b_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = 
