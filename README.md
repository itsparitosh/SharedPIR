# Private Information Retrieval (PIR) Protocols Implementation

## Overview
This repository contains implementations of four different PIR protocols. Each protocol is placed in a separate folder and can be executed using its corresponding script.

Each script takes two input parameters:
- **Database size (n)** → actual database size is 2^n  
- **Number of operations (ops)** → number of queries to execute  

For example:
./run_pir.sh 20 3

This means:
- Database size = 2^20  
- Operations = 3  

---

## Project Structure

The repository contains the following protocols:

- Hafiz-Henry Protocol  
- Protocol 2  
- Protocol 3  
- Protocol 4  

Each protocol has its own script file for execution.

---

## How to Run

Go to the respective protocol folder and run:

### Hafiz-Henry
./run_pir.sh <db_size> <operations>

### Protocol 2
./run_p2.sh <db_size> <operations>

### Protocol 3
./run_p3.sh <db_size> <operations>

### Protocol 4
./run_p4.sh <db_size> <operations>

---
**Command:**
```bash
./run_p4.sh 20 5

**Output:**
================================================
 Compiling.....
=================================================
=================================================
 Starting Test
 DB Size: 2^20
 Operations: 5
=================================================
[Script] Servers started in background. Waiting 1 second for ports to bind...
[Script] Starting Client...
-------------------------------------------------

[Client] Connected to 4 parties. DB Size: 1048576 | Columns: 3 | Read Ops: 5
[Client] Executing Warmup (practice run)... Done!

Read Op 1/5 [Idx: 584218] [OK] Data: [584219, 584220, 584221]
  └─ Read Timings  -> Query: 0.035ms | DPF Eval: 18.966ms | XOR: 12.959ms | Recon: 0.005ms
  └─ Network Costs -> Up: 4356 B | Down: 72 B

Read Op 2/5 [Idx: 440272] [OK] Data: [440273, 440274, 440275]
  └─ Read Timings  -> Query: 0.025ms | DPF Eval: 28.945ms | XOR: 18.087ms | Recon: 0.002ms
  └─ Network Costs -> Up: 4356 B | Down: 72 B

Read Op 3/5 [Idx: 539618] [OK] Data: [539619, 539620, 539621]
  └─ Read Timings  -> Query: 0.023ms | DPF Eval: 31.029ms | XOR: 19.079ms | Recon: 0.002ms
  └─ Network Costs -> Up: 4356 B | Down: 72 B

Read Op 4/5 [Idx: 838098] [OK] Data: [838099, 838100, 838101]
  └─ Read Timings  -> Query: 0.022ms | DPF Eval: 29.667ms | XOR: 18.306ms | Recon: 0.001ms
  └─ Network Costs -> Up: 4356 B | Down: 72 B

Read Op 5/5 [Idx: 388192] [OK] Data: [388193, 388194, 388195]
  └─ Read Timings  -> Query: 0.022ms | DPF Eval: 20.105ms | XOR: 12.950ms | Recon: 0.001ms
  └─ Network Costs -> Up: 4356 B | Down: 72 B

================ READ RESULTS ===================
Total Operations      : 5
Total Key Gen Time    : 0.1269 ms
-------------------------------------------------
Avg Query Time        : 0.0254 ms
Avg Server DPF Time   : 25.7424 ms
Avg Server XOR Time   : 16.2762 ms
Avg Reconstruct Time  : 0.0023 ms
-------------------------------------------------
Avg Upload / Op       : 4356 Bytes
Avg Download / Op     : 72 Bytes
=================================================
-------------------------------------------------
[Script] Test execution completed.

[Script] Cleaning up background server processes...
---

## Notes
- Scripts automatically handle compilation and execution.
- Servers are started and terminated within the scripts.

---

## Requirements
- Linux environment
- Bash shell
- GCC / standard build tools

---

## Summary
This repository provides implementations of multiple PIR protocols under a unified setup. Each protocol can be executed independently and outputs performance metrics such as computation time and communication cost.
