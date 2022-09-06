SUBDIRS = DMR2NXDN DMR2YSF NXDN2DMR YSF2DMR YSF2NXDN YSF2P25
CLEANDIRS = $(SUBDIRS:%=clean-%)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(CLEANDIRS)

$(CLEANDIRS): 
	$(MAKE) -C $(@:clean-%=%) clean

.PHONY: $(SUBDIRS) $(CLEANDIRS)

install-restart:
	systemctl ysf2dmr stop
	cp YSF2DMR /usr/local/sbin
	systemctl ysf2dmr start