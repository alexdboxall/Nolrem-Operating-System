from collections import Counter

def find_common_sequences(file_path, top_n=16):
    with open(file_path, 'rb') as f:
        data = f.read()

    # Generate sequences using a list comprehension over sliding windows
    seq_2byte = [data[i:i+2] for i in range(len(data) - 1)]
    seq_3byte = [data[i:i+3] for i in range(len(data) - 2)]
    seq_4byte = [data[i:i+4] for i in range(len(data) - 3)]

    # Count occurrences
    counter_2b = Counter(seq_2byte)
    counter_3b = Counter(seq_3byte)
    counter_4b = Counter(seq_4byte)

    # Helper function to format byte sequences nicely as hex strings
    format_hex = lambda b: ' '.join(f'{x:02X}' for x in b)

    print(f"--- Top {top_n} Most Common 2-Byte Sequences ---")
    for seq, count in counter_2b.most_common(top_n):
        print(f"Sequence: [{format_hex(seq)}] | Count: {count}")

    print(f"\n--- Top {top_n} Most Common 3-Byte Sequences ---")
    for seq, count in counter_3b.most_common(top_n):
        print(f"Sequence: [{format_hex(seq)}] | Count: {count}")

    print(f"\n--- Top {top_n} Most Common 4-Byte Sequences ---")
    for seq, count in counter_4b.most_common(top_n):
        print(f"Sequence: [{format_hex(seq)}] | Count: {count}")

if __name__ == "__main__":
    # Replace 'input.bin' with the path to your binary file
    find_common_sequences('build/output/kernel.cpz', top_n=114)


'''
8B 44 24
89 44 24
83 C4 10
83 EC 0C
8B 54 24
53 83 EC
44 24
02 C0
83 C4
8B 44
83 EC
54 24
82 24
89 44
C4 10
7C 24

'''