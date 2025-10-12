rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/lyq/repo/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=arm64-linux

cmake --build .

ctest -V