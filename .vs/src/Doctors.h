#pragma once
#include "Binary_file.h"
#include <cstddef>
#include <string>
#include <vector>

// Member 2: ALL three sorted index tables live on DISK, not in vectors.
// Binary search reads individual index entries directly from disk.
// The secondary linked-list nodes are kept in two separate disk files.
class DoctorsManager {
public:
    explicit DoctorsManager(std::string dataPath = "Doctors.dat");
    bool initialize(bool seedFive = true);
    bool addDoctor(const std::string& id, const std::string& name,
                   const std::string& specialty, const std::string& address);
    bool deleteDoctor(const std::string& id);
    bool updateDoctorName(const std::string& id, const std::string& newName);
    bool updateDoctorID(const std::string& oldId, const std::string& newId);
    bool updateDoctorSpecialty(const std::string& id, const std::string& specialty);
    bool updateDoctorAddress(const std::string& id, const std::string& address);
    bool findDoctorByID(const std::string& id, Doctor& result) const;
    std::vector<Doctor> findDoctorsByName(const std::string& name) const;
    std::vector<Doctor> findDoctorsBySpecialty(const std::string& specialty) const;
    std::vector<Doctor> listAllDoctors() const;
    std::size_t count() const;
    int recordOffset(const std::string& id) const;
    bool validateIndexes() const;

private:
    std::string dataPath_, primaryPath_, nameIndexPath_, specialtyIndexPath_;
    std::string nameNodesPath_, specialtyNodesPath_;
    static bool validText(const std::string& value, std::size_t limit, bool id = false);
    static std::string field(const char* raw, int width);
    bool readAt(int offset, Doctor& result) const;
    bool writeAt(int offset, const Doctor& doctor);
    bool rebuildIndexes();
    bool referencedByAppointment(const std::string& id) const;
    std::vector<Doctor> findSecondary(const std::string& key,
                                     const std::string& indexPath,
                                     const std::string& nodesPath,
                                     int keyWidth) const;
};

void printDoctorDetails(const Doctor& doctor);
