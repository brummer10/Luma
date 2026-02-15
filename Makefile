
CXX      := g++
TARGET   := luma
SRC      := main.cpp
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)

PKG_CFLAGS := $(shell pkg-config --cflags jack lilv-0 x11)
PKG_LIBS   := $(shell pkg-config --libs jack lilv-0 x11)

CXXFLAGS := -std=c++17 -Wall -Wextra -O2
LDFLAGS  := -ldl

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $^ -o $@ $(PKG_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0
debug: clean all

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)

.PHONY: all clean debug
