#!/usr/bin/env bash
# Use `sudo update-alternatives --config c++` to configure this
c++ -std=c++11 -O3 -o treesheets *.cpp `wx-config --cxxflags --libs all`
mv treesheets ../TS
