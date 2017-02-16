include $(top_srcdir)/Makefile.gtester

libexec_PROGRAMS = dconf-proxy

dbusservice_DATA = ca.desrt.dconf.Proxy.service

dconf_proxy_CFLAGS = \
	-I$(top_srcdir)			\
	$(gio_CFLAGS)
dconf_proxy_LDADD = \
	../client/libdconf.so		\
	$(gio_LIBS)

dconf_proxy_SOURCES = \
	../gvdb/gvdb-builder.c		\
	confinement.c			\
	confinement-flatpak.c		\
	confinement.h			\
	permissions.c			\
	permissions.h			\
	proxy.c

DISTCLEANFILES = ca.desrt.dconf.service

ca.desrt.dconf.Proxy.service: Makefile
	$(AM_V_GEN) (echo '[D-BUS Service]'; \
	             echo 'Name=ca.desrt.dconf.Proxy'; \
	             echo 'Exec=${libexecdir}/dconf-proxy') > $@.tmp && \
	            mv $@.tmp $@
