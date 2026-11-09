#include "Doctors.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <utility>

static_assert(sizeof(Doctor) == DOC_SIZE, "Doctor must occupy 95 bytes");
static_assert(sizeof(int) == 4, "The on-disk format expects 4-byte integer offsets");

namespace {
// Explicit byte serialization avoids C++ structure-padding surprises.
constexpr int PRIMARY_BYTES = 15 + sizeof(int);  // ID + data offset = 19
constexpr int NODE_BYTES = 15 + sizeof(int);     // Doctor ID + next = 19

std::string trim(const char* raw, int width) {
    int n = width;
    while (n > 0 && raw[n - 1] == ' ') --n;
    return std::string(raw, n);
}

// Open existing binary files for arbitrary-position reads AND writes.
bool openRW(std::fstream& f, const std::string& path) {
    f.open(path, std::ios::binary | std::ios::in | std::ios::out);
    return static_cast<bool>(f);
}

// All index files consist of fixed-width records, without headers.
int entries(std::fstream& f, int bytesPerEntry) {
    f.clear();
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0 || size % bytesPerEntry != 0 ||
        size / bytesPerEntry > std::numeric_limits<int>::max()) return -1;
    return static_cast<int>(size / bytesPerEntry);
}

bool getEntry(std::fstream& f, int position, int bytesPerEntry,
              std::string& key, int& value, int keyWidth) {
    if (position < 0 || bytesPerEntry != keyWidth + static_cast<int>(sizeof(int))) return false;
    char raw[30];
    if (keyWidth > 30) return false;
    f.clear();
    f.seekg(static_cast<std::streamoff>(position) * bytesPerEntry, std::ios::beg);
    if (!f.read(raw, keyWidth)) return false;
    if (!f.read(reinterpret_cast<char*>(&value), sizeof(value))) return false;
    key = trim(raw, keyWidth);
    return true;
}

bool putEntry(std::fstream& f, int position, int bytesPerEntry,
              const std::string& key, int value, int keyWidth) {
    if (position < 0 || keyWidth > 30 || key.size() > static_cast<std::size_t>(keyWidth) ||
        bytesPerEntry != keyWidth + static_cast<int>(sizeof(int))) return false;
    char raw[30];
    setField(raw, keyWidth, key.c_str());
    f.clear();
    f.seekp(static_cast<std::streamoff>(position) * bytesPerEntry, std::ios::beg);
    f.write(raw, keyWidth);
    f.write(reinterpret_cast<const char*>(&value), sizeof(value));
    f.flush();
    return static_cast<bool>(f);
}

// Real DISK binary search: never load the index into a vector.
// Returns existing entry position, or -1; insertion position is separately reported.
int diskSearch(std::fstream& f, const std::string& wanted,
               int keyWidth, int& insertionPosition) {
    const int width = keyWidth + static_cast<int>(sizeof(int));
    int low = 0, high = entries(f, width);
    if (high < 0) { insertionPosition = -1; return -1; }
    while (low < high) {
        const int mid = low + (high - low) / 2;
        std::string key; int val;
        if (!getEntry(f, mid, width, key, val, keyWidth)) {
            insertionPosition = -1; return -1;
        }
        if (key < wanted) low = mid + 1;
        else high = mid;
    }
    insertionPosition = low;
    const int n = entries(f, width);
    if (low < n) {
        std::string key; int val;
        if (getEntry(f, low, width, key, val, keyWidth) && key == wanted) return low;
    }
    return -1;
}

// In-place sorted insertion ON DISK: shift existing fixed-length entries right.
bool diskInsert(std::fstream& f, int position, const std::string& key,
                int value, int keyWidth) {
    const int width = keyWidth + static_cast<int>(sizeof(int));
    const int n = entries(f, width);
    if (n < 0 || position < 0 || position > n) return false;
    for (int i = n; i > position; --i) {
        std::string oldKey; int oldValue;
        if (!getEntry(f, i - 1, width, oldKey, oldValue, keyWidth) ||
            !putEntry(f, i, width, oldKey, oldValue, keyWidth)) return false;
    }
    return putEntry(f, position, width, key, value, keyWidth);
}

// Read/write a secondary linked-list node in its DIFFERENT disk file.
bool readNode(std::fstream& f, int offset, std::string& doctorId, int& next) {
    if (offset < 0 || offset % NODE_BYTES != 0) return false;
    const int n = entries(f, NODE_BYTES);
    if (n < 0 || offset / NODE_BYTES >= n) return false;
    return getEntry(f, offset / NODE_BYTES, NODE_BYTES, doctorId, next, 15);
}

bool appendNode(std::fstream& f, const std::string& doctorId,
                int next, int& newOffset) {
    const int n = entries(f, NODE_BYTES);
    if (n < 0 || n > std::numeric_limits<int>::max() / NODE_BYTES - 1) return false;
    newOffset = n * NODE_BYTES;
    return putEntry(f, n, NODE_BYTES, doctorId, next, 15);
}

// Index key tables are sorted by diskInsert; nodes append to a separate disk file.
bool addSecondary(std::fstream& index, std::fstream& nodes,
                  const std::string& key, int keyWidth, const std::string& doctorId) {
    int insertion;
    const int oldPos = diskSearch(index, key, keyWidth, insertion);
    if (insertion < 0) return false;
    int oldHead = -1;
    if (oldPos != -1) {
        std::string found;
        if (!getEntry(index, oldPos, keyWidth + 4, found, oldHead, keyWidth)) return false;
    }
    int newHead;
    if (!appendNode(nodes, doctorId, oldHead, newHead)) return false;
    if (oldPos != -1) return putEntry(index, oldPos, keyWidth + 4, key, newHead, keyWidth);
    return diskInsert(index, insertion, key, newHead, keyWidth);
}

bool truncateFile(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out);
}
} // namespace

DoctorsManager::DoctorsManager(std::string path)
    : dataPath_(std::move(path)),
      primaryPath_(dataPath_ + ".primary.idx"),
      nameIndexPath_(dataPath_ + ".name.idx"),
      specialtyIndexPath_(dataPath_ + ".specialty.idx"),
      nameNodesPath_(dataPath_ + ".name_nodes.bin"),
      specialtyNodesPath_(dataPath_ + ".specialty_nodes.bin") {}

std::string DoctorsManager::field(const char* raw, int width) { return trim(raw, width); }

bool DoctorsManager::validText(const std::string& value, std::size_t limit, bool id) {
    if (value.empty() || value.size() > limit || value.front() == '*' ||
        value.front() == ' ' || value.back() == ' ' ||
        value.find('\0') != std::string::npos) return false;
    if (id && value.find_first_of("\r\n\t") != std::string::npos) return false;
    return true;
}

bool DoctorsManager::initialize(bool seedFive) {
    createFile(dataPath_.c_str());
    std::fstream f;
    if (!openRW(f, dataPath_)) return false;
    const int size = getFileEnd(f);
    if (size < HEADER_SIZE || (size - HEADER_SIZE) % DOC_SIZE != 0) return false;
    if (seedFive && size == HEADER_SIZE) {
        const Doctor samples[] = {
            makeDoctor("D001", "Ahmed", "Cardiology", "Cairo"),
            makeDoctor("D002", "Sara", "Neurology", "Giza"),
            makeDoctor("D003", "Omar", "Cardiology", "Alexandria"),
            makeDoctor("D004", "Mona", "Dermatology", "Cairo"),
            makeDoctor("D005", "Ali", "Neurology", "Giza")
        };
        for (const Doctor& d : samples)
            if (addRecord(f, reinterpret_cast<const char*>(&d), DOC_SIZE) < 0 || !f)
                return false;
    }
    f.close();
    // Reuse valid persistent disk indexes across restarts; rebuild only if missing or stale.
    if (validateIndexes()) return true;
    return rebuildIndexes();
}

bool DoctorsManager::readAt(int offset, Doctor& result) const {
    std::fstream f(dataPath_, std::ios::binary | std::ios::in);
    if (!f) return false;
    char raw[DOC_SIZE];
    if (!readRecord(f, offset, raw, DOC_SIZE) || !f || raw[0] == '*') return false;
    std::memcpy(&result, raw, DOC_SIZE);
    return true;
}

bool DoctorsManager::writeAt(int offset, const Doctor& d) {
    std::fstream f;
    if (!openRW(f, dataPath_) || isDeleted(f, offset, DOC_SIZE)) return false;
    return writeRecord(f, offset, reinterpret_cast<const char*>(&d), DOC_SIZE) && static_cast<bool>(f);
}

// Reconstruct *disk* indexes using on-disk insertion sorting, no RAM vectors.
bool DoctorsManager::rebuildIndexes() {
    if (!truncateFile(primaryPath_) || !truncateFile(nameIndexPath_) ||
        !truncateFile(specialtyIndexPath_) || !truncateFile(nameNodesPath_) ||
        !truncateFile(specialtyNodesPath_)) return false;

    std::fstream data, primary, nameIndex, specialtyIndex, nameNodes, specialtyNodes;
    if (!openRW(data, dataPath_) || !openRW(primary, primaryPath_) ||
        !openRW(nameIndex, nameIndexPath_) || !openRW(specialtyIndex, specialtyIndexPath_) ||
        !openRW(nameNodes, nameNodesPath_) || !openRW(specialtyNodes, specialtyNodesPath_)) return false;

    const int size = getFileEnd(data);
    if (size < HEADER_SIZE || (size - HEADER_SIZE) % DOC_SIZE != 0) return false;
    for (int offset = HEADER_SIZE; offset + DOC_SIZE <= size; offset += DOC_SIZE) {
        char raw[DOC_SIZE];
        if (!readRecord(data, offset, raw, DOC_SIZE) || !data) return false;
        if (raw[0] == '*') continue;
        Doctor d; std::memcpy(&d, raw, DOC_SIZE);
        const std::string id = field(d.id, 15);
        const std::string name = field(d.name, 30);
        const std::string specialty = field(d.specialty, 20);
        if (!validText(id, 15, true) || !validText(name, 30) || !validText(specialty, 20)) return false;
        int insertion;
        if (diskSearch(primary, id, 15, insertion) != -1 || insertion < 0) return false;
        if (!diskInsert(primary, insertion, id, offset, 15) ||
            !addSecondary(nameIndex, nameNodes, name, 30, id) ||
            !addSecondary(specialtyIndex, specialtyNodes, specialty, 20, id)) return false;
    }
    return true;
}

int DoctorsManager::recordOffset(const std::string& id) const {
    std::fstream f(primaryPath_, std::ios::in | std::ios::binary);
    if (!f) return -1;
    int insertion;
    const int pos = diskSearch(f, id, 15, insertion);
    if (pos < 0) return -1;
    std::string stored; int offset;
    return getEntry(f, pos, PRIMARY_BYTES, stored, offset, 15) ? offset : -1;
}

bool DoctorsManager::findDoctorByID(const std::string& id, Doctor& result) const {
    const int offset = recordOffset(id);
    return offset >= 0 && readAt(offset, result);
}

std::vector<Doctor> DoctorsManager::findSecondary(const std::string& key,
    const std::string& indexPath, const std::string& nodesPath, int keyWidth) const {
    std::vector<Doctor> result;
    std::fstream index(indexPath, std::ios::in | std::ios::binary);
    std::fstream nodes(nodesPath, std::ios::in | std::ios::binary);
    if (!index || !nodes) return result;
    int insertion;
    const int position = diskSearch(index, key, keyWidth, insertion);
    if (position < 0) return result;
    std::string found; int node;
    if (!getEntry(index, position, keyWidth + 4, found, node, keyWidth)) return result;
    std::set<int> visited; // safety guard against corrupt linked-list cycles
    while (node != -1 && visited.insert(node).second) {
        std::string id; int next;
        if (!readNode(nodes, node, id, next)) break;
        Doctor d;
        if (findDoctorByID(id, d)) result.push_back(d);
        node = next;
    }
    std::sort(result.begin(), result.end(), [](const Doctor& a, const Doctor& b) {
        return trim(a.id, 15) < trim(b.id, 15);
    }); // only sort returned results (NOT index tables)
    return result;
}

std::vector<Doctor> DoctorsManager::findDoctorsByName(const std::string& name) const {
    return findSecondary(name, nameIndexPath_, nameNodesPath_, 30);
}
std::vector<Doctor> DoctorsManager::findDoctorsBySpecialty(const std::string& specialty) const {
    return findSecondary(specialty, specialtyIndexPath_, specialtyNodesPath_, 20);
}

std::size_t DoctorsManager::count() const {
    std::fstream f(primaryPath_, std::ios::binary | std::ios::in);
    if (!f) return 0;
    int n = entries(f, PRIMARY_BYTES);
    return n < 0 ? 0 : static_cast<std::size_t>(n);
}

std::vector<Doctor> DoctorsManager::listAllDoctors() const {
    std::vector<Doctor> result;
    std::fstream f(primaryPath_, std::ios::binary | std::ios::in);
    if (!f) return result;
    const int n = entries(f, PRIMARY_BYTES);
    for (int i = 0; i < n; ++i) {
        std::string id; int offset; Doctor d;
        if (getEntry(f, i, PRIMARY_BYTES, id, offset, 15) && readAt(offset, d))
            result.push_back(d);
    }
    return result;
}

// Appointments path is the team's current convention; coordinate with Member 3.
bool DoctorsManager::referencedByAppointment(const std::string& doctorId) const {
    std::ifstream in("Appointments.dat", std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff len = in.tellg();
    if (len < HEADER_SIZE || (len - HEADER_SIZE) % APP_SIZE != 0) return true;
    for (int off = HEADER_SIZE; off + APP_SIZE <= len; off += APP_SIZE) {
        char raw[APP_SIZE];
        in.clear(); in.seekg(off);
        if (!in.read(raw, APP_SIZE)) return true;
        if (raw[0] != '*' && field(raw + 45, 15) == doctorId) return true;
    }
    return false;
}

bool DoctorsManager::addDoctor(const std::string& id, const std::string& name,
    const std::string& specialty, const std::string& address) {
    if (!validText(id, 15, true) || !validText(name, 30) ||
        !validText(specialty, 20) || !validText(address, 30) || recordOffset(id) != -1) return false;
    Doctor d = makeDoctor(id.c_str(), name.c_str(), specialty.c_str(), address.c_str());
    std::fstream f;
    if (!openRW(f, dataPath_)) return false;
    const int offset = addRecord(f, reinterpret_cast<const char*>(&d), DOC_SIZE);
    if (offset < 0 || !f) return false;
    f.close();
    return rebuildIndexes();
}

bool DoctorsManager::deleteDoctor(const std::string& id) {
    const int offset = recordOffset(id);
    if (offset == -1 || referencedByAppointment(id)) return false;
    std::fstream f;
    if (!openRW(f, dataPath_) || !deleteRecord(f, offset, DOC_SIZE)) return false;
    f.close();
    return rebuildIndexes();
}

bool DoctorsManager::updateDoctorName(const std::string& id, const std::string& name) {
    if (!validText(name, 30)) return false;
    const int offset = recordOffset(id); Doctor d;
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.name, 30, name.c_str());
    return writeAt(offset, d) && rebuildIndexes();
}

bool DoctorsManager::updateDoctorSpecialty(const std::string& id, const std::string& specialty) {
    if (!validText(specialty, 20)) return false;
    const int offset = recordOffset(id); Doctor d;
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.specialty, 20, specialty.c_str());
    return writeAt(offset, d) && rebuildIndexes();
}

bool DoctorsManager::updateDoctorAddress(const std::string& id, const std::string& address) {
    if (!validText(address, 30)) return false;
    const int offset = recordOffset(id); Doctor d;
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.address, 30, address.c_str());
    return writeAt(offset, d); // Address isn't indexed: no index re-sort needed
}

bool DoctorsManager::updateDoctorID(const std::string& oldId, const std::string& newId) {
    if (!validText(newId, 15, true) || recordOffset(newId) != -1 ||
        referencedByAppointment(oldId)) return false;
    const int offset = recordOffset(oldId); Doctor d;
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.id, 15, newId.c_str());
    return writeAt(offset, d) && rebuildIndexes(); // physical shifting via diskInsert
}

// Compare on-disk entries with data records and all secondary-key chains.
bool DoctorsManager::validateIndexes() const {
    std::fstream primary(primaryPath_, std::ios::in | std::ios::binary);
    std::fstream names(nameIndexPath_, std::ios::in | std::ios::binary);
    std::fstream specialties(specialtyIndexPath_, std::ios::in | std::ios::binary);
    if (!primary || !names || !specialties) return false;
    int n = entries(primary, PRIMARY_BYTES);
    if (n < 0) return false;
    std::string prev;
    for (int i = 0; i < n; ++i) {
        std::string id; int offset; Doctor d;
        if (!getEntry(primary, i, PRIMARY_BYTES, id, offset, 15) ||
            (i && prev >= id) || !readAt(offset, d) || field(d.id, 15) != id ||
            recordOffset(id) != offset) return false;
        const std::string nameKey = field(d.name, 30);
        const std::string specKey = field(d.specialty, 20);
        int insertion;
        if (diskSearch(names, nameKey, 30, insertion) < 0 ||
            diskSearch(specialties, specKey, 20, insertion) < 0) return false;
        prev = id;
    }
    const std::pair<std::fstream*, int> secondaryFiles[2] = {
        {&names, 30}, {&specialties, 20}
    };
    for (const auto& spec : secondaryFiles) {
        auto& f = *spec.first;
        const int width = spec.second;
        const int total = entries(f, width + 4);
        if (total < 0) return false;
        std::string prevKey;
        for (int i = 0; i < total; ++i) {
            std::string key; int head;
            if (!getEntry(f, i, width + 4, key, head, width) ||
                (i && prevKey >= key)) return false;
            prevKey = key;
            const auto actual = findSecondary(key, width == 30 ? nameIndexPath_ : specialtyIndexPath_,
                                              width == 30 ? nameNodesPath_ : specialtyNodesPath_, width);
            std::set<std::string> actualIDs, expectedIDs;
            for (const Doctor& d : actual) actualIDs.insert(field(d.id, 15));
            for (int j = 0; j < n; ++j) {
                std::string id; int offset; Doctor d;
                if (!getEntry(primary, j, PRIMARY_BYTES, id, offset, 15) || !readAt(offset, d)) return false;
                if ((width == 30 ? field(d.name, 30) : field(d.specialty, 20)) == key)
                    expectedIDs.insert(id);
            }
            if (expectedIDs.empty() || actualIDs != expectedIDs ||
                static_cast<int>(actual.size()) != static_cast<int>(expectedIDs.size())) return false;
        }
    }
    return true;
}

void printDoctorDetails(const Doctor& d) {
    char id[16], name[31], specialty[21], address[31];
    getField(d.id, 15, id);
    getField(d.name, 30, name);
    getField(d.specialty, 20, specialty);
    getField(d.address, 30, address);
    std::cout << id << " | " << name << " | " << specialty << " | " << address << '\n';
}
