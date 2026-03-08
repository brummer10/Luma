
LIB_DIR   := ../libxputty/libxputty/
HEADER_DIR := $(LIB_DIR)include/
RESOURCES  := $(wildcard $(RESOURCES_DIR)*.png)

CXX      := g++
TARGET   := ../luma
INCL     := -I. -I../LV2Host/BackEnds -I../LV2Host/Engines -I../LV2Host/InterFaces \
            -I../LV2Host/Host -I../LV2Host -I$(HEADER_DIR)

# Default packages (GUI build)
PKGS := jack lilv-0

GTK2CXXFLAGS :=
EXTRA_DEFS   :=

# Detect NOGUI target
ifneq (,$(findstring nogui,$(MAKECMDGOALS)))
    $(info $(blue)Building in NO-GUI mode $(reset))
    SRC      := ./NoGui/main.cpp
    EXTRA_DEFS += -DNOGUI
else
	SRC      := main.cpp
	PKGS += cairo x11
	XPUTTY := -L. $(LIB_DIR)libxputty.a
    # GTK2 auto-detect (only if not clean and not nogui)
    ifeq (,$(findstring clean,$(MAKECMDGOALS)))
        GTK2_FOUND := $(shell pkg-config --exists gtk+-2.0 && echo 1)
        ifeq ($(GTK2_FOUND),1)
            $(info $(blue)GTK2 found — enabling GTK support $(reset))
            PKGS += gtk+-2.0
            GTK2CXXFLAGS += -DHAVE_GTK2 -Wno-deprecated-declarations
        else
            $(info $(red)GTK2 not found — building X11 only $(reset))
        endif
    endif
endif

OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs   $(PKGS))

CXXFLAGS += -std=c++17 -Wall -Wextra -O2 $(EXTRA_DEFS)
LDFLAGS  := -ldl -lm

all: $(TARGET)

nogui: clean all

$(TARGET): $(OBJ)
	$(CXX) $^ $(XPUTTY) -o $@ $(PKG_LIBS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(GTK2CXXFLAGS) $(INCL) $(PKG_CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0 -DDEBUG
debug: clean all

clean:
	rm -f $(TARGET) $(OBJ) $(DEP) ./NoGui/main.d ./NoGui/main.o 

.PHONY: all clean debug nogui
