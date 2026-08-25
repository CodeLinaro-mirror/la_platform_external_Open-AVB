descend = \
	+mkdir -p $(OUTPUT)$(1) && \
	$(MAKE) $(COMMAND_O) subdir=$(if $(subdir),$(subdir)/$(1),$(1)) $(PRINT_DIR) -C $(1) $(2)

help:
	@echo 'Possible targets:'
	@echo '  gptp              - gptp daemon for linux'
	@echo ''
	@echo '  libgptp      - build libgptp'
	@echo '  libgptp_test     - libgptp_test application'
	@echo ''
	@echo 'Cleaning targets:'
	@echo ''
	@echo '  all of the above with the "_clean" string appended cleans'
	@echo '    the respective build directory.'
	@echo '  clean: a summary clean target to clean _all_ folders'
	@echo ''

gptp:
	$(call descend,daemons/$@/)

gptp_clean:
	$(call descend,daemons/gptp/,clean)

gptp_install: FORCE
ifeq ($(ENABLE_GPTP),1)
	mkdir -p $(DESTDIR)$(bindir)
	install -m 0755 daemons/gptp/obj/qgptp $(DESTDIR)$(bindir)
endif
ifeq ($(ENABLE_GPTP_SERVICE),1)
	mkdir -p $(DESTDIR)$(sysconfdir)
	mkdir -p $(DESTDIR)$(systemd_unitdir)
ifeq ($(TSC_FEATURE),1)
	install -m 0644 daemons/gptp/etc/gptp_cfg_tsc.ini $(DESTDIR)$(sysconfdir)/gptp_cfg.ini
else
	install -m 0644 daemons/gptp/etc/gptp_cfg.ini $(DESTDIR)$(sysconfdir)
endif
	install -DpZm 0644 gptp.service $(DESTDIR)$(systemd_unitdir)/system/gptp.service
	install -DpZm 0644 sleep-notify@gptp.service.d/gptp.conf $(DESTDIR)$(systemd_unitdir)/system/sleep-notify@gptp.service.d/gptp.conf
endif

libgptp:
	$(call descend,lib/libgptp)

libgptp_clean:
	$(call descend,lib/libgptp/,clean)

libgptp_install: FORCE
ifeq ($(ENABLE_LIBGPTP),1)
	mkdir -p $(DESTDIR)$(libdir)
	mkdir -p $(DESTDIR)$(includedir)
	install -m 0755 lib/libgptp/*.so $(DESTDIR)$(libdir)
	install -m 0644 lib/libgptp/gptp_helper.h ${DESTDIR}${includedir}
endif

libgptp_test:
	$(call descend,examples/$@)

libgptp_test_clean:
	$(call descend,examples/libgptp_test/,clean)

libgptp_test_install: FORCE
ifeq ($(ENABLE_LIBGPTP_TEST),1)
	mkdir -p $(DESTDIR)$(bindir)
	install -m 0755 examples/libgptp_test/libgptp_test $(DESTDIR)$(bindir)
endif

all: gptp libgptp libgptp_test

install: gptp_install libgptp_install libgptp_test_install

clean: gptp_clean libgptp_clean libgptp_test_clean

.PHONY: FORCE
