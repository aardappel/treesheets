
# Use `sudo update-alternatives --config c++` to configure this
c++ -O3 -o treesheets *.cpp `wx-config --cxxflags --libs all`
mv treesheets ../TS
