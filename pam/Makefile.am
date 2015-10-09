pamlibdir = $(PAM_DEST_DIR)
pamlib_PROGRAMS = pam_dconf.so

noinst_LIBRARIES = pam_dconf.a

pam_dconf_so_SOURCES = \
	pam_dconf.c \
	pam_dconf.h
pam_dconf_so_CFLAGS = -fPIC -DPIC -shared -Wall

pam_dconf_a_CFLAGS = -Wall
pam_dconf_a_SOURCES = $(pam_dconf_so_SOURCES)
