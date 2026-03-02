SUBDIR := linuxdoom-1.10

.PHONY: all

all:
	$(MAKE) -C $(SUBDIR)

%:
	$(MAKE) -C $(SUBDIR) $@
