import itertools
import math

def strict_threshold_db_setup():
    print("=== Scheme 1: Strict Threshold DB Setup ===")
    
    # 1. User Input
    try:
        # n = int(input("Enter the total number of parties (n): "))
        # t = int(input("Enter the reconstruction threshold (t): "))
        n = 4
        t = 2
    except ValueError:
        print("Error: Please enter valid numbers.")
        return

    # 2. Compatibility Check
    if t < 1 or t > n:
        print(f"\n[!] INCOMPATIBLE: The threshold 't' ({t}) must be between 1 and 'n' ({n}).")
        return
    else:
        print(f"\n[+] COMPATIBLE: n={n} and t={t} are valid parameters.")

    # 3. Distribution Logic
    # To stop (t-1) parties from reconstructing, we exclude groups of size (t-1)
    exclude_size = t - 1
    
    # CHANGED: Start parties from 0 to n-1 (e.g., 0, 1, 2, 3)
    parties = list(range(0, n))
    
    # Generate all subsets of parties to exclude
    excluded_sets = list(itertools.combinations(parties, exclude_size))
    total_shares = len(excluded_sets)
    
    # Create empty lists for each party
    party_shares = {p: [] for p in parties}
    
    # Hand out the shares
    # CHANGED: Start enumeration from 0 so shares are named D0, D1, etc.
    for share_idx, excluded in enumerate(excluded_sets, start=0):
        share_name = f"D{share_idx}"
        for p in parties:
            # A party gets the share ONLY if they are not in the excluded group
            if p not in excluded:
                party_shares[p].append(share_name)

    # 4. Display Results
    print("\n--- Scheme Details ---")
    print(f"Rule: Any {t} parties can reconstruct. Any {t-1} parties cannot.")
    print(f"Total database chunks created: {total_shares}")
    
    # Calculate shares per party safely
    shares_per_party = math.comb(n - 1, t - 1) if n - 1 >= t - 1 else 0
    print(f"Number of chunks held by each party: {shares_per_party}")
    
    print("\n--- Distribution Map ---")
    for p in parties:
        print(f"Party P{p} holds: {', '.join(party_shares[p])}")

# Run the program
strict_threshold_db_setup()