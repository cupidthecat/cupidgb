#include "cupid/gb/cheats.h"

#include "cupid/gb/cartridge.h"

#include <stdlib.h>
#include <string.h>

enum { ADDRESS_BUCKETS = 256, FILE_HEADER_BYTES = 8, FILE_RECORD_BYTES = 136 };

typedef struct CheatList {
    CupidGbCheat **items;
    size_t count, capacity;
} CheatList;

typedef struct CheatEntry {
    CupidGbCheat record;
    size_t index;
} CheatEntry;

struct CupidGbCheats {
    CheatList records;
    CheatList addresses[ADDRESS_BUCKETS];
    bool enabled;
};

static bool reserve(CheatList *list, size_t count) {
    if (count <= list->capacity)
        return true;
    if (count > SIZE_MAX / sizeof(*list->items))
        return false;
    size_t capacity = list->capacity ? list->capacity : 4;
    if (capacity <= SIZE_MAX / 2)
        capacity *= 2;
    if (capacity < count || capacity > SIZE_MAX / sizeof(*list->items))
        capacity = count;
    CupidGbCheat **items = realloc(list->items, capacity * sizeof(*items));
    if (!items)
        return false;
    list->items = items;
    list->capacity = capacity;
    return true;
}

static void remove_item(CheatList *list, const CupidGbCheat *record) {
    for (size_t index = 0; index < list->count; ++index) {
        if (list->items[index] == record) {
            list->items[index] = list->items[--list->count];
            return;
        }
    }
}

static void normalize_description(CupidGbCheat *record) {
    size_t length = 0;
    while (length < CUPID_GB_CHEAT_DESCRIPTION_SIZE - 1 && record->description[length])
        ++length;
    memset(record->description + length, 0, CUPID_GB_CHEAT_DESCRIPTION_SIZE - length);
}

CupidGbCheats *cupid_gb_cheats_create(void) {
    return calloc(1, sizeof(CupidGbCheats));
}

CupidGbCheats *cupid_gb_cheats_clone(const CupidGbCheats *cheats) {
    if (!cheats)
        return NULL;
    CupidGbCheats *copy = cupid_gb_cheats_create();
    if (!copy)
        return NULL;
    for (size_t index = 0; index < cheats->records.count; ++index) {
        if (cupid_gb_cheats_add(copy, cheats->records.items[index]) != CUPID_GB_CHEAT_OK) {
            cupid_gb_cheats_destroy(copy);
            return NULL;
        }
    }
    for (unsigned index = 0; index < ADDRESS_BUCKETS; ++index) {
        const CheatList *source = &cheats->addresses[index];
        for (size_t position = 0; position < source->count; ++position) {
            const CheatEntry *entry = (const CheatEntry *)source->items[position];
            copy->addresses[index].items[position] = copy->records.items[entry->index];
        }
    }
    copy->enabled = cheats->enabled;
    return copy;
}

void cupid_gb_cheats_clear(CupidGbCheats *cheats) {
    if (!cheats)
        return;
    for (size_t index = 0; index < cheats->records.count; ++index)
        free(cheats->records.items[index]);
    cheats->records.count = 0;
    for (unsigned index = 0; index < ADDRESS_BUCKETS; ++index)
        cheats->addresses[index].count = 0;
}

void cupid_gb_cheats_destroy(CupidGbCheats *cheats) {
    if (!cheats)
        return;
    cupid_gb_cheats_clear(cheats);
    for (unsigned index = 0; index < ADDRESS_BUCKETS; ++index)
        free(cheats->addresses[index].items);
    free(cheats->records.items);
    free(cheats);
}

void cupid_gb_set_cheats(CupidGb *gb, const CupidGbCheats *cheats) {
    if (gb)
        gb->cheats = cheats;
}

size_t cupid_gb_cheats_count(const CupidGbCheats *cheats) {
    return cheats ? cheats->records.count : 0;
}

const CupidGbCheat *cupid_gb_cheats_get(const CupidGbCheats *cheats, size_t index) {
    return cheats && index < cheats->records.count ? cheats->records.items[index] : NULL;
}

bool cupid_gb_cheats_enabled(const CupidGbCheats *cheats) {
    return cheats && cheats->enabled;
}

void cupid_gb_cheats_set_enabled(CupidGbCheats *cheats, bool enabled) {
    if (cheats)
        cheats->enabled = enabled;
}

CupidGbCheatResult cupid_gb_cheats_add(CupidGbCheats *cheats, const CupidGbCheat *cheat) {
    if (!cheats || !cheat)
        return CUPID_GB_CHEAT_INVALID_ARGUMENT;
    CheatEntry *entry = malloc(sizeof(*entry));
    CupidGbCheat *copy = entry ? &entry->record : NULL;
    if (!copy)
        return CUPID_GB_CHEAT_OUT_OF_MEMORY;
    *copy = *cheat;
    normalize_description(copy);
    CheatList *bucket = &cheats->addresses[copy->address % ADDRESS_BUCKETS];
    if (!reserve(&cheats->records, cheats->records.count + 1) ||
        !reserve(bucket, bucket->count + 1)) {
        free(copy);
        return CUPID_GB_CHEAT_OUT_OF_MEMORY;
    }
    entry->index = cheats->records.count;
    cheats->records.items[cheats->records.count++] = copy;
    bucket->items[bucket->count++] = copy;
    return CUPID_GB_CHEAT_OK;
}

CupidGbCheatResult cupid_gb_cheats_update(CupidGbCheats *cheats, size_t index,
                                          const CupidGbCheat *cheat) {
    if (!cheats || !cheat || index >= cheats->records.count)
        return CUPID_GB_CHEAT_INVALID_ARGUMENT;
    const CupidGbCheat replacement = *cheat;
    CupidGbCheat *record = cheats->records.items[index];
    if (record->address != replacement.address) {
        CheatList *destination = &cheats->addresses[replacement.address % ADDRESS_BUCKETS];
        if (!reserve(destination, destination->count + 1))
            return CUPID_GB_CHEAT_OUT_OF_MEMORY;
        remove_item(&cheats->addresses[record->address % ADDRESS_BUCKETS], record);
        destination->items[destination->count++] = record;
    }
    *record = replacement;
    normalize_description(record);
    return CUPID_GB_CHEAT_OK;
}

bool cupid_gb_cheats_remove(CupidGbCheats *cheats, size_t index) {
    if (!cheats || index >= cheats->records.count)
        return false;
    CupidGbCheat *record = cheats->records.items[index];
    remove_item(&cheats->addresses[record->address % ADDRESS_BUCKETS], record);
    cheats->records.items[index] = cheats->records.items[--cheats->records.count];
    if (index < cheats->records.count)
        ((CheatEntry *)cheats->records.items[index])->index = index;
    free(record);
    return true;
}

static unsigned hexadecimal(char digit) {
    if (digit >= '0' && digit <= '9')
        return (unsigned)(digit - '0');
    if (digit >= 'a' && digit <= 'f')
        return (unsigned)(digit - 'a') + 10u;
    if (digit >= 'A' && digit <= 'F')
        return (unsigned)(digit - 'A') + 10u;
    return 16u;
}

bool cupid_gb_cheat_decode(const char *code, const char *description, bool enabled,
                           CupidGbCheat *cheat) {
    if (!code || !cheat)
        return false;
    unsigned digits[9], length = 0;
    for (const char *cursor = code; *cursor; ++cursor) {
        if (*cursor == '-' && length)
            continue;
        if (length == 9 || hexadecimal(*cursor) > 15u)
            return false;
        digits[length++] = hexadecimal(*cursor);
    }
    CupidGbCheat decoded = {.bank = CUPID_GB_CHEAT_ANY_BANK, .enabled = enabled};
    if (length == 8 && strlen(code) == 8) {
        const unsigned bank = digits[0] * 16u + digits[1];
        decoded.bank = bank == 1u ? CUPID_GB_CHEAT_ANY_BANK : (uint16_t)(bank & 15u);
        decoded.value = (uint8_t)(digits[2] * 16u + digits[3]);
        decoded.address =
            (uint16_t)(digits[6] * 4096u + digits[7] * 256u + digits[4] * 16u + digits[5]);
    } else if (length == 6 || length == 9) {
        decoded.value = (uint8_t)(digits[0] * 16u + digits[1]);
        decoded.address =
            (uint16_t)((digits[5] * 4096u + digits[2] * 256u + digits[3] * 16u + digits[4]) ^
                       0xf000u);
        if (decoded.address >= 0x8000u)
            return false;
        decoded.use_compare = length == 9;
        if (decoded.use_compare) {
            const unsigned encoded = digits[6] * 16u + digits[8];
            decoded.compare_value = (uint8_t)(((encoded >> 2u) | (encoded << 6u)) ^ 0xbau);
        }
    } else {
        return false;
    }
    if (description) {
        size_t description_length = strlen(description);
        if (description_length >= sizeof(decoded.description))
            description_length = sizeof(decoded.description) - 1;
        memcpy(decoded.description, description, description_length);
    }
    *cheat = decoded;
    return true;
}

CupidGbCheatResult cupid_gb_cheats_import(CupidGbCheats *cheats, const char *code,
                                          const char *description, bool enabled) {
    CupidGbCheat record;
    if (!cupid_gb_cheat_decode(code, description, enabled, &record))
        return CUPID_GB_CHEAT_INVALID_DATA;
    return cupid_gb_cheats_add(cheats, &record);
}

static uint16_t selected_bank(const CupidGb *gb, uint16_t address) {
    if (address < 0x8000u)
        return (uint16_t)cupid_gb_selected_rom_bank(gb, address < 0x4000u);
    if (address >= 0xa000u && address < 0xc000u) {
        if (gb->cartridge_ram_size && !(gb->header.mbc_type == CUPID_GB_MBC3 &&
                                        gb->header.has_timer && gb->rtc_register >= 8u))
            return (uint16_t)cupid_gb_effective_ram_bank(gb);
        return (uint16_t)cupid_gb_selected_ram_bank(gb);
    }
    if (address >= 0xd000u && address < 0xe000u)
        return gb->cgb_wram_bank;
    return 0;
}

uint8_t cupid_gb_cheats_apply(const CupidGb *gb, uint16_t address, uint8_t value) {
    if (!gb)
        return value;
    const CupidGbCheats *cheats = gb->cheats;
    if (!cheats || !cheats->enabled || !cheats->records.count || gb->boot_rom_enabled)
        return value;
    const CheatList *bucket = &cheats->addresses[address % ADDRESS_BUCKETS];
    for (size_t index = 0; index < bucket->count; ++index) {
        const CupidGbCheat *record = bucket->items[index];
        if (record->address == address && record->enabled &&
            (!record->use_compare || record->compare_value == value) &&
            (record->bank == CUPID_GB_CHEAT_ANY_BANK || record->bank == selected_bank(gb, address)))
            return record->value;
    }
    return value;
}

static uint32_t read_integer(const uint8_t *bytes, unsigned count, bool big_endian) {
    uint32_t value = 0;
    for (unsigned index = 0; index < count; ++index) {
        const unsigned position = big_endian ? count - 1u - index : index;
        value |= (uint32_t)bytes[index] << (position * 8u);
    }
    return value;
}

size_t cupid_gb_cheats_encoded_size(const CupidGbCheats *cheats) {
    if (!cheats || cheats->records.count > (SIZE_MAX - FILE_HEADER_BYTES) / FILE_RECORD_BYTES)
        return 0;
    return FILE_HEADER_BYTES + cheats->records.count * FILE_RECORD_BYTES;
}

CupidGbCheatResult cupid_gb_cheats_encode(const CupidGbCheats *cheats, uint8_t *data,
                                          size_t capacity, size_t *written) {
    if (written)
        *written = 0;
    const size_t size = cupid_gb_cheats_encoded_size(cheats);
    if (!size || !data)
        return CUPID_GB_CHEAT_INVALID_ARGUMENT;
    if (capacity < size)
        return CUPID_GB_CHEAT_BUFFER_TOO_SMALL;
    const uint8_t header[FILE_HEADER_BYTES] = {0x68, 0x43, 0x42, 0x53, FILE_RECORD_BYTES, 0, 0, 0};
    memcpy(data, header, sizeof(header));
    for (size_t index = 0; index < cheats->records.count; ++index) {
        const CupidGbCheat *record = cheats->records.items[index];
        uint8_t *output = data + FILE_HEADER_BYTES + index * FILE_RECORD_BYTES;
        output[0] = (uint8_t)record->address;
        output[1] = (uint8_t)(record->address >> 8);
        output[2] = (uint8_t)record->bank;
        output[3] = (uint8_t)(record->bank >> 8);
        output[4] = record->value;
        output[5] = record->compare_value;
        output[6] = record->use_compare;
        output[7] = record->enabled;
        memcpy(output + 8, record->description, CUPID_GB_CHEAT_DESCRIPTION_SIZE);
    }
    if (written)
        *written = size;
    return CUPID_GB_CHEAT_OK;
}

CupidGbCheatResult cupid_gb_cheats_decode(CupidGbCheats *cheats, const uint8_t *data, size_t size,
                                          bool replace_existing) {
    if (!cheats || !data)
        return CUPID_GB_CHEAT_INVALID_ARGUMENT;
    const uint8_t little_magic[4] = {0x68, 0x43, 0x42, 0x53};
    const uint8_t big_magic[4] = {0x53, 0x42, 0x43, 0x68};
    if (size < FILE_HEADER_BYTES || (size - FILE_HEADER_BYTES) % FILE_RECORD_BYTES)
        return CUPID_GB_CHEAT_INVALID_DATA;
    const bool big_endian = memcmp(data, big_magic, sizeof(big_magic)) == 0;
    if ((!big_endian && memcmp(data, little_magic, sizeof(little_magic))) ||
        read_integer(data + 4, 4, big_endian) != FILE_RECORD_BYTES)
        return CUPID_GB_CHEAT_INVALID_DATA;
    CupidGbCheats *candidate =
        replace_existing ? cupid_gb_cheats_create() : cupid_gb_cheats_clone(cheats);
    if (!candidate)
        return CUPID_GB_CHEAT_OUT_OF_MEMORY;
    CupidGbCheatResult result = CUPID_GB_CHEAT_OK;
    const size_t count = (size - FILE_HEADER_BYTES) / FILE_RECORD_BYTES;
    for (size_t index = 0; index < count && result == CUPID_GB_CHEAT_OK; ++index) {
        const uint8_t *input = data + FILE_HEADER_BYTES + index * FILE_RECORD_BYTES;
        if (input[6] > 1u || input[7] > 1u) {
            result = CUPID_GB_CHEAT_INVALID_DATA;
            break;
        }
        CupidGbCheat record = {.address = (uint16_t)read_integer(input, 2, big_endian),
                               .bank = (uint16_t)read_integer(input + 2, 2, big_endian),
                               .value = input[4],
                               .compare_value = input[5],
                               .use_compare = input[6] != 0,
                               .enabled = input[7] != 0};
        memcpy(record.description, input + 8, sizeof(record.description));
        result = cupid_gb_cheats_add(candidate, &record);
    }
    if (result == CUPID_GB_CHEAT_OK) {
        CupidGbCheats previous = *cheats;
        candidate->enabled = cheats->enabled;
        *cheats = *candidate;
        *candidate = previous;
    }
    cupid_gb_cheats_destroy(candidate);
    return result;
}
