#pragma once

#include "Binary_file.h"
#include "Doctors.h"
#include <string>
#include <vector>

// Person 3: manages Appointments.dat and its two indexes.
class AppointmentsManager {
public:
    // The main program passes the already initialized Person 2 manager.
    AppointmentsManager(DoctorsManager& doctors,
                        const std::string& dataPath = "Appointments.dat");

    bool initialize(bool seedTen = true);
    bool addAppointment(const std::string& id, const std::string& date,
                        const std::string& doctorId);
    bool deleteAppointment(const std::string& id);
    bool updateAppointmentDate(const std::string& id, const std::string& newDate);

    bool findAppointmentByID(const std::string& id, Appointment& result) const;
    std::vector<Appointment> findAppointmentsByDoctorID(const std::string& doctorId) const;
    std::vector<Appointment> listAllAppointments() const;
    size_t count() const;
    int recordOffset(const std::string& id) const; // -1 when it is not found
    bool validateIndexes() const;

private:
    struct PrimaryEntry {
        std::string id;
        int offset;
    };

    struct SecondaryEntry {
        std::string doctorId;
        int head;
    };

    DoctorsManager& doctors_; // Person 2 lookup is used for the foreign key.
    std::string dataPath_;
    std::string doctorNodesPath_;
    std::vector<PrimaryEntry> primary_;
    std::vector<SecondaryEntry> byDoctorId_;

    static bool validText(const std::string& value, size_t limit, bool isId = false);
    static std::string field(const char* data, int length);
    int primaryPosition(const std::string& id) const;
    int primaryInsertPosition(const std::string& id) const;
    int secondaryPosition(const std::string& doctorId) const;
    int secondaryInsertPosition(const std::string& doctorId) const;
    bool doctorExists(const std::string& doctorId) const;
    bool readAt(int offset, Appointment& result) const;
    bool writeAt(int offset, const Appointment& appointment);
    bool rebuildIndexes();
    bool buildDoctorSecondaryFile();
    std::vector<Appointment> findSecondary(const std::string& doctorId) const;
};

void printAppointmentDetails(const Appointment& appointment);
