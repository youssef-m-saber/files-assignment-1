#pragma once
#include "Binary_file.h"
#include <string>
#include <vector>

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
    size_t count() const;
    int recordOffset(const std::string& id) const; // -1 if missing
    bool validateIndexes() const;

private:
    struct PrimaryEntry { std::string id; int offset; };
    struct SecondaryEntry { std::string key; int head; };
    std::string dataPath_, nameNodesPath_, specialtyNodesPath_;
    std::vector<PrimaryEntry> primary_;                
    std::vector<SecondaryEntry> byName_, bySpecialty_; 

    static bool validText(const std::string& value, size_t limit, bool id = false);
    static std::string field(const char* data, int length);
    int primaryPosition(const std::string& id) const; 
    int secondaryPosition(const std::vector<SecondaryEntry>& index, const std::string& key) const;
    bool readAt(int offset, Doctor& result) const;
    bool writeAt(int offset, const Doctor& doctor);
    bool rebuildIndexes();
    bool buildSecondaryFiles();
    bool writeSecondary(const std::string& path,
                        const std::vector<std::pair<std::string,std::string>>& items,
                        std::vector<SecondaryEntry>& index);
    std::vector<Doctor> findSecondary(const std::string& key,
                                     const std::vector<SecondaryEntry>& index,
                                     const std::string& nodePath) const;
    bool referencedByAppointment(const std::string& doctorId) const;
};

void printDoctorDetails(const Doctor& doctor);
