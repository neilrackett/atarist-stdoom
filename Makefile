export STCMD_NO_TTY=1

# .PHONY: all
# all: stdoom sidecart

.PHONY: stdoom $(MAKECMDGOALS)
stdoom:
	stcmd make -C linuxdoom-1.10 $(filter-out $@,$(MAKECMDGOALS))

.PHONY: sidecart $(MAKECMDGOALS)
sidecart:
	make -C sidecart $(filter-out $@,$(MAKECMDGOALS))

.PHONY: sctest
sctest:
	stcmd m68k-atari-mint-gcc -O2 \
		-I sidecart/test -I linuxdoom-1.10 \
		-o sidecart/test/SCTEST.TOS \
		sidecart/test/sctest.c linuxdoom-1.10/sidecart_md.c linuxdoom-1.10/sidecart_stubs.S
