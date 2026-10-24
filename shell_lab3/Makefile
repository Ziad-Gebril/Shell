CXX = g++
CXXFLAGS = -Wall -g

myshell: command.cc tokenizer.cc command.h tokenizer.h
	$(CXX) $(CXXFLAGS) command.cc tokenizer.cc -o myshell

clean:
	rm -f myshell
