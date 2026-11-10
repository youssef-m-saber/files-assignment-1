#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>
#include "Appointments.h"
#include "Binary_file.h"
#include "Doctors.h"
#include "QueryEngine.h"
using namespace std;


// Input helpers
static bool inputClosed = false;

static string trimText(const string& s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

static string readLine(const string& prompt) {
    cout << prompt;
    string line;
    if (!getline(cin, line)) {
        inputClosed = true;
        return "";
    }
    return trimText(line);
}

static bool readField(const string& label, size_t maxLen, string& value) {
    while (true) {
        value = readLine("Enter " + label + " (max " + to_string(maxLen) + " chars): ");
        if (inputClosed) return false;
        if (value.empty()) {
            cout << "  " << label << " cannot be empty.\n";
        } else if (value.size() > maxLen) {
            cout << "  " << label << " is too long (" << value.size() << " chars, max "
                 << maxLen << ").\n";
        } else if (value[0] == '*') {
            cout << "  " << label << " cannot start with '*' (reserved as the delete marker).\n";
        } else if (value.find_first_of("\r\n\t") != string::npos) {
            cout << "  " << label << " cannot contain tabs or line breaks.\n";
        } else {
            return true;
        }
    }
}

static int readMenuChoice() {
    string text = readLine("Enter your choice: ");
    if (inputClosed || text.empty()) return -1;
    for (char c : text)
        if (!isdigit(static_cast<unsigned char>(c))) return -1;
    if (text.size() > 3) return -1;
    return stoi(text);
}

// Display helpers
static void printDoctorHeader() {
    cout << "Doctor ID | Doctor Name | Doctor Specialty | Address\n";
}

static void printAppointmentHeader() {
    cout << "Appointment ID | Appointment Date | Doctor ID\n";
}

static void printQueryResult(const QueryResult& result) {
    if (!result.ok) {
        cout << "Query error: " << result.error << '\n';
        return;
    }

    cout << "Index used: " << result.indexUsed << '\n';
    if (result.rows.empty()) {
        cout << "No matching records found.\n";
        return;
    }

    vector<size_t> widths(result.columns.size());
    for (size_t c = 0; c < result.columns.size(); c++) {
        widths[c] = result.columns[c].size();
        for (const auto& row : result.rows)
            if (c < row.size()) widths[c] = max(widths[c], row[c].size());
    }

    auto line = [&]() {
        for (size_t c = 0; c < widths.size(); c++) cout << '+' << string(widths[c] + 2, '-');
        cout << "+\n";
    };
    auto printRow = [&](const vector<string>& row) {
        for (size_t c = 0; c < widths.size(); c++) {
            string v = c < row.size() ? row[c] : "";
            cout << "| " << v << string(widths[c] - v.size() + 1, ' ');
        }
        cout << "|\n";
    };

    line();
    printRow(result.columns);
    line();
    for (const auto& row : result.rows) printRow(row);
    line();
    cout << result.rows.size() << " record(s) found.\n";
}

// Menu actions
static void addDoctor(DoctorsManager& doctors) {
    string id, name, specialty, address;
    if (!readField("Doctor ID", 15, id)) return;

    Doctor existing;
    if (doctors.findDoctorByID(id, existing)) {
        cout << "A doctor with ID '" << id << "' already exists. Nothing was written.\n";
        return;
    }
    if (!readField("Doctor Name", 30, name)) return;
    if (!readField("Doctor Specialty", 20, specialty)) return;
    if (!readField("Address", 30, address)) return;

    if (doctors.addDoctor(id, name, specialty, address))
        cout << "Doctor added successfully.\n";
    else
        cout << "Failed to add the doctor.\n";
}

static void addAppointment(DoctorsManager& doctors, AppointmentsManager& appointments) {
    string id, date, doctorId;
    if (!readField("Appointment ID", 15, id)) return;

    Appointment existing;
    if (appointments.findAppointmentByID(id, existing)) {
        cout << "An appointment with ID '" << id << "' already exists. Nothing was written.\n";
        return;
    }
    if (!readField("Appointment Date", 30, date)) return;
    if (!readField("Doctor ID", 15, doctorId)) return;

    Doctor doctor;
    if (!doctors.findDoctorByID(doctorId, doctor)) {
        cout << "Doctor '" << doctorId << "' does not exist. Add the doctor first.\n";
        return;
    }

    if (appointments.addAppointment(id, date, doctorId))
        cout << "Appointment added successfully.\n";
    else
        cout << "Failed to add the appointment.\n";
}

static void updateDoctorName(DoctorsManager& doctors) {
    string id, newName;
    if (!readField("Doctor ID", 15, id)) return;

    Doctor doctor;
    if (!doctors.findDoctorByID(id, doctor)) {
        cout << "Warning: doctor '" << id << "' was not found.\n";
        return;
    }
    if (!readField("new Doctor Name", 30, newName)) return;

    if (doctors.updateDoctorName(id, newName))
        cout << "Doctor name updated successfully.\n";
    else
        cout << "Failed to update the doctor name.\n";
}

static void updateDoctorId(DoctorsManager& doctors, AppointmentsManager& appointments) {
    string oldId, newId;
    if (!readField("current Doctor ID", 15, oldId)) return;

    Doctor doctor;
    if (!doctors.findDoctorByID(oldId, doctor)) {
        cout << "Warning: doctor '" << oldId << "' was not found.\n";
        return;
    }
    if (!appointments.findAppointmentsByDoctorID(oldId).empty()) {
        cout << "Cannot change the ID of doctor '" << oldId
             << "': appointments still reference it.\n"
             << "Delete those appointments first (or test with a doctor that has none).\n";
        return;
    }
    if (!readField("new Doctor ID", 15, newId)) return;

    Doctor other;
    if (doctors.findDoctorByID(newId, other)) {
        cout << "Doctor ID '" << newId << "' is already used. Nothing was changed.\n";
        return;
    }

    if (doctors.updateDoctorID(oldId, newId))
        cout << "Doctor ID updated successfully (primary index re-sorted).\n";
    else
        cout << "Failed to update the doctor ID.\n";
}

static void updateAppointmentDate(AppointmentsManager& appointments) {
    string id, newDate;
    if (!readField("Appointment ID", 15, id)) return;

    Appointment appointment;
    if (!appointments.findAppointmentByID(id, appointment)) {
        cout << "Warning: appointment '" << id << "' was not found.\n";
        return;
    }
    if (!readField("new Appointment Date", 30, newDate)) return;

    if (appointments.updateAppointmentDate(id, newDate))
        cout << "Appointment date updated successfully.\n";
    else
        cout << "Failed to update the appointment date.\n";
}

static void deleteAppointment(AppointmentsManager& appointments) {
    string id;
    if (!readField("Appointment ID", 15, id)) return;

    Appointment appointment;
    if (!appointments.findAppointmentByID(id, appointment)) {
        cout << "Warning: appointment '" << id << "' does not exist. Nothing was deleted.\n";
        return;
    }

    if (appointments.deleteAppointment(id))
        cout << "Appointment deleted (marked with '*' and added to the AVAIL LIST).\n";
    else
        cout << "Failed to delete the appointment.\n";
}

static void deleteDoctor(DoctorsManager& doctors, AppointmentsManager& appointments) {
    string id;
    if (!readField("Doctor ID", 15, id)) return;

    Doctor doctor;
    if (!doctors.findDoctorByID(id, doctor)) {
        cout << "Warning: doctor '" << id << "' does not exist. Nothing was deleted.\n";
        return;
    }
    size_t linked = appointments.findAppointmentsByDoctorID(id).size();
    if (linked > 0) {
        cout << "Cannot delete doctor '" << id << "': " << linked
             << " appointment(s) still belong to this doctor.\n"
             << "Delete those appointments first.\n";
        return;
    }

    if (doctors.deleteDoctor(id))
        cout << "Doctor deleted (marked with '*' and added to the AVAIL LIST).\n";
    else
        cout << "Failed to delete the doctor.\n";
}

static void printDoctorInfo(const DoctorsManager& doctors) {
    string id;
    if (!readField("Doctor ID", 15, id)) return;

    Doctor doctor;
    if (!doctors.findDoctorByID(id, doctor)) {
        cout << "Doctor '" << id << "' was not found.\n";
        return;
    }
    printDoctorHeader();
    printDoctorDetails(doctor);
}

static void printAppointmentInfo(const AppointmentsManager& appointments) {
    string id;
    if (!readField("Appointment ID", 15, id)) return;

    Appointment appointment;
    if (!appointments.findAppointmentByID(id, appointment)) {
        cout << "Appointment '" << id << "' was not found.\n";
        return;
    }
    printAppointmentHeader();
    printAppointmentDetails(appointment);
}

static void writeQuery(const QueryEngine& engine) {
    cout << "Examples:\n"
         << "  Select all from Doctors where Doctor ID='D001';\n"
         << "  Select all from Appointments where Doctor ID='D001';\n"
         << "  Select Doctor Name from Doctors where Doctor Name='Ahmed';\n"
         << "  Select all from Doctors where Doctor Name='Ahmed' and Doctor Specialty='Cardiology';\n";
    string query = readLine("Query> ");
    if (inputClosed) return;
    if (query.empty()) {
        cout << "Query is empty.\n";
        return;
    }
    printQueryResult(engine.execute(query));
}

// Main
static void printMenu() {
    cout << "\n========== Healthcare Management System ==========\n"
         << " 1. Add New Doctor\n"
         << " 2. Add New Appointment\n"
         << " 3. Update Doctor Name (Doctor ID)\n"
         << " 4. Update Doctor ID [Key Update Test]\n"
         << " 5. Update Appointment Date (Appointment ID)\n"
         << " 6. Delete Appointment (Appointment ID)\n"
         << " 7. Delete Doctor (Doctor ID)\n"
         << " 8. Print Doctor Info (Doctor ID)\n"
         << " 9. Print Appointment Info (Appointment ID)\n"
         << "10. Write Query\n"
         << "11. Exit\n"
         << "==================================================\n";
}

int main() {
    DoctorsManager doctors;
    if (!doctors.initialize(true)) {
        cout << "Error: could not initialize the Doctors file (it may be corrupted).\n";
        return 1;
    }
    AppointmentsManager appointments(doctors);
    if (!appointments.initialize(true)) {
        cout << "Error: could not initialize the Appointments file (it may be corrupted).\n";
        return 1;
    }
    QueryEngine engine(doctors, appointments);

    cout << "Welcome to the Healthcare Management System\n"
         << "Loaded " << doctors.count() << " doctor(s) and "
         << appointments.count() << " appointment(s).\n";

    int choice = 0;
    while (choice != 11 && !inputClosed) {
        printMenu();
        choice = readMenuChoice();
        if (inputClosed) break;

        switch (choice) {
            case 1:  addDoctor(doctors); break;
            case 2:  addAppointment(doctors, appointments); break;
            case 3:  updateDoctorName(doctors); break;
            case 4:  updateDoctorId(doctors, appointments); break;
            case 5:  updateAppointmentDate(appointments); break;
            case 6:  deleteAppointment(appointments); break;
            case 7:  deleteDoctor(doctors, appointments); break;
            case 8:  printDoctorInfo(doctors); break;
            case 9:  printAppointmentInfo(appointments); break;
            case 10: writeQuery(engine); break;
            case 11: break;
            default: cout << "Invalid choice. Please enter a number from 1 to 11.\n";
        }
    }
    cout << "Goodbye.\n";
    return 0;
}