
#!/bin/bash

# Validate Input
if [ "$#" -ne 2 ]; then
    echo "Usage: ./run.sh <power> <num_ops>"
    echo "Example: ./run.sh 10 100  (Database Size 2^10, 100 operations)"
    exit 1
fi

POWER=$1
NUM_OPS=$2

# -------------------------------
# 1. Compile the code
# -------------------------------
echo "================================================="
echo " Compiling....."
echo "================================================="

# Compile with C++20, OpenMP, Pthreads, and optimizations
g++ -std=c++20 -O3 -march=native -fopenmp -pthread -Wno-ignored-attributes main.cpp -o new_test

# Check if compilation was successful
if [ $? -ne 0 ]; then
    echo "[Script] Compilation failed! Please check the errors above."
    exit 1
fi

# -------------------------------
# 2. Show parameters
# -------------------------------
echo "================================================="
echo " Starting Test"
echo " DB Size: 2^$POWER"
echo " Operations: $NUM_OPS"
echo "================================================="

# 1. Start Servers (P1, P2, P3, P4)
./new_test p1 $POWER $NUM_OPS > /dev/null 2>&1 &
PID1=$!

./new_test p2 $POWER $NUM_OPS > /dev/null 2>&1 &
PID2=$!

./new_test p3 $POWER $NUM_OPS > /dev/null 2>&1 &
PID3=$!

./new_test p4 $POWER $NUM_OPS > /dev/null 2>&1 &
PID4=$!

# Give servers a moment to start listening
sleep 0.5

echo "[Script] Servers running (PIDs: $PID1, $PID2, $PID3, $PID4)"
echo "---------------------------------------------------------------"

# 2. Run Client (Foreground)
./new_test client $POWER $NUM_OPS

# 3. Cleanup
echo "---------------------------------------------------------------"
echo "[Script] Cleaning up and killing servers..."
kill $PID1 $PID2 $PID3 $PID4 2>/dev/null
wait $PID1 $PID2 $PID3 $PID4 2>/dev/null
echo "[Script] Finished."