include Makefile.gtester

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = shm gvdb common engine service gdbus gsettings client bin docs tests

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc
EXTRA_DIST = trim-lcov.py m4

clean-am: lcov-clean gcno-clean

.PHONY: lcov lcov-clean gcno-clean

lcov: lcov-clean
	$(MAKE) $(AM_MAKEFLAGS) check
	lcov --directory $(top_builddir) --capture --test-name dconf | $(top_srcdir)/trim-lcov.py > dconf-lcov.info
	LANG=C genhtml --prefix $(top_builddir) --output-directory lcov-html --legend --show-details dconf-lcov.info
	@echo
	@echo "     file://$(abs_top_builddir)/lcov-html/index.html"
	@echo

gcno-clean:
	-find . -name '*.gcno' -delete

lcov-clean:
	-lcov --directory $(top_builddir) -z
	find . -name '*.gcda' -delete
	rm -rf lcov-html dconf-lcov.info
