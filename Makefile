CXX = clang++
CXXFLAGS = -std=c++17 -pthread
SRC_DIR = src
BIN_DIR = bin

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR)/server: $(SRC_DIR)/server.cpp $(SRC_DIR)/commands.cpp
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BIN_DIR)/client: $(SRC_DIR)/client.cpp $(SRC_DIR)/commands.cpp
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(BIN_DIR)
