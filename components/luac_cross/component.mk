COMPONENT_OWNBUILDTARGET:=build
COMPONENT_ADD_LDFLAGS:=

build:
	$(MAKE) -f Makefile HOSTCC=$(HOSTCC) BUILD_DIR_BASE=$(BUILD_DIR_BASE) V=$V