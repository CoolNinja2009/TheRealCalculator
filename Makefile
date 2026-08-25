# Makefile for Natural Calculator (native Win32/GDI, no external deps).
#
# Build on Windows with MinGW-w64:
#   mingw32-make
#
# Cross-compile from Linux/macOS with MinGW-w64:
#   make CXX=x86_64-w64-mingw32-g++ WINDRES=x86_64-w64-mingw32-windres
#
# Produces build/NaturalCalculator.exe -- a single, dependency-free,
# statically-linked executable (no MSVCRT/libgcc/libstdc++ DLLs required
# at runtime) sized for near-instant startup.

CXX      ?= g++
WINDRES  ?= windres
SRC_DIR  := src
BUILD_DIR:= build
TARGET   := $(BUILD_DIR)/NaturalCalculator.exe

SOURCES  := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS  := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode -mwindows -DUNICODE -D_UNICODE
LDFLAGS  := -static -static-libgcc -static-libstdc++ -municode -mwindows -s
LIBS     := -lgdi32 -luser32 -lkernel32 -lcomctl32 -lcomdlg32

.PHONY: all clean run

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LIBS)
	@echo "Built $(TARGET)"

clean:
	rm -rf $(BUILD_DIR)
