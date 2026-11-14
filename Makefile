CXX ?= c++
CPPFLAGS := -Iinclude -Ithird_party -DBUILDQUEUE_ROOT='"$(CURDIR)"'
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -pthread
LDFLAGS ?= -pthread
BUILD ?= .build
APP ?= buildqueue
CORE := $(filter-out src/main.cpp src/CLI.cpp src/HttpServer.cpp,$(wildcard src/*.cpp))
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SOURCES))
TEST_OBJECTS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE)) $(patsubst tests/%.cpp,$(BUILD)/test_%.o,$(wildcard tests/*.cpp))
.PHONY: all test clean debug asan tsan
all: $(APP)
$(APP): $(OBJECTS)
	$(CXX) $^ $(LDFLAGS) -o $@
$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/test_%.o: tests/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/buildqueue_tests: $(TEST_OBJECTS)
	$(CXX) $^ $(LDFLAGS) -o $@
test: $(BUILD)/buildqueue_tests
	./$(BUILD)/buildqueue_tests
debug:
	$(MAKE) BUILD=.build-debug APP=.build-debug/buildqueue CXXFLAGS='-std=c++17 -O0 -g3 -Wall -Wextra -Wpedantic -pthread' all test
asan:
	$(MAKE) BUILD=.build-asan CXXFLAGS='-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -pthread -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-pthread -fsanitize=address,undefined' test
tsan:
	$(MAKE) BUILD=.build-tsan CXXFLAGS='-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -pthread -fsanitize=thread' LDFLAGS='-pthread -fsanitize=thread' test
clean:
	$(RM) -r .build .build-debug .build-asan .build-tsan buildqueue
-include $(wildcard $(BUILD)/*.d)
