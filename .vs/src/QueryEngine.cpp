#include "QueryEngine.h"
#include <cctype>   // std::tolower, std::isspace

namespace {

    // One "Field = 'value'" pair from the where clause.
    struct Condition {
        std::string field;   // lowercase
        std::string value;   // exactly as typed
    };

    // Everything the executor needs, extracted from the raw query text.
    struct ParsedQuery {
        std::string table;
        std::string selectField;
        std::vector<Condition> conditions;
    };

    std::string toLower(std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string trim(const std::string& s) {
        std::size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            start++;

        std::size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
            end--;

        // substr(position, length)
        return s.substr(start, end - start);
    }

    struct Clauses {
        std::string selectPart;   // text between "select" and "from"
        std::string tablePart;    // text between "from" and "where"
        std::string wherePart;    // everything after "where"
    };

    bool splitClauses(const std::string& rawQuery, Clauses& out, std::string& error) {
        const std::string selectKeyword = "select ";
        const std::string fromKeyword   = " from ";
        const std::string WhereKeyword  = " where ";

        std::string q = trim(rawQuery);
        if (!q.empty() && q.back() == ';')
            q = trim(q.substr(0, q.size() - 1));

        std::string lower = toLower(q);

        if (lower.compare(0, selectKeyword.size(), selectKeyword) != 0) {
            error = "Query must start with 'Select'";
            return false;
        }

        std::size_t fromPos = lower.find(fromKeyword);
        if (fromPos == std::string::npos) {
            error = "Missing 'from'";
            return false;
        }
        std::size_t wherePos = lower.find(WhereKeyword, fromPos);
        if (wherePos == std::string::npos) {
            error = "Missing 'where'";
            return false;
        }

        std::size_t selectStart = selectKeyword.size();
        std::size_t tableStart  = fromPos + fromKeyword.size();
        std::size_t whereStart  = wherePos + WhereKeyword.size();

        // Catch overlapping keywords such as "select from doctors ...". Without this check,
        // the subtractions in step would wrap around to a huge number.
        if (fromPos < selectStart || wherePos < tableStart) {
            error = "Malformed query: missing field list or table name";
            return false;
        }

        out.selectPart = trim(q.substr(selectStart, fromPos - selectStart));
        out.tablePart  = trim(q.substr(tableStart,  wherePos - tableStart));
        out.wherePart  = trim(q.substr(whereStart));// no length given = until the end

        if (out.selectPart.empty() || out.tablePart.empty() || out.wherePart.empty()) {
            error = "Malformed query: empty select, table or where part";
            return false;
        }
        return true;
    }

    bool splitConditionTexts(const std::string& wherePart, std::vector<std::string>& parts, std::string& error) {
        const std::string andKeyword = " and ";
        std::string lower = toLower(wherePart);
        bool inQuote = false;
        std::size_t start = 0;

        for (std::size_t i = 0; i < wherePart.size(); i++) {
            if (wherePart[i] == '\'') {
                inQuote = !inQuote;
            }
            else if (!inQuote && lower.compare(i, andKeyword.size(), andKeyword) == 0) {
                // Found a real separator: save the text before it.
                parts.push_back(wherePart.substr(start, i - start));
                i += andKeyword.size() - 1;
                start = i + 1;
            }
        }
        if (inQuote) {
            error = "Missing closing quote";
            return false;
        }
        parts.push_back(wherePart.substr(start));
        return true;
    }

    bool parseCondition(const std::string& text, Condition& out, std::string& error) {
        std::size_t eq = text.find('=');
        if (eq == std::string::npos) {
            error = "Condition must look like Field='value'";
            return false;
        }

        std::string field = trim(text.substr(0, eq));
        std::string value = trim(text.substr(eq + 1));

        if (field.empty()) {
            error = "Missing field name before '='";
            return false;
        }
        if (value.size() < 2 || value.front() != '\'' || value.back() != '\'') {
            error = "Value must be inside single quotes, e.g. Doctor ID='D001'";
            return false;
        }

        out.field = toLower(field);
        out.value = value.substr(1, value.size() - 2);
        if (out.value.empty()) {
            error = "Value cannot be empty";
            return false;
        }
        return true;
    }

    // Full where-part parsing: split, then parse each piece.
    bool parseConditions(const std::string& wherePart, std::vector<Condition>& out, std::string& error) {
        std::vector<std::string> parts;
        if (!splitConditionTexts(wherePart, parts, error))
            return false;

        for (const std::string& p : parts) {
            Condition c;
            if (!parseCondition(p, c, error))
                return false;
            out.push_back(c);
        }
        return true;
    }

    const std::vector<std::string> kDoctorFields = {"doctor id", "doctor name", "doctor specialty", "address"};
    const std::vector<std::string> kAppointmentFields = {"appointment id", "appointment date", "doctor id"};

    bool contains(const std::vector<std::string>& list, const std::string& item) {
        for (const std::string& s : list)
            if (s == item)
                return true;
        return false;
    }

    // Parses AND validates a full query
    bool parseQuery(const std::string& rawQuery, ParsedQuery& out, std::string& error) {
        Clauses clauses;
        if (!splitClauses(rawQuery, clauses, error))
            return false;

        out.table = toLower(clauses.tablePart);
        if (out.table != "doctors" && out.table != "appointments") {
            error = "Unknown table '" + clauses.tablePart + "'. Use Doctors or Appointments";
            return false;
        }

        const std::vector<std::string>& validFields =
            (out.table == "doctors") ? kDoctorFields : kAppointmentFields;

        out.selectField = toLower(clauses.selectPart);
        if (out.selectField != "all" && !contains(validFields, out.selectField)) {
            error = "Unknown field '" + clauses.selectPart + "' for table " + clauses.tablePart;
            return false;
        }

        if (!parseConditions(clauses.wherePart, out.conditions, error))
            return false;

        for (const Condition& c : out.conditions) {
            if (!contains(validFields, c.field)) {
                error = "Field '" + c.field + "' does not belong to table " + clauses.tablePart;
                return false;
            }
        }
        return true;
    }

    std::string fieldToString(const char* data, std::size_t width) {
        return trim(std::string(data, width));
    }

    bool searchDoctors(const DoctorsManager& doctors, const ParsedQuery& q,
                       std::vector<Doctor>& found, std::string& indexUsed,
                       std::string& error) {
        found.clear();

        if (q.conditions.size() == 1) {
            const Condition& c = q.conditions[0];

            if (c.field == "doctor id") {
                Doctor d;
                if (doctors.findDoctorByID(c.value, d))
                    found.push_back(d);
                indexUsed = "Doctor ID primary index";
                return true;
            }
            if (c.field == "doctor name") {
                found = doctors.findDoctorsByName(c.value);
                indexUsed = "Doctor Name secondary index";
                return true;
            }
            if (c.field == "doctor specialty") {
                found = doctors.findDoctorsBySpecialty(c.value);
                indexUsed = "Doctor Specialty secondary index";
                return true;
            }
            error = "No index exists on '" + c.field +
                    "'. Search by Doctor ID, Doctor Name or Doctor Specialty";
            return false;
        }

        if (q.conditions.size() == 2) {
            std::string nameValue, specialtyValue;
            bool hasName = false, hasSpecialty = false;

            for (const Condition& c : q.conditions) {
                if (c.field == "doctor name") {
                    nameValue = c.value;
                    hasName = true;
                }
                else if (c.field == "doctor specialty") {
                    specialtyValue = c.value;
                    hasSpecialty = true;
                }
            }
            if (!hasName || !hasSpecialty) {
                error = "Two conditions are only supported as: "
                        "Doctor Name='...' and Doctor Specialty='...'";
                return false;
            }

            std::vector<Doctor> byName = doctors.findDoctorsByName(nameValue);
            std::vector<Doctor> bySpecialty = doctors.findDoctorsBySpecialty(specialtyValue);

            // Intersection: keep a doctor from the name list only if the SAME
            for (const Doctor& a : byName) {
                std::string idA = fieldToString(a.id, sizeof(a.id));
                for (const Doctor& b : bySpecialty) {
                    if (idA == fieldToString(b.id, sizeof(b.id))) {
                        found.push_back(a);
                        break;
                    }
                }
            }
            indexUsed = "Doctor Name and Doctor Specialty secondary indexes (intersection)";
            return true;
        }

        error = "Only one or two conditions are supported";
        return false;
    }

    const std::vector<std::string> kDoctorHeaders = {"Doctor ID", "Doctor Name", "Doctor Specialty", "Address"};

    int indexOf(const std::vector<std::string>& list, const std::string& item) {
        for (std::size_t i = 0; i < list.size(); i++)
            if (list[i] == item)
                return static_cast<int>(i);
        return -1;
    }

    std::string doctorValue(const Doctor& d, const std::string& field) {
        if (field == "doctor id")        return fieldToString(d.id, sizeof(d.id));
        if (field == "doctor name")      return fieldToString(d.name, sizeof(d.name));
        if (field == "doctor specialty") return fieldToString(d.specialty, sizeof(d.specialty));
        return fieldToString(d.address, sizeof(d.address));
    }

    void fillDoctorResult(const std::vector<Doctor>& found, const ParsedQuery& q, QueryResult& result) {
        std::vector<std::string> fields;
        if (q.selectField == "all")
            fields = kDoctorFields;
        else
            fields.push_back(q.selectField);

        for (const std::string& f : fields)
            result.columns.push_back(kDoctorHeaders[indexOf(kDoctorFields, f)]);

        for (const Doctor& d : found) {
            std::vector<std::string> row;
            for (const std::string& f : fields)
                row.push_back(doctorValue(d, f));
            result.rows.push_back(row);
        }
    }

    const std::vector<std::string> kAppointmentHeaders = {"Appointment ID", "Appointment Date", "Doctor ID"};

    bool searchAppointments(const AppointmentsManager& appointments, const ParsedQuery& q,
                            std::vector<Appointment>& found, std::string& indexUsed,
                            std::string& error) {
        found.clear();

        if (q.conditions.size() != 1) {
            error = "Appointments queries support exactly one condition";
            return false;
        }
        const Condition& c = q.conditions[0];

        if (c.field == "appointment id") {
            Appointment a;
            if (appointments.findAppointmentByID(c.value, a))
                found.push_back(a);
            indexUsed = "Appointment ID primary index";
            return true;
        }
        if (c.field == "doctor id") {
            found = appointments.findAppointmentsByDoctorID(c.value);
            indexUsed = "Appointment Doctor ID secondary index";
            return true;
        }

        error = "No index exists on '" + c.field +
                "'. Search by Appointment ID or Doctor ID";
        return false;
    }

    std::string appointmentValue(const Appointment& a, const std::string& field) {
        if (field == "appointment id")   return fieldToString(a.id, sizeof(a.id));
        if (field == "appointment date") return fieldToString(a.date, sizeof(a.date));
        return fieldToString(a.doctorId, sizeof(a.doctorId));
    }

    void fillAppointmentResult(const std::vector<Appointment>& found, const ParsedQuery& q, QueryResult& result) {
        std::vector<std::string> fields;
        if (q.selectField == "all")
            fields = kAppointmentFields;
        else
            fields.push_back(q.selectField);

        for (const std::string& f : fields)
            result.columns.push_back(kAppointmentHeaders[indexOf(kAppointmentFields, f)]);

        for (const Appointment& a : found) {
            std::vector<std::string> row;
            for (const std::string& f : fields)
                row.push_back(appointmentValue(a, f));
            result.rows.push_back(row);
        }
    }
}// namespace

QueryEngine::QueryEngine(DoctorsManager& doctors, AppointmentsManager& appointments)
    : doctors_(doctors), appointments_(appointments) {}

QueryResult QueryEngine::execute(const std::string& query) const {
    QueryResult result;
    ParsedQuery parsed;

    // 1. parse and validate.
    if (!parseQuery(query, parsed, result.error)) {
        result.ok = false;
        return result;
    }

    // 2. run against the right table.
    if (parsed.table == "doctors") {
        std::vector<Doctor> found;
        if (!searchDoctors(doctors_, parsed, found, result.indexUsed, result.error)) {
            result.ok = false;
            return result;
        }
        fillDoctorResult(found, parsed, result);
        result.ok = true;
        return result;
    }

    std::vector<Appointment> found;
    if (!searchAppointments(appointments_, parsed, found, result.indexUsed, result.error)) {
        result.ok = false;
        return result;
    }
    fillAppointmentResult(found, parsed, result);
    result.ok = true;
    return result;
}