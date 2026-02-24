
CXX      := g++
TARGET   := luma
SRC      := main.cpp
INCL     := -I. -I./BackEnds -I./Engines -I./LV2Host
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)

# Default packages (GUI build)
PKGS := jack lilv-0 x11

GTK2CXXFLAGS :=
EXTRA_DEFS   :=

# Detect NOGUI target
ifneq (,$(findstring nogui,$(MAKECMDGOALS)))
    $(info Building in NO-GUI mode)
    PKGS := jack lilv-0
    EXTRA_DEFS += -DNOGUI
else
    # GTK2 auto-detect (only if not clean and not nogui)
    ifeq (,$(findstring clean,$(MAKECMDGOALS)))
        GTK2_FOUND := $(shell pkg-config --exists gtk+-2.0 && echo 1)
        ifeq ($(GTK2_FOUND),1)
            $(info GTK2 found — enabling GTK support)
            PKGS += gtk+-2.0
            GTK2CXXFLAGS += -DHAVE_GTK2 -Wno-deprecated-declarations
        else
            $(info GTK2 not found — building X11 only)
        endif
    endif
endif

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs   $(PKGS))

CXXFLAGS += -std=c++17 -Wall -Wextra -O2 $(EXTRA_DEFS)
LDFLAGS  := -ldl

all: $(TARGET)

nogui: clean all

$(TARGET): $(OBJ)
	$(CXX) $^ -o $@ $(PKG_LIBS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(GTK2CXXFLAGS) $(INCL) $(PKG_CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0 -DDEBUG
debug: clean all

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)

.PHONY: all clean debug nogui
