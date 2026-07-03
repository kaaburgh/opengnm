include config.mak

# opengnm source files
OPENGNM_SRCS = \
	src/drawcommandbuffer.c \
	src/rendertarget.c \
	src/depthrendertarget.c \
	src/texture.c \
	src/shader.c \
	src/dataformat.c \
	src/commandbuffer.c \
	src/error.c

ifeq ($(PLATFORM), orbis)
OPENGNM_SRCS += \
	src/driver_orbis.c \
	src/platform_orbis.c
else
OPENGNM_SRCS += \
	src/driver_generic.c \
	src/platform_generic.c
endif

# GpuAddr (AMD PAL-derived surface computation)
OPENGNM_SRCS += \
	src/gpuaddr/surface.c \
	src/gpuaddr/tilemodes.c \
	src/gpuaddr/tiler.c \
	src/gpuaddr/decompress.c \
	src/gpuaddr/surfgen.c \
	src/gpuaddr/error.c

# GCN assembler (internal)
OPENGNM_SRCS += \
	src/gcn/analyzer.c \
	src/gcn/assembler.c \
	src/gcn/decoder.c \
	src/gcn/error.c \
	src/gcn/format.c \
	src/gcn/types.c

# PM4 encoding
OPENGNM_SRCS += \
	src/pm4/format.c \
	src/pm4/decoder.c \
	src/pm4/error.c \
	src/pm4/types.c

# Filter out comment-only lines (lines starting with # after whitespace)
OPENGNM_SRCS := $(filter-out #%,$(OPENGNM_SRCS))
OPENGNM_SRCS := $(strip $(OPENGNM_SRCS))

# === Library ===
LIB_NAME = libopengnm
LIB_STATIC = $(LIB_NAME).a
LIB_SHARED = $(LIB_NAME)$(LIBEXT).$(LIBVER)

.PHONY: all lib tools tests clean install

all: lib

lib:
ifneq ($(OPENGNM_SRCS),)
	@for src in $(OPENGNM_SRCS); do \
		obj=$$(echo $$src | sed 's/\.c$$/.o/') ; \
		dir=$$(dirname $$obj) ; \
		mkdir -p $$dir ; \
		echo "CC  $$src" ; \
		$(CC) $(CFLAGS) -c $$src -o $$obj ; \
	done
	$(AR) rcs $(LIB_STATIC) $$(find . -name '*.o' -path '*/src/*')
else
	@echo "opengnm: No source files configured yet (Phase 1 — headers only)"
	@echo "opengnm: Headers are in include/ — use 'make install' to install them"
endif

# === Install ===
install: install-headers

install-headers:
	install -d $(DESTDIR)$(INCDIR)/opengnm/pm4
	install -m 644 include/*.h $(DESTDIR)$(INCDIR)/opengnm/
	install -m 644 include/pm4/*.h $(DESTDIR)$(INCDIR)/opengnm/pm4/

install-lib: lib
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/

# === Tests ===
tests:
	@echo "opengnm: Tests will be added in Phase 7"

# === Clean ===
clean:
	find . -name '*.o' -path '*/src/*' -delete
	rm -f $(LIB_STATIC) $(LIB_SHARED)
