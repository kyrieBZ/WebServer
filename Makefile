# Makefile
CXX = g++
CXXFLAGS = -Wall -g -std=c++11
LIBS = -lmysqlclient -lpthread
INCLUDES = -I./DataBaseModule -I./Thread

SRCS = main.cpp Task/http_connection.cpp DataBaseModule/mysql_connection.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = server

# 注意：LIBS 必须在链接命令的最后
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f Task/*.o
	rm -f DataBaseModule/*.o

.PHONY: clean