
struct file_ent {
    uint16_t name_len;
    uint8_t name_utf8[name_len];
    uint64_t file_size;
    uint64_t num_data_clusters;
    uint64_t data_clusters[num_data_clusters];

};