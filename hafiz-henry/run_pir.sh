
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
echo "=> Compiling the PIR code..."
g++ -std=c++20 -O3 -pthread -march=native -Wno-ignored-attributes main.cpp -o pir_test -lbsd

if [ $? -ne 0 ]; then
    echo "Compilation failed! Exiting."
    exit 1
fi

# -------------------------------
# 2. Show parameters
# -------------------------------
echo "=> Parameters:"
echo "   POWER   = $POWER (Database size = 2^$POWER)"
echo "   NUM_OPS = $NUM_OPS"

echo "=> Starting 4 Database Servers in the background..."

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