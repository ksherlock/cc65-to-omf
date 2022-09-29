CXXFLAGS = -std=c++17 -g

.PHONY: all clean clobber

all: cc65-to-omf

clean:
	$(RM) *.o
clobber:
	$(RM) *.o
	$(RM) cc65-to-omf

cc65-to-omf: main.o expression.o fileio.o finder_info.o
	$(CXX) -o $@ $^
