#include "Doctors.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

static_assert(sizeof(Doctor) == DOC_SIZE, "Doctor must be exactly 95 bytes");
static_assert(sizeof(int) == 4, "This assignment uses 4-byte integer offsets");
namespace {
constexpr int NODE_SIZE = 15 + sizeof(int); 
const char* const APPOINTMENTS_FILE = "Appointments.dat";

void writeNode(std::ofstream& out, const std::string& id, int next) {
    char padded[15];
    setField(padded, 15, id.c_str());
    out.write(padded, 15);
    out.write(reinterpret_cast<const char*>(&next), sizeof(int));
}

bool readNode(std::ifstream& in, int offset, std::string& id, int& next) {
    if (offset < 0 || offset % NODE_SIZE != 0) return false;
    in.clear();
    in.seekg(offset, std::ios::beg);
    char padded[15];
    if (!in.read(padded, 15)) return false;
    if (!in.read(reinterpret_cast<char*>(&next), sizeof(int))) return false;
    char buffer[16];
    getField(padded, 15, buffer);
    id = buffer;
    return true;
}
}

DoctorsManager::DoctorsManager(std::string path)
    : dataPath_(std::move(path)),
      nameNodesPath_(dataPath_ + ".name_nodes.bin"),
      specialtyNodesPath_(dataPath_ + ".specialty_nodes.bin") {}

std::string DoctorsManager::field(const char* data, int length) {
    std::string s(data, length);
    const auto end = s.find_last_not_of(' ');
    if (end == std::string::npos) return "";
    s.erase(end + 1);
    return s;
}

bool DoctorsManager::validText(const std::string& value, size_t limit, bool id) {
    if (value.empty() || value.size() > limit || value[0] == '*' ||
        value.front() == ' ' || value.back() == ' ') return false;
    if (value.find('\0') != std::string::npos) return false;
    if (id && value.find_first_of("\r\n\t") != std::string::npos) return false;
    return true;
}

int DoctorsManager::primaryPosition(const std::string& id) const {
    int low = 0, high = static_cast<int>(primary_.size()) - 1;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (primary_[middle].id == id) return middle;
        if (primary_[middle].id < id) low = middle + 1;
        else high = middle - 1;
    }
    return -1;
}

int DoctorsManager::secondaryPosition(const std::vector<SecondaryEntry>& index,
                                      const std::string& key) const {
    int low = 0, high = static_cast<int>(index.size()) - 1;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (index[middle].key == key) return middle;
        if (index[middle].key < key) low = middle + 1;
        else high = middle - 1;
    }
    return -1;
}

bool DoctorsManager::initialize(bool seedFive) {
    createFile(dataPath_.c_str());
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) return false;
    int length = getFileEnd(file);
    if (length < HEADER_SIZE || (length - HEADER_SIZE) % DOC_SIZE != 0) return false;
    file.close();
    if (!rebuildIndexes()) return false;
    if (seedFive && length == HEADER_SIZE) {
        const Doctor sample[] = {
            makeDoctor("D001", "Ahmed", "Cardiology", "Cairo"),
            makeDoctor("D002", "Sara", "Neurology", "Giza"),
            makeDoctor("D003", "Omar", "Cardiology", "Alexandria"),
            makeDoctor("D004", "Mona", "Dermatology", "Cairo"),
            makeDoctor("D005", "Ali", "Neurology", "Giza")
        };
        // Rebuild only once after initial bulk insert.
        std::fstream out(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
        if (!out) return false;
        for (const Doctor& d : sample) {
            if (addRecord(out, reinterpret_cast<const char*>(&d), DOC_SIZE) < 0 || !out) return false;
        }
        out.close();
        return rebuildIndexes();
    }
    return true;
}

bool DoctorsManager::readAt(int offset, Doctor& result) const {
    std::fstream file(dataPath_, std::ios::in | std::ios::binary);
    if (!file) return false;
    char raw[DOC_SIZE];
    if (!readRecord(file, offset, raw, DOC_SIZE) || !file || raw[0] == '*') return false;
    std::memcpy(&result, raw, DOC_SIZE);
    return true;
}

bool DoctorsManager::writeAt(int offset, const Doctor& doctor) {
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file || isDeleted(file, offset, DOC_SIZE)) return false;
    if (!writeRecord(file, offset, reinterpret_cast<const char*>(&doctor), DOC_SIZE)) return false;
    return static_cast<bool>(file);
}

bool DoctorsManager::rebuildIndexes() {
    std::fstream file(dataPath_, std::ios::in | std::ios::binary);
    if (!file) return false;
    int length = getFileEnd(file);
    if (length < HEADER_SIZE || (length - HEADER_SIZE) % DOC_SIZE != 0) return false;
    std::vector<PrimaryEntry> fresh;
    for (int offset = HEADER_SIZE; offset + DOC_SIZE <= length; offset += DOC_SIZE) {
        char raw[DOC_SIZE];
        if (!readRecord(file, offset, raw, DOC_SIZE) || !file) return false;
        if (raw[0] == '*') continue;
        Doctor d;
        std::memcpy(&d, raw, DOC_SIZE);
        fresh.push_back({field(d.id, 15), offset});
    }
    std::sort(fresh.begin(), fresh.end(), [](const PrimaryEntry& a, const PrimaryEntry& b) {
        return a.id < b.id;
    });
    for (size_t i = 1; i < fresh.size(); ++i) {
        if (fresh[i-1].id == fresh[i].id) return false; // duplicate corruption
    }
    primary_ = std::move(fresh);
    return buildSecondaryFiles();
}

bool DoctorsManager::writeSecondary(
    const std::string& path,
    const std::vector<std::pair<std::string,std::string>>& items,
    std::vector<SecondaryEntry>& index) {
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& item : items) groups[item.first].push_back(item.second);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    index.clear();
    int fileOffset = 0;
    for (auto& group : groups) {
        // Each chain is stored in its own range in the shared linked-list file.
        // Reverse insertion makes the chain traverse in ascending Doctor ID order.
        std::sort(group.second.begin(), group.second.end());
        int head = -1;
        for (auto it = group.second.rbegin(); it != group.second.rend(); ++it) {
            writeNode(out, *it, head);
            if (!out) return false;
            head = fileOffset;
            fileOffset += NODE_SIZE;
        }
        index.push_back({group.first, head});
    }
    out.flush();
    return static_cast<bool>(out);
}

bool DoctorsManager::buildSecondaryFiles() {
    std::vector<std::pair<std::string,std::string>> names, specialties;
    for (const auto& entry : primary_) {
        Doctor d;
        if (!readAt(entry.offset, d)) return false;
        names.emplace_back(field(d.name, 30), entry.id);
        specialties.emplace_back(field(d.specialty, 20), entry.id);
    }
    return writeSecondary(nameNodesPath_, names, byName_) &&
           writeSecondary(specialtyNodesPath_, specialties, bySpecialty_);
}

int DoctorsManager::recordOffset(const std::string& id) const {
    const int i = primaryPosition(id);
    return i == -1 ? -1 : primary_[i].offset;
}

bool DoctorsManager::findDoctorByID(const std::string& id, Doctor& result) const {
    int offset = recordOffset(id);
    return offset != -1 && readAt(offset, result);
}

std::vector<Doctor> DoctorsManager::findSecondary(
    const std::string& key, const std::vector<SecondaryEntry>& index,
    const std::string& nodePath) const {
    std::vector<Doctor> matches;
    int pos = secondaryPosition(index, key);
    if (pos == -1) return matches;
    std::ifstream in(nodePath, std::ios::binary);
    if (!in) return matches;
    in.seekg(0, std::ios::end);
    const std::streamoff length = in.tellg();
    int node = index[pos].head;
    std::set<int> visited;
    while (node != -1) {
        if (node < 0 || node + NODE_SIZE > length || !visited.insert(node).second) break;
        std::string id;
        int next;
        if (!readNode(in, node, id, next)) break;
        Doctor doctor;
        if (findDoctorByID(id, doctor)) matches.push_back(doctor);
        node = next;
    }
    return matches;
}

std::vector<Doctor> DoctorsManager::findDoctorsByName(const std::string& name) const {
    return findSecondary(name, byName_, nameNodesPath_);
}
std::vector<Doctor> DoctorsManager::findDoctorsBySpecialty(const std::string& spec) const {
    return findSecondary(spec, bySpecialty_, specialtyNodesPath_);
}
std::vector<Doctor> DoctorsManager::listAllDoctors() const {
    std::vector<Doctor> result;
    for (const auto& e : primary_) {
        Doctor d;
        if (readAt(e.offset, d)) result.push_back(d);
    }
    return result;
}
size_t DoctorsManager::count() const { return primary_.size(); }

bool DoctorsManager::addDoctor(const std::string& id, const std::string& name,
                               const std::string& specialty, const std::string& address) {
    if (!validText(id, 15, true) || !validText(name, 30) ||
        !validText(specialty, 20) || !validText(address, 30) || primaryPosition(id) != -1) return false;
    Doctor d = makeDoctor(id.c_str(), name.c_str(), specialty.c_str(), address.c_str());
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) return false;
    int offset = addRecord(file, reinterpret_cast<const char*>(&d), DOC_SIZE);
    if (offset < 0 || !file) return false;
    file.close();
    // Inserts in sorted order (vector::insert shifts later entries right).
    auto it = std::lower_bound(primary_.begin(), primary_.end(), id,
        [](const PrimaryEntry& a, const std::string& key) { return a.id < key; });
    primary_.insert(it, {id, offset});
    return buildSecondaryFiles();
}

bool DoctorsManager::referencedByAppointment(const std::string& doctorId) const {
    std::ifstream in(APPOINTMENTS_FILE, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff len = in.tellg();
    if (len < HEADER_SIZE || (len - HEADER_SIZE) % APP_SIZE != 0) return true; // fail safe
    for (int offset = HEADER_SIZE; offset + APP_SIZE <= len; offset += APP_SIZE) {
        char raw[APP_SIZE];
        in.seekg(offset);
        if (!in.read(raw, APP_SIZE)) return true;
        if (raw[0] == '*') continue;
        if (field(raw + 45, 15) == doctorId) return true;
    }
    return false;
}

bool DoctorsManager::deleteDoctor(const std::string& id) {
    int pos = primaryPosition(id);
    if (pos == -1 || referencedByAppointment(id)) return false;
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file || !deleteRecord(file, primary_[pos].offset, DOC_SIZE)) return false;
    file.close();
    primary_.erase(primary_.begin() + pos);
    return buildSecondaryFiles();
}

bool DoctorsManager::updateDoctorName(const std::string& id, const std::string& newName) {
    if (!validText(newName, 30)) return false;
    Doctor d;
    int offset = recordOffset(id);
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.name, 30, newName.c_str());
    return writeAt(offset, d) && buildSecondaryFiles();
}

bool DoctorsManager::updateDoctorSpecialty(const std::string& id, const std::string& newSpec) {
    if (!validText(newSpec, 20)) return false;
    Doctor d;
    int offset = recordOffset(id);
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.specialty, 20, newSpec.c_str());
    return writeAt(offset, d) && buildSecondaryFiles();
}

bool DoctorsManager::updateDoctorAddress(const std::string& id, const std::string& address) {
    if (!validText(address, 30)) return false;
    Doctor d;
    int offset = recordOffset(id);
    if (offset == -1 || !readAt(offset, d)) return false;
    setField(d.address, 30, address.c_str());
    return writeAt(offset, d); // address is not indexed
}

bool DoctorsManager::updateDoctorID(const std::string& oldId, const std::string& newId) {
    if (!validText(newId, 15, true) || primaryPosition(newId) != -1 ||
        referencedByAppointment(oldId)) return false;
    int pos = primaryPosition(oldId);
    if (pos == -1) return false;
    const int offset = primary_[pos].offset;
    Doctor d;
    if (!readAt(offset, d)) return false;
    setField(d.id, 15, newId.c_str());
    if (!writeAt(offset, d)) return false;

    primary_.erase(primary_.begin() + pos);
    auto it = std::lower_bound(primary_.begin(), primary_.end(), newId,
        [](const PrimaryEntry& a, const std::string& key) { return a.id < key; });
    primary_.insert(it, {newId, offset});
    return buildSecondaryFiles();
}

bool DoctorsManager::validateIndexes() const {
    for (size_t i = 1; i < primary_.size(); ++i) {
        if (primary_[i-1].id >= primary_[i].id) return false;
    }
    for (const auto& e : primary_) {
        Doctor d;
        if (primaryPosition(e.id) == -1 || !readAt(e.offset, d) || field(d.id, 15) != e.id)
            return false;
    }
    for (const auto& e : byName_) {
        std::set<std::string> actual;
        for (const Doctor& d : findDoctorsByName(e.key)) actual.insert(field(d.id, 15));
        std::set<std::string> expected;
        for (const auto& p : primary_) {
            Doctor d;
            if (!readAt(p.offset, d)) return false;
            if (field(d.name, 30) == e.key) expected.insert(p.id);
        }
        if (actual != expected) return false;
    }
    for (const auto& e : bySpecialty_) {
        std::set<std::string> actual;
        for (const Doctor& d : findDoctorsBySpecialty(e.key)) actual.insert(field(d.id, 15));
        std::set<std::string> expected;
        for (const auto& p : primary_) {
            Doctor d;
            if (!readAt(p.offset, d)) return false;
            if (field(d.specialty, 20) == e.key) expected.insert(p.id);
        }
        if (actual != expected) return false;
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
