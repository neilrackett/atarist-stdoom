export STCMD_NO_TTY=1

.PHONY: all
all: stdoom sidecart

.PHONY: stdoom $(MAKECMDGOALS)
stdoom:
	stcmd make -C linuxdoom-1.10 $(filter-out $@,$(MAKECMDGOALS))

.PHONY: sidecart $(MAKECMDGOALS)
sidecart:
	make -C sidecart $(filter-out $@,$(MAKECMDGOALS))
