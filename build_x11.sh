cmake -S . -B build_x11 -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=x11
cmake --build build_x11 -j 2
#./build_x11/myapp