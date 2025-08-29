CXX = g++
CXXFLAGS = -std=c++11 -Wall -I.
LDFLAGS = -lpthread

SRCS = Task/http_connection.cpp main.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = server

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 模式规则编译 .cpp 文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)