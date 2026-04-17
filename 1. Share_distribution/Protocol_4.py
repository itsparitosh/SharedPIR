def generate_optimized_architecture(n):
    # The optimization lowers the minimum required servers from 4 to 3
    if n < 3:
        return "Requires at least 3 parties to safely distribute D, zeta, and D_tilde."

    print(f"--- Optimized Strict {n}-out-of-{n} Architecture (0-Based) ---")
    print(f"Total Database Shares Generated: {2 * n}")
    print("Pairs: (D0, D1), (D2, D3), etc.")
    print("Routing: zeta to +1, D_tilde to +2")
    print("------------------------------------------------------\n")

    for i in range(n):  # i loops from 0 to n-1
        # 1. The Unique Share (Even index)
        unique_share = 2 * i
        
        # 2. The Duplicated Shares (Odd index)
        prev_party_1 = (i - 1) % n
        prev_party_2 = (i - 2) % n
        shared_1 = prev_party_1 * 2 + 1
        shared_2 = prev_party_2 * 2 + 1
        
        # 3. The Blinding Term (zeta)
        # Because sender sends to +1, the receiver gets it from -1
        zeta_source_party = (i - 1) % n
        zeta_index = 2 * zeta_source_party
        
        # 4. The Blinded Database Share (D_tilde)
        # Because sender sends to +2, the receiver gets it from -2
        tilde_source_party = (i - 2) % n
        tilde_index = 2 * tilde_source_party
        
        # Formatting the output
        print(f"P{i} holds:")
        print(f"  [Database Shares]")
        print(f"   - Unique: D{unique_share}")
        print(f"   - Duplicated: D{shared_1}, D{shared_2}")
        print(f"  [Blinding Data]")
        print(f"   - Blinding Term (zeta): \u03B6_{zeta_index} (from P{zeta_source_party})")
        print(f"   - Blinded Share (D_tilde): ~D{tilde_index} (from P{tilde_source_party})")
        print()

# Example running with the new minimum of 3 parties
generate_optimized_architecture(4)