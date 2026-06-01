SUBDIR :=

# .PHONY: all
# all: stdoom sidecart

.PHONY: stdoom
stdoom:
	stcmd make -C linuxdoom-1.10

.PHONY: sidecart
sidecart:
	make -C sidecart
