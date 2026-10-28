def rle_encode(input_path, output_path):
    with open(input_path, 'rb') as f:
        data = f.read()

    # The 112 most valuable x86 sequences ordered from highest priority to lowest
    dict_sequences = [
        # --- 4-Byte Blocks ---
        bytes([0x83, 0xC4, 0x10, 0x85]), bytes([0x20, 0x20, 0x20, 0x20]),
        bytes([0x5B, 0x5E, 0x5F, 0x5D]), bytes([0x56, 0x53, 0x83, 0xEC]),
        bytes([0x55, 0x57, 0x56, 0x53]), bytes([0x5E, 0x5F, 0x5D, 0xC3]),
        bytes([0xC3, 0x55, 0x57, 0x56]), bytes([0xC4, 0x10, 0x85, 0xC0]),
        bytes([0x89, 0xE7, 0xF3, 0xA5]),
        # --- 3-Byte Blocks ---
        bytes([0x8B, 0x44, 0x24]), bytes([0x89, 0x44, 0x24]), bytes([0x83, 0xC4, 0x10]),
        bytes([0x83, 0xEC, 0x0C]), bytes([0x8B, 0x54, 0x24]), bytes([0x53, 0x83, 0xEC]),
        bytes([0x8B, 0x4C, 0x24]), bytes([0x5B, 0x5E, 0x5F]), bytes([0x90, 0x02, 0xC0]),
        bytes([0x8B, 0x5C, 0x24]), bytes([0x57, 0x56, 0x53]), bytes([0x02, 0xC0, 0xE8]),
        bytes([0x5E, 0x5F, 0x5D]), bytes([0x56, 0x53, 0x83]), bytes([0x8B, 0x74, 0x24]),
        bytes([0x89, 0x54, 0x24]), bytes([0x8B, 0x7C, 0x24]), bytes([0xB9, 0x02, 0xC0]),
        bytes([0x8D, 0x44, 0x24]), bytes([0x89, 0x4C, 0x24]), bytes([0x8A, 0x44, 0x24]),
        bytes([0xC7, 0x44, 0x24]), bytes([0x8B, 0x84, 0x24]), bytes([0x8D, 0x74, 0x24]),
        bytes([0x8D, 0x54, 0x24]), bytes([0x88, 0x44, 0x24]), bytes([0xB4, 0x02, 0xC0]),
        bytes([0x89, 0xD8, 0xE8]), bytes([0x83, 0xC4, 0x1C]), bytes([0xBB, 0x02, 0xC0]),
        # --- 2-Byte Blocks ---
        bytes([0x44, 0x24]), bytes([0x02, 0xC0]), bytes([0x83, 0xC4]), bytes([0x8B, 0x44]),
        bytes([0x83, 0xEC]), bytes([0x54, 0x24]), bytes([0x74, 0x24]), bytes([0x89, 0x44]),
        bytes([0x01, 0x00]), bytes([0xC4, 0x10]), bytes([0x7C, 0x24]), bytes([0x4C, 0x24]),
        bytes([0xEC, 0x0C]), bytes([0x24, 0x0C]), bytes([0x24, 0x10]), bytes([0x5B, 0x5E]),
        bytes([0x24, 0x08]), bytes([0x24, 0x1C]), bytes([0x5C, 0x24]), bytes([0x8B, 0x54]),
        bytes([0x03, 0x00]), bytes([0x56, 0x53]), bytes([0x24, 0x14]), bytes([0x85, 0xC0]),
        bytes([0x20, 0x20]), bytes([0x31, 0xC0]), bytes([0x24, 0x18]), bytes([0x24, 0x04]),
        bytes([0x5E, 0x5F]), bytes([0x53, 0x83]), bytes([0x8B, 0x4C]), bytes([0x57, 0x56]),
        bytes([0xC0, 0xE8]), bytes([0x90, 0x02]), bytes([0x24, 0x20]), bytes([0x8B, 0x5C]),
        bytes([0x84, 0x24]), bytes([0x0F, 0xB6]), bytes([0x31, 0xD2]), bytes([0x5F, 0x5D]),
        bytes([0x24, 0x28]), bytes([0x6A, 0x00]), bytes([0x24, 0x24]), bytes([0x89, 0x54]),
        bytes([0x02, 0x00]), bytes([0x8B, 0x74]), bytes([0x10, 0x8B]), bytes([0x89, 0xD8]),
        bytes([0xC0, 0x74]), bytes([0x55, 0x57]), bytes([0x24, 0x2C]), bytes([0x89, 0xF0]),
        bytes([0xB9, 0x02]), bytes([0xF3, 0xA5]), bytes([0xC0, 0x89]), bytes([0x08, 0x89]),
        bytes([0x04, 0x00]), bytes([0xC0, 0x00]), bytes([0x50, 0xE8]), bytes([0x10, 0x89]),
        bytes([0x5D, 0xC3]), bytes([0x24, 0x30]), bytes([0x8B, 0x7C]), bytes([0x0F, 0x85]),
        bytes([0x66, 0x89]), bytes([0xC0, 0x75]), bytes([0x0C, 0x8B]), bytes([0x89, 0xC3]),
        bytes([0x08, 0x8B]), bytes([0x0C, 0x89]), bytes([0xC0, 0x83]), bytes([0x04, 0x24]),
        bytes([0x24, 0x34]), bytes([0x01, 0xC0]), bytes([0x24, 0x3C]), bytes([0x8D, 0x44]),
        bytes([0x0F, 0x84]), bytes([0xC3, 0x55]), bytes([0x24, 0x40]), bytes([0x0F, 0xBF])
    ]

    encoded = bytearray()
    i = 0
    n = len(data)

    while i < n:
        # --- 1. Check for 0x00 runs (Greedy Chunking matching spec from largest to smallest) ---
        if data[i] == 0x00:
            z_run = 0
            while i + z_run < n and data[i + z_run] == 0x00:
                z_run += 1
            
            # Smart lookahead check: If it's only a single 0x00, let the literal run logic absorb it
            # if we are currently inside or building a code path, preventing token fragmentation.
            if z_run == 1 and i > 0:
                pass
            else:
                if z_run >= 1024:
                    encoded.append(0xFC)
                    i += 1024
                elif z_run >= 384:
                    encoded.append(0xFB)
                    i += 384
                elif z_run >= 128:
                    encoded.append(0xFA)
                    i += 128
                elif z_run >= 48:
                    encoded.append(0xF9)
                    i += 48
                elif z_run >= 16:
                    encoded.append(0xF8)
                    i += 16
                else:
                    # 0xF0 - 0xF7 covers lengths of 2 to 9 zeros: (Length = Token - 0xF0 + 2)
                    # Cap at 9 bytes max to fit within 0xF7
                    chunk_len = min(z_run, 9) 
                    encoded.append(0xF0 + (chunk_len - 2))
                    i += chunk_len
                continue

        # --- 2. Check for 0xFF runs ---
        if data[i] == 0xFF:
            f_run = 0
            while i + f_run < n and data[i + f_run] == 0xFF:
                f_run += 1
            
            if f_run == 1 and i > 0:
                pass
            else:
                if f_run >= 4:
                    encoded.append(0xFF) # 4x 0xFF
                    i += 4
                elif f_run == 3:
                    encoded.append(0xFE) # 3x 0xFF
                    i += 3
                elif f_run == 2:
                    encoded.append(0xFD) # 2x 0xFF
                    i += 2
                continue

        # --- 3. Check for Static Dictionary Sequences (0x80 - 0xEF) ---
        matched_dict = False
        for idx, seq in enumerate(dict_sequences):
            if data[i:i+len(seq)] == seq:
                encoded.append(0x80 + idx)
                i += len(seq)
                matched_dict = True
                break
        
        if matched_dict:
            continue

        # --- 4. Handle Literal (Non-RLE) runs ---
        literal_bytes = bytearray()
        while i < n and len(literal_bytes) < 128:  # 0x00 to 0x7F allows up to 128 bytes (N+1)
            
            # Stop if we hit a substantial run of 0x00 or 0xFF
            if data[i] == 0x00:
                check_z = 0
                while i + check_z < n and data[i + check_z] == 0x00:
                    check_z += 1
                if check_z >= 2:  # 2 or more zeros can use 0xF0 tokens, stop literal block
                    break
                    
            if data[i] == 0xFF:
                check_f = 0
                while i + check_f < n and data[i + check_f] == 0xFF:
                    check_f += 1
                if check_f >= 2:  # 2 or more 0xFFs use 0xFD-0xFF tokens, stop literal block
                    break

            # Stop if an entry in our dictionary matches ahead
            dict_ahead = False
            for seq in dict_sequences:
                if data[i:i+len(seq)] == seq:
                    dict_ahead = True
                    break
            if dict_ahead:
                break

            literal_bytes.append(data[i])
            i += 1
        
        if literal_bytes:
            # Token 0x00 means 1 byte payload, 0x7F means 128 bytes payload
            header = len(literal_bytes) - 1
            encoded.append(header)
            encoded.extend(literal_bytes)

    with open(output_path, 'wb') as f:
        f.write(encoded)

if __name__ == "__main__":
    # Replace 'input.bin' with your actual source file
    rle_encode('build/output/kernel.exe', 'build/output/kernel.cpz')
    print("Compression complete.")
