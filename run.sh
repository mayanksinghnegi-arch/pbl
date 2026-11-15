#!/bin/sh
# Build (if needed) and start FoodBridge, then open http://localhost:8080
cd "$(dirname "$0")"
if [ ! -x backend/foodbridge ] || [ backend/main.cpp -nt backend/foodbridge ]; then
  echo "Compiling backend (first build takes ~30 seconds)..."
  g++ -std=c++17 -O2 backend/main.cpp -o backend/foodbridge -lpthread || exit 1
fi
./backend/foodbridge "$@"
