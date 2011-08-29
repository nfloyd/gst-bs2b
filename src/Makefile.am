# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

plugin_LTLIBRARIES = libgstbs2b.la

# sources used to compile this plug-in
libgstbs2b_la_SOURCES = gstbs2b.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstbs2b_la_CFLAGS = $(GST_CFLAGS) $(BS2B_CFLAGS)
libgstbs2b_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(BS2B_LIBS)
libgstbs2b_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbs2b_la_LIBTOOLFLAGS = --tag=disable-static
