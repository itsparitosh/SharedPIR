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

## Example Runs and Outputs

### Protocol 1
Command:
./run_pir.sh 20 5

Output (sample):
Starting Test
DB Size: 2^20
Operations: 5

[Client] Connected. DB Size: 2^20 (1048576 rows) | Operations: 5

Op 1/5 [Reading Idx: 397876] [OK]  
Op 2/5 [Reading Idx: 397876] [OK]  
...

**READ RESULTS:**
Total Ops: 5  
Avg Query Time: 0.0168 ms  
Avg Server Respond: ~19 ms  
Avg Reconstruct Time: ~31 ms  
Upload / Op: 4480 Bytes  
Download / Op: 32 Bytes  

---

### Protocol 2
Command:
./run_p2.sh 20 5

Output (sample):
Starting Test
DB Size: 2^20
Operations: 5

[Client] Connected to 4 parties

Read Op 1/5 [OK]  
Read Op 2/5 [OK]  
...

**READ RESULTS:**
Total Operations: 5  
Avg Query Time: ~0.01 ms  
Avg Server DPF Time: ~20 ms  
Avg Server XOR Time: ~13 ms  
Upload / Op: 4356 Bytes  
Download / Op: 72 Bytes  

---

### Protocol 3
Command:
./run_p3.sh 20 5

Output (sample):
Starting Test
DB Size: 2^20
Operations: 5

Op 1/5 [OK]
  Write -> Query: ~0.12 ms  
  Read  -> Query: ~0.04 ms  

...

**WRITE AVERAGES:**
Avg Query Time: ~0.07 ms  
Avg Server Eval: ~46 ms  

**READ AVERAGES:**
Avg Query Time: ~0.03 ms  
Avg Server Eval: ~31 ms  
Download / Op: 72 Bytes  

---

### Protocol 4
Command:
./run_p4.sh 20 5

Output (sample):
Starting Test
DB Size: 2^20
Operations: 5

Op 1/5 [OK]
  Write Stats -> Query: ~0.07 ms  
  Read Stats  -> Query: ~0.06 ms  

...

**WRITE AVERAGES:**
Avg Query Time: ~0.05 ms  
Avg Server DPF Eval: ~36 ms  

**READ AVERAGES:**
Avg Query Time: ~0.04 ms  
Avg Server DPF Eval: ~30 ms  
Download / Op: 24 Bytes  

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
