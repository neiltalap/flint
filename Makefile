CC ?= cc
CXX ?= c++

# Flint uses a C23 subset (nullptr,
# constexpr objects, attributes) that
# older compilers reject. Fail fast
# with a clear message instead of
# pages of syntax errors. 
    'static constexpr unsigned N = 8;' \
    '[[nodiscard]] static int f(void *p)' \
    '{ return p == nullptr; }' \
    'int arr[N];' \
    'int main(void) { return f(nullptr); }'

C23_STD := $(shell $(C23_SNIPPET) | \
    $(CC) -std=c23 -pedantic -x c -c - \
    -o /dev/null 2>/dev/null && echo c23)
ifeq ($(C23_STD),)
C23_STD := $(shell $(C23_SNIPPET) | \
    $(CC) -std=c2x -pedantic -x c -c - \
    -o /dev/null 2>/dev/null && echo c2x)
endif
ifeq ($(C23_STD),)
$(error C compiler rejects Flint's C23 subset (nullptr, constexpr objects, attributes); use a newer GCC or Clang)
endif

CFLAGS += -std=$(C23_STD) -pedantic -Wall -Wextra -Werror
CFLAGS += -O2 -Wunused-result
CFLAGS += -Igenerated -Iinclude

# C++ sources build as C++23. -Iinclude
# lets src/ui.cpp find flint/ui.h, -I.
# covers the include/flint/ui.h spelling.
CXXFLAGS += -std=c++23 -pedantic -Wall -Wextra
CXXFLAGS += -Werror -O2
CXXFLAGS += -Igenerated -Iinclude -I.

# Use pkg-config for Wayland/Vulkan flags if available
PKG_CFLAGS := $(shell pkg-config --cflags wayland-client vulkan 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs wayland-client vulkan 2>/dev/null)

# Wayland protocol discovery. Prefer
# pkg-config so distro layout changes
# do not break the build; fall back
# to the historical prefix.
WAYLAND_PROTOCOLS := $(shell pkg-config \
    --variable=pkgdatadir \
    wayland-protocols 2>/dev/null)
ifeq ($(WAYLAND_PROTOCOLS),)
WAYLAND_PROTOCOLS := /usr/share/wayland-protocols
endif
WAYLAND_SCANNER := wayland-scanner

XDG_XML := $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml
ifeq ($(wildcard $(XDG_XML)),)
$(error xdg-shell.xml not found under $(WAYLAND_PROTOCOLS); install the wayland-protocols package)
endif
XDG_HEADER := generated/xdg-shell-client-protocol.h
XDG_SOURCE := generated/xdg-shell-protocol.c

# Explicit C sources. The generated
# protocol source builds as C.
C_SRCS := main.c src/vk.c src/wl.c
C_SRCS += src/flint.c $(XDG_SOURCE)

# Explicit C++ sources. $(wildcard) keeps
# make working before src/ui.cpp lands;
# it expands empty until the file exists.
CXX_SRCS := $(wildcard src/ui.cpp)

C_OBJS := $(C_SRCS:.c=.o)
CXX_OBJS := $(CXX_SRCS:.cpp=.o)
OBJS := $(C_OBJS) $(CXX_OBJS)

all: bin/flint

generated:
	mkdir -p $@

$(XDG_HEADER): $(XDG_XML) | generated
	$(WAYLAND_SCANNER) client-header $< $@

$(XDG_SOURCE): $(XDG_XML) | generated
	$(WAYLAND_SCANNER) private-code $< $@

bin:
	mkdir -p $@

# C objects.
%.o: %.c $(XDG_HEADER)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

# C++ objects.
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c $< -o $@

# Link with $(CXX) so C++ objects resolve
# libstdc++ without extra flags.
bin/flint: $(OBJS) | bin
	$(CXX) -o $@ $(OBJS) $(PKG_LIBS)

check: bin/flint
	./bin/flint --frames 120

clean:
	rm -f main.o src/*.o
	rm -rf bin dist generated

.PHONY: all check clean
