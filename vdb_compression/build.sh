#!/bin/bash
echo "Building VDB Compressor..."
mkdir -p build
cd build
cmake ..
make -j4
echo "Build complete!"
