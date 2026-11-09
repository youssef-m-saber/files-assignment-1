#include "Appointments.h"

#include <cstring>
#include <fstream>
#include <iostream>

static_assert(sizeof(Appointment) == APP_SIZE, "Appointment must be exactly 60 bytes");
static_assert(sizeof(int) == 4, "This assignment uses 4-byte integer offsets");

namespace {
const int NODE_SIZE = 15 + sizeof(int); // Appointment ID + next node offset

bool writeNode(std::ofstream& file, const std::string& appointmentId, int next) {
    char id[15];
    setField(id, 15, appointmentId.c_str());
    file.write(id, 15);
    file.write(reinterpret_cast<const char*>(&next), sizeof(int));
    return static_cast<bool>(file);
}

bool readNode(std::ifstream& file, int offset, std::string& appointmentId, int& next) {
    if (offset < 0 || offset % NODE_SIZE != 0) return false;

    char id[15];
    file.clear();
    file.seekg(offset, std::ios::beg);
    if (!file.read(id, 15)) return false;
    if (!file.read(reinterpret_cast<char*>(&next), sizeof(int))) return false;

    char text[16];
    getField(id, 15, text);
    appointmentId = text;
    return true;
}
}

AppointmentsManager::AppointmentsManager(DoctorsManager& doctors, const std::string& dataPath)
    : doctors_(doctors),
      dataPath_(dataPath),
      doctorNodesPath_(dataPath + ".doctor_nodes.bin") {}

std::string AppointmentsManager::field(const char* data, int length) {
    int last = length - 1;
    while (last >= 0 && data[last] == ' ') last--;
    if (last < 0) return "";
    return std::string(data, last + 1);
}

bool AppointmentsManager::validText(const std::string& value, size_t limit, bool isId) {
    if (value.empty() || value.size() > limit || value[0] == '*' ||
        value[0] == ' ' || value[value.size() - 1] == ' ') return false;

    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '\0') return false;
        if (isId && (value[i] == '\r' || value[i] == '\n' || value[i] == '\t')) return false;
    }
    return true;
}

int AppointmentsManager::primaryPosition(const std::string& id) const {
    int low = 0;
    int high = static_cast<int>(primary_.size()) - 1;

    // Binary search is required for the sorted Appointment ID primary index.
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (primary_[middle].id == id) return middle;
        if (primary_[middle].id < id) low = middle + 1;
        else high = middle - 1;
    }
    return -1;
}

int AppointmentsManager::primaryInsertPosition(const std::string& id) const {
    int low = 0;
    int high = static_cast<int>(primary_.size());

    while (low < high) {
        int middle = low + (high - low) / 2;
        if (primary_[middle].id < id) low = middle + 1;
        else high = middle;
    }
    return low;
}

int AppointmentsManager::secondaryPosition(const std::string& doctorId) const {
    int low = 0;
    int high = static_cast<int>(byDoctorId_.size()) - 1;

    // The Doctor ID secondary index is also kept sorted for binary search.
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (byDoctorId_[middle].doctorId == doctorId) return middle;
        if (byDoctorId_[middle].doctorId < doctorId) low = middle + 1;
        else high = middle - 1;
    }
    return -1;
}

int AppointmentsManager::secondaryInsertPosition(const std::string& doctorId) const {
    int low = 0;
    int high = static_cast<int>(byDoctorId_.size());

    while (low < high) {
        int middle = low + (high - low) / 2;
        if (byDoctorId_[middle].doctorId < doctorId) low = middle + 1;
        else high = middle;
    }
    return low;
}

bool AppointmentsManager::doctorExists(const std::string& doctorId) const {
    Doctor doctor;
    // Use Person 2's public primary-index lookup for the foreign-key check.
    return doctors_.findDoctorByID(doctorId, doctor);
}

bool AppointmentsManager::initialize(bool seedTen) {
    createFile(dataPath_.c_str());
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) return false;

    int length = getFileEnd(file);
    if (length < HEADER_SIZE || (length - HEADER_SIZE) % APP_SIZE != 0) return false;
    file.close();

    if (!rebuildIndexes()) return false;
    if (!seedTen || length != HEADER_SIZE) return true;

    const char* ids[10] = {"A001", "A002", "A003", "A004", "A005",
                           "A006", "A007", "A008", "A009", "A010"};
    const char* dates[10] = {"2026-11-15 09:00", "2026-11-15 10:00",
                             "2026-11-16 09:30", "2026-11-16 11:00",
                             "2026-11-17 08:30", "2026-11-17 12:00",
                             "2026-11-18 10:30", "2026-11-18 13:00",
                             "2026-11-19 09:00", "2026-11-19 14:00"};
    const char* doctorIds[10] = {"D001", "D002", "D003", "D004", "D005",
                                 "D001", "D002", "D003", "D004", "D005"};

    for (int i = 0; i < 10; i++) {
        if (!addAppointment(ids[i], dates[i], doctorIds[i])) return false;
    }
    return true;
}

bool AppointmentsManager::readAt(int offset, Appointment& result) const {
    std::fstream file(dataPath_, std::ios::in | std::ios::binary);
    if (!file) return false;

    char record[APP_SIZE];
    if (!readRecord(file, offset, record, APP_SIZE) || !file || record[0] == '*') return false;
    std::memcpy(&result, record, APP_SIZE);
    return true;
}

bool AppointmentsManager::writeAt(int offset, const Appointment& appointment) {
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file || isDeleted(file, offset, APP_SIZE)) return false;

    return writeRecord(file, offset, reinterpret_cast<const char*>(&appointment), APP_SIZE);
}

bool AppointmentsManager::rebuildIndexes() {
    std::fstream file(dataPath_, std::ios::in | std::ios::binary);
    if (!file) return false;

    int length = getFileEnd(file);
    if (length < HEADER_SIZE || (length - HEADER_SIZE) % APP_SIZE != 0) return false;

    primary_.clear();
    for (int offset = HEADER_SIZE; offset + APP_SIZE <= length; offset += APP_SIZE) {
        char record[APP_SIZE];
        if (!readRecord(file, offset, record, APP_SIZE) || !file) return false;
        if (record[0] == '*') continue;

        Appointment appointment;
        std::memcpy(&appointment, record, APP_SIZE);
        std::string id = field(appointment.id, 15);
        int position = primaryInsertPosition(id);
        if (position < static_cast<int>(primary_.size()) && primary_[position].id == id) return false;

        PrimaryEntry entry;
        entry.id = id;
        entry.offset = offset;
        primary_.insert(primary_.begin() + position, entry);
    }
    return buildDoctorSecondaryFile();
}

bool AppointmentsManager::buildDoctorSecondaryFile() {
    byDoctorId_.clear();

    // First build the sorted Doctor ID index in memory.
    for (int i = 0; i < static_cast<int>(primary_.size()); i++) {
        Appointment appointment;
        if (!readAt(primary_[i].offset, appointment)) return false;

        std::string doctorId = field(appointment.doctorId, 15);
        if (secondaryPosition(doctorId) == -1) {
            SecondaryEntry entry;
            entry.doctorId = doctorId;
            entry.head = -1;
            int position = secondaryInsertPosition(doctorId);
            byDoctorId_.insert(byDoctorId_.begin() + position, entry);
        }
    }

    std::ofstream nodes(doctorNodesPath_, std::ios::binary | std::ios::trunc);
    if (!nodes) return false;

    int nodeOffset = 0;
    for (int i = 0; i < static_cast<int>(byDoctorId_.size()); i++) {
        int head = -1;

        // Primary index is ascending. Add from the end so the final chain is ascending too.
        for (int j = static_cast<int>(primary_.size()) - 1; j >= 0; j--) {
            Appointment appointment;
            if (!readAt(primary_[j].offset, appointment)) return false;
            if (field(appointment.doctorId, 15) != byDoctorId_[i].doctorId) continue;

            // A node stores the Appointment ID, never a data-file byte offset.
            if (!writeNode(nodes, primary_[j].id, head)) return false;
            head = nodeOffset;
            nodeOffset += NODE_SIZE;
        }
        byDoctorId_[i].head = head;
    }

    nodes.flush();
    return static_cast<bool>(nodes);
}

int AppointmentsManager::recordOffset(const std::string& id) const {
    int position = primaryPosition(id);
    if (position == -1) return -1;
    return primary_[position].offset;
}

bool AppointmentsManager::findAppointmentByID(const std::string& id, Appointment& result) const {
    int offset = recordOffset(id);
    return offset != -1 && readAt(offset, result);
}

std::vector<Appointment> AppointmentsManager::findSecondary(const std::string& doctorId) const {
    std::vector<Appointment> matches;
    int position = secondaryPosition(doctorId);
    if (position == -1) return matches;

    std::ifstream nodes(doctorNodesPath_, std::ios::binary);
    if (!nodes) return matches;
    nodes.seekg(0, std::ios::end);
    int fileLength = static_cast<int>(nodes.tellg());

    int node = byDoctorId_[position].head;
    int steps = 0;
    while (node != -1 && steps < static_cast<int>(primary_.size())) {
        if (node < 0 || node + NODE_SIZE > fileLength) break;

        std::string appointmentId;
        int next;
        if (!readNode(nodes, node, appointmentId, next)) break;

        Appointment appointment;
        if (findAppointmentByID(appointmentId, appointment)) matches.push_back(appointment);
        node = next;
        steps++;
    }
    return matches;
}

std::vector<Appointment> AppointmentsManager::findAppointmentsByDoctorID(
    const std::string& doctorId) const {
    return findSecondary(doctorId);
}

std::vector<Appointment> AppointmentsManager::listAllAppointments() const {
    std::vector<Appointment> appointments;
    for (int i = 0; i < static_cast<int>(primary_.size()); i++) {
        Appointment appointment;
        if (readAt(primary_[i].offset, appointment)) appointments.push_back(appointment);
    }
    return appointments;
}

size_t AppointmentsManager::count() const {
    return primary_.size();
}

bool AppointmentsManager::addAppointment(const std::string& id, const std::string& date,
                                         const std::string& doctorId) {
    if (!validText(id, 15, true) || !validText(date, 30) ||
        !validText(doctorId, 15, true) || primaryPosition(id) != -1) return false;
    if (!doctorExists(doctorId)) return false;

    Appointment appointment = makeAppointment(id.c_str(), date.c_str(), doctorId.c_str());
    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) return false;

    // addRecord reads the header and reuses the first AVAIL LIST record when one exists.
    int offset = addRecord(file, reinterpret_cast<const char*>(&appointment), APP_SIZE);
    if (offset < 0 || !file) return false;
    file.close();

    PrimaryEntry entry;
    entry.id = id;
    entry.offset = offset;
    int position = primaryInsertPosition(id);
    primary_.insert(primary_.begin() + position, entry);
    return buildDoctorSecondaryFile();
}

bool AppointmentsManager::deleteAppointment(const std::string& id) {
    int position = primaryPosition(id);
    if (position == -1) return false;

    std::fstream file(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file || !deleteRecord(file, primary_[position].offset, APP_SIZE)) return false;
    file.close();

    // deleteRecord writes '*' and links this slot into the AVAIL LIST header.
    primary_.erase(primary_.begin() + position);
    return buildDoctorSecondaryFile();
}

bool AppointmentsManager::updateAppointmentDate(const std::string& id,
                                                const std::string& newDate) {
    if (!validText(newDate, 30)) return false;

    int offset = recordOffset(id);
    Appointment appointment;
    if (offset == -1 || !readAt(offset, appointment)) return false;

    setField(appointment.date, 30, newDate.c_str());
    // Date is not indexed, so the two indexes do not change.
    return writeAt(offset, appointment);
}

bool AppointmentsManager::validateIndexes() const {
    for (int i = 1; i < static_cast<int>(primary_.size()); i++) {
        if (primary_[i - 1].id >= primary_[i].id) return false;
    }

    for (int i = 0; i < static_cast<int>(primary_.size()); i++) {
        Appointment appointment;
        if (!readAt(primary_[i].offset, appointment)) return false;
        if (field(appointment.id, 15) != primary_[i].id) return false;
        if (secondaryPosition(field(appointment.doctorId, 15)) == -1) return false;
    }

    for (int i = 0; i < static_cast<int>(byDoctorId_.size()); i++) {
        std::vector<Appointment> matches = findSecondary(byDoctorId_[i].doctorId);
        int expected = 0;

        for (int j = 0; j < static_cast<int>(primary_.size()); j++) {
            Appointment appointment;
            if (!readAt(primary_[j].offset, appointment)) return false;
            if (field(appointment.doctorId, 15) != byDoctorId_[i].doctorId) continue;

            expected++;
            bool found = false;
            for (int k = 0; k < static_cast<int>(matches.size()); k++) {
                if (field(matches[k].id, 15) == primary_[j].id) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (static_cast<int>(matches.size()) != expected) return false;
    }
    return true;
}

void printAppointmentDetails(const Appointment& appointment) {
    char id[16], date[31], doctorId[16];
    getField(appointment.id, 15, id);
    getField(appointment.date, 30, date);
    getField(appointment.doctorId, 15, doctorId);
    std::cout << id << " | " << date << " | " << doctorId << '\n';
}
