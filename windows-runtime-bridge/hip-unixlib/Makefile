# Build: x86_64 Wine unixlib module for amdhip64_7.dll.
# Confirmed compatible with Proton 11.0 - see docs/linux-support-spec.md
# for the full validation this Makefile's flags are derived from
# (adapted from brainrom/winedll-example for Ubuntu's actual layout).

WINELIB = /usr/lib/x86_64-linux-gnu/wine
WINEINC = /usr/include/wine/wine

WINE_INCLUDES = -I$(WINEINC)/windows -I$(WINEINC)/msvcrt

LIBNAME = amdhip64_7
UNIX_DIR = x86_64-unix
WIN_DIR = x86_64-windows

UNIX_CC = clang
UNIX_CFLAGS = \
    -m64 -O2 -D__WINESRC__ -DWINE_UNIX_LIB -D_WIN64 -pipe \
    -fcf-protection=none -fvisibility=hidden -fno-stack-protector \
    -fno-strict-aliasing -fPIC -fasynchronous-unwind-tables \
    -I. -I$(WINEINC)/windows
UNIX_LDFLAGS = \
    -shared -Wl,-Bsymbolic -Wl,-soname,$(LIBNAME).so -Wl,-z,defs \
    -L$(WINELIB)/x86_64-unix -l:ntdll.so -lpthread -ldl

WIN_CC = clang
WIN_CFLAGS = \
    -O2 -D__STDC__ -D__WINE_PE_BUILD -D_UCRT -D__WINESRC__ \
    -target x86_64-windows -fuse-ld=lld --no-default-config \
    -fno-strict-aliasing -mcx16 -mcmodel=small \
    -I. $(WINE_INCLUDES)
WIN_LDFLAGS = \
    -b x86_64-windows -Wl,--wine-builtin -shared --no-default-config \
    -L$(WINELIB)/x86_64-windows -lwinecrt0 -lucrtbase -lkernel32 -lntdll

all: $(UNIX_DIR)/$(LIBNAME).so $(WIN_DIR)/$(LIBNAME).dll

$(UNIX_DIR) $(WIN_DIR):
	mkdir -p $@

$(UNIX_DIR)/native.o: native.c unixlib.h registry.h version_query.h ext_mem.h hip_forward.h alloc_cap.h | $(UNIX_DIR)
	$(UNIX_CC) -c -o $@ $< $(UNIX_CFLAGS)

$(UNIX_DIR)/registry.o: registry.c registry.h | $(UNIX_DIR)
	$(UNIX_CC) -c -o $@ $< $(UNIX_CFLAGS)

$(UNIX_DIR)/version_query.o: version_query.c version_query.h | $(UNIX_DIR)
	$(UNIX_CC) -c -o $@ $< $(UNIX_CFLAGS)

$(UNIX_DIR)/ext_mem.o: ext_mem.c ext_mem.h | $(UNIX_DIR)
	$(UNIX_CC) -c -o $@ $< $(UNIX_CFLAGS)

$(UNIX_DIR)/hip_forward.o: hip_forward.c hip_forward.h | $(UNIX_DIR)
	$(UNIX_CC) -c -o $@ $< $(UNIX_CFLAGS)

$(UNIX_DIR)/alloc_cap.o: alloc_cap.c alloc_cap.h | $(UNIX_DIR)
	$(UNIX_CC) -c -o $@ $< $(UNIX_CFLAGS)

$(UNIX_DIR)/$(LIBNAME).so: $(UNIX_DIR)/native.o $(UNIX_DIR)/registry.o $(UNIX_DIR)/version_query.o $(UNIX_DIR)/ext_mem.o $(UNIX_DIR)/hip_forward.o $(UNIX_DIR)/alloc_cap.o
	$(UNIX_CC) -o $@ $^ $(UNIX_LDFLAGS)

test_registry: registry.c registry.h test_registry.c
	gcc -Wall -o test_registry registry.c test_registry.c
	./test_registry

test_version_query: version_query.c version_query.h test_version_query.c
	gcc -Wall -o test_version_query version_query.c test_version_query.c
	./test_version_query

test_ext_mem: ext_mem.c ext_mem.h test_ext_mem.c
	gcc -Wall -o test_ext_mem ext_mem.c test_ext_mem.c
	./test_ext_mem

test_hip_forward: hip_forward.c hip_forward.h test_hip_forward.c
	gcc -Wall -o test_hip_forward hip_forward.c test_hip_forward.c
	./test_hip_forward

test_memcpy_kind: memcpy_kind.c memcpy_kind.h test_memcpy_kind.c
	gcc -Wall -o test_memcpy_kind memcpy_kind.c test_memcpy_kind.c
	./test_memcpy_kind

test_alloc_cap: alloc_cap.c alloc_cap.h test_alloc_cap.c
	gcc -Wall -o test_alloc_cap alloc_cap.c test_alloc_cap.c
	./test_alloc_cap

test_last_error: last_error.c last_error.h test_last_error.c
	gcc -Wall -o test_last_error last_error.c test_last_error.c
	./test_last_error

test: test_registry test_version_query test_ext_mem test_hip_forward test_memcpy_kind test_alloc_cap test_last_error

$(WIN_DIR)/pe_shim.o: pe_shim.c unixlib.h ext_mem.h memcpy_kind.h last_error.h | $(WIN_DIR)
	$(WIN_CC) -c -o $@ $< $(WIN_CFLAGS)

$(WIN_DIR)/ext_mem.o: ext_mem.c ext_mem.h | $(WIN_DIR)
	$(WIN_CC) -c -o $@ $< $(WIN_CFLAGS)

$(WIN_DIR)/memcpy_kind.o: memcpy_kind.c memcpy_kind.h | $(WIN_DIR)
	$(WIN_CC) -c -o $@ $< $(WIN_CFLAGS)

$(WIN_DIR)/last_error.o: last_error.c last_error.h | $(WIN_DIR)
	$(WIN_CC) -c -o $@ $< $(WIN_CFLAGS)

$(WIN_DIR)/$(LIBNAME).dll: $(LIBNAME).spec $(WIN_DIR)/pe_shim.o $(WIN_DIR)/ext_mem.o $(WIN_DIR)/memcpy_kind.o $(WIN_DIR)/last_error.o
	winegcc -o $@ $^ $(WIN_LDFLAGS)

$(WIN_DIR)/interop_roundtrip_test.o: interop_roundtrip_test.c | $(WIN_DIR)
	$(WIN_CC) -c -o $@ $< $(WIN_CFLAGS)

interop_roundtrip_test.exe: $(WIN_DIR)/interop_roundtrip_test.o
	winegcc -o $@ $^ -b x86_64-windows --no-default-config -L$(WINELIB)/x86_64-windows -ld3d12

clean:
	rm -rf $(UNIX_DIR) $(WIN_DIR) test_registry test_version_query test_ext_mem test_hip_forward test_memcpy_kind test_alloc_cap test_last_error interop_roundtrip_test.exe

.PHONY: all clean test
