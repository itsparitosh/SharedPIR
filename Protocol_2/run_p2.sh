# #!/bin/bash

# # 1. Validate Input Arguments
# if [ "$#" -ne 2 ]; then
#     echo "Usage: $0 <power> <num_ops>"
#     echo "Example: $0 10 5  (Tests DB size 2^10 with 5 random operations)"
#     exit 1
# fi

# POWER=$1
# NUM_OPS=$2
# APP="./new_test" # Change this if your compiled executable has a different name

# # 2. Check if the executable exists
# if [ ! -f "$APP" ]; then
#     echo "Error: Executable '$APP' not found in the current directory."
#     echo "Please compile your C++ code first (e.g., g++ -O3 ... -o new_test)."
#     exit 1
# fi

# echo "================================================="
# echo " Starting Test"
# echo " DB Size: 2^$POWER"
# echo " Operations: $NUM_OPS"
# echo "================================================="

# # Array to keep track of server Process IDs
# PIDS=()

# # 3. Cleanup Function
# # This ensures servers don't remain stuck in the background if the script exits or crashes
# cleanup() {
#     echo -e "\n[Script] Cleaning up background server processes..."
#     for pid in "${PIDS[@]}"; do
#         kill -9 $pid 2>/dev/null
#     done
# }
# # Attach the cleanup function to EXIT, INT (Ctrl+C), and TERM signals
# trap cleanup EXIT INT TERM

# # 4. Start the 5 Servers
# for party in p1 p2 p3 p4 p5; do
#     # Run in background and suppress standard output to keep terminal clean
#     $APP $party $POWER $NUM_OPS > /dev/null 2>&1 &
#     PIDS+=($!)
# done

# echo "[Script] Servers started in background. Waiting 1 second for ports to bind..."
# sleep 1

# # 5. Start the Client in the foreground
# echo "[Script] Starting Client..."
# echo "-------------------------------------------------"
# $APP client $POWER $NUM_OPS
# echo "-------------------------------------------------"

# echo "[Script] Test execution completed."
# # The 'trap cleanup' will automatically kill the servers when the script reaches this end point.




# ==================================

#!/bin/bash

# 1. Validate Input Arguments
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <power> <num_ops>"
    echo "Example: $0 10 5  (Tests DB size 2^10 with 5 random operations)"
    exit 1
fi

POWER=$1
NUM_OPS=$2
APP="./new_test"

# 2. Silently Compile the C++ Code
echo "================================================="
echo " Compiling....."
echo "================================================="

# The '> /dev/null 2>&1' part completely hides all compilation output and warnings
g++ -O3 -std=c++20 -fopenmp -march=native -maes -Wno-ignored-attributes new_test.cpp -o new_test -lboost_coroutine -lboost_context -pthread > /dev/null 2>&1

# Check if the compilation was successful
if [ $? -ne 0 ]; then
    echo "[Error] Compilation failed! Try compiling manually in the terminal to see the errors."
    exit 1
fi

echo "================================================="
echo " Starting Test"
echo " DB Size: 2^$POWER"
echo " Operations: $NUM_OPS"
echo "================================================="

# Array to keep track of server Process IDs
PIDS=()

# 3. Cleanup Function
# This ensures servers don't remain stuck in the background if the script exits or crashes
cleanup() {
    echo -e "\n[Script] Cleaning up background server processes..."
    for pid in "${PIDS[@]}"; do
        kill -9 $pid 2>/dev/null
    done
}
# Attach the cleanup function to EXIT, INT (Ctrl+C), and TERM signals
trap cleanup EXIT INT TERM

# 4. Start the 5 Servers 
# (Note: If you are using the 4-party code we wrote earlier, change this to: for party in p1 p2 p3 p4; do)
for party in p1 p2 p3 p4 p5; do
    # Run in background and suppress standard output to keep terminal clean
    $APP $party $POWER $NUM_OPS > /dev/null 2>&1 &
    PIDS+=($!)
done

echo "[Script] Servers started in background. Waiting 1 second for ports to bind..."
sleep 1

# 5. Start the Client in the foreground
echo "[Script] Starting Client..."
echo "-------------------------------------------------"
$APP client $POWER $NUM_OPS
echo "-------------------------------------------------"

echo "[Script] Test execution completed."
# The 'trap cleanup' will automatically kill the servers when the script reaches this end point.
