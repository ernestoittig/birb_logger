const char *common_name_from_species_name(const char* name) {
    return strchr(name, '_') + 1;
}
