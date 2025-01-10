cd src
rm -rf build
mkdir build && cd build
# If you want to use Astraea's inference service, add -DCOMPILE_INFERENCE_SERVICE=ON
CXX=/usr/bin/g++-9 cmake ..
make -j