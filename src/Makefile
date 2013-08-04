../TS/treesheets: $(patsubst %.cpp,%.o,$(wildcard *.cpp))
	$(CXX) -O3 -o $@ $^ `wx-config-2.9 --libs all`
%.o: %.cpp
	$(CXX) -O3 -c $< `wx-config-2.9 --cxxflags all`
