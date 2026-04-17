
#!/bin/bash

# -------------------------------
# 0. Check command-line arguments
# -------------------------------
if [ $# -lt 2 ]; then
    echo "Usage: $0 <POWER> <NUM_OPS>"
    echo "Example: $0 8 10"
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
g++ -std=c++20 -O3 -pthread -march=native -Wno-ignored-attributes main.cpp -o pir_test -lbsd

if [ $? -ne 0 ]; then
    echo "Compilation failed! Exiting."
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

# -------------------------------
# 3. Launch servers
# -------------------------------
./pir_test p0 $POWER $NUM_OPS > /dev/null 2>&1 &
P0_PID=$!

./pir_test p1 $POWER $NUM_OPS > /dev/null 2>&1 &
P1_PID=$!

./pir_test p2 $POWER $NUM_OPS > /dev/null 2>&1 &
P2_PID=$!

./pir_test p3 $POWER $NUM_OPS > /dev/null 2>&1 &
P3_PID=$!

# Allow servers time to start
sleep 1

# -------------------------------
# 4. Run client
# -------------------------------
echo "=> Starting Client..."
echo "--------------------------------------------------------"

./pir_test client $POWER $NUM_OPS

echo "--------------------------------------------------------"
echo "=> Client finished. Waiting for servers to shut down..."

# -------------------------------
# 5. Wait for servers
# -------------------------------
wait $P0_PID
wait $P1_PID
wait $P2_PID
wait $P3_PID

echo "=> Test Complete!"