SUBDIRS = libinfinity libinftext

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libinfinity-1.0.pc libinftext-1.0.pc

if WITH_INFGTK
SUBDIRS += libinfgtk
pkgconfig_DATA += libinfgtk-1.0.pc
endif

if WITH_INFTEXTGTK
SUBDIRS += libinftextgtk
pkgconfig_DATA += libinftextgtk-1.0.pc
endif

if WITH_INFINOTED
SUBDIRS += infinoted
endif

SUBDIRS += pixmaps test docs po

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc
