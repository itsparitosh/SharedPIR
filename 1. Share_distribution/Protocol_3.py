
import itertools

def generate_image_perfect_distribution():
    print("--- Image-Perfect Replicated Secret Sharing Generator ---")
    n, t = 5, 4 # Hardcoded to match your setting exactly
    r = n - t + 1
    
    all_parties = list(range(n))
    # Generate subsets in a stable order to match the indexing
    subsets = list(itertools.combinations(all_parties, r))
    
    seen_subsets = set()
    pairs = []
    for s in subsets:
        s_tuple = tuple(sorted(s))
        if s_tuple in seen_subsets: continue
        sc = tuple(sorted(set(all_parties) - set(s)))
        pairs.append((s_tuple, sc))
        seen_subsets.add(s_tuple); seen_subsets.add(sc)

    party_data = {i: {"D": [], "zeta": [], "D_tilde": []} for i in range(n)}
    
    share_id = 0
    # The image uses this specific sequence of DPF pairs
    # Pair 1: {0,1} vs {2,3} | Pair 2: {0,2} vs {1,3} | Pair 3: {0,3} vs {1,2}
    for s, sc in pairs:
        for holders, non_holders in [(s, sc), (sc, s)]:
            label_a = share_id
            for p in holders:
                party_data[p]["D"].append(f"D{label_a}")
            
            # --- IMAGE MAPPING LOGIC ---
            # Default: ζ goes to the smaller index, D̃ goes to the larger index.
            # EXCEPTION: For D5, we swap them to match the image balance.
            if label_a == 5:
                zeta_idx, tilde_idx = 1, 0
            else:
                zeta_idx, tilde_idx = 0, 1
            
            zeta_holder = non_holders[zeta_idx]
            tilde_holder = non_holders[tilde_idx]
            
            party_data[zeta_holder]["zeta"].append(f"ζ{label_a}")
            party_data[tilde_holder]["D_tilde"].append(f"D̃{label_a}")
            share_id += 1

    # Output formatting to match your image exactly
    for p_id in range(n):
        data = party_data[p_id]
        print(f"S{p_id}")
        print(f"({', '.join(data['D'])})")
        print(f"({', '.join(data['D_tilde'] + data['zeta'])})")
        print("-" * 20)

if __name__ == "__main__":
    generate_image_perfect_distribution()