#!/bin/sh
if [ -f Makefile ]; then
  echo "Making make distclean..."
  make distclean
fi
echo "Removing autogenned files..."
rm -f depcomp install-sh libtool ltmain.sh missing INSTALL stamp-h1 aclocal.m4
rm -f config.guess config.h config.h.in config.log config.status config.sub
rm -f configure
rm -f Makefile Makefile.in 
rm -f src/Makefile src/Makefile.in
rm -rf src/.deps
rm -rf autom4te.cache
echo "Done."
