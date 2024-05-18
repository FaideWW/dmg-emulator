# inspired by https://stackoverflow.com/questions/30573481/how-to-write-a-makefile-with-separate-source-and-header-directories

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

.PHONY: all clean

EXE = $(BIN_DIR)/emu
SHADER_H = $(INCLUDE_DIR)/generated/metal_shaders.generated.h
SHADER_LIB = $(OBJ_DIR)/shaders.metallib

SRC = $(wildcard $(SRC_DIR)/*.cpp $(SRC_DIR)/*.mm)
SRC += $(INCLUDE_DIR)/stb/stbi_image.cpp
SHADER_SRC = $(wildcard $(SRC_DIR)/shaders/*.metal)

OBJ = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRC)))))
SHADER_OBJ = $(addprefix $(OBJ_DIR)/, $(addsuffix .air, $(basename $(notdir $(SHADER_SRC)))))

$(info SRC is $(SRC))
$(info OBJ is $(OBJ))

CXXFLAGS = -std=c++17 -Iinclude -Iinclude/metal-cpp -I/opt/homebrew/include
CXXFLAGS += -Wall -Wformat

LDFLAGS = -framework Foundation -framework Metal -framework QuartzCore
LDFLAGS += -L/opt/homebrew/lib -Llib -lglfw

$(info LDFLAGS is $(LDFLAGS))
$(info CLFAGS is $(CFLAGS))
$(info SHADER_LIB is $(SHADER_LIB))
$(info SHADER_OBJ is $(SHADER_OBJ))
$(info SHADER_SRC is $(SHADER_SRC))

all: shaders $(EXE) 

shaders: $(SHADER_H)

$(SHADER_H): $(SHADER_LIB)
	xxd -i $< > $@

$(SHADER_LIB): $(SHADER_OBJ)
	xcrun -sdk macosx metallib -o $@ $^

$(OBJ_DIR)/%.air: $(SRC_DIR)/shaders/%.metal | $(OBJ_DIR)
	xcrun -sdk macosx metal -c $< -o $@

$(EXE): $(OBJ) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.mm | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CFLAGS) -ObjC++ -fobjc-weak -fobjc-arc -c $< -o $@

$(OBJ_DIR)/%.o: $(INCLUDE_DIR)/stb/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CFLAGS) -c $< -o $@


$(BIN_DIR) $(OBJ_DIR): 
	mkdir -p $@

clean:
	@$(RM) -rv $(BIN_DIR) $(OBJ_DIR)

-include $(OBJ:.o=.d)
