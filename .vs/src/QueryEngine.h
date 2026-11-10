#pragma once
#include "Doctors.h"
#include "Appointments.h"
#include <string>
#include <vector>

// Result of running one query. Member 5 only needs this struct
struct QueryResult {
    bool ok = false;
    std::string error;
    std::string indexUsed;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows; // Outer vector for records, inner vector for fields per record
};

class QueryEngine {
    public:
    QueryEngine(DoctorsManager& doctors, AppointmentsManager& appointments);
    QueryResult execute(const std::string& query) const;

    private:
    DoctorsManager& doctors_;
    AppointmentsManager& appointments_;
};