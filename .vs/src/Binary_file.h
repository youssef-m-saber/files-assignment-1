#pragma once
#include <fstream>

constexpr int DOC_SIZE = 95;
constexpr int APP_SIZE = 60;
constexpr int HEADER_SIZE = sizeof(int);

struct Doctor {
    char id[15];
    char name[30];
    char specialty[20];
    char address[30];
};

struct Appointment {
    char id[15];
    char date[30];
    char doctorId[15];
};

void setField(char field[], int size, const char text[]);
void getField(const char field[], int size, char out[]);
Doctor makeDoctor(const char id[], const char name[], const char spec[], const char addr[]);
Appointment makeAppointment(const char id[], const char date[], const char docId[]);
void createFile(const char fileName[]);
int getHeader(std::fstream& f);
void setHeader(std::fstream& f, int head);
int getFileEnd(std::fstream& f);
bool validOffset(std::fstream& f, int offset, int recSize);
bool readRecord(std::fstream& f, int offset, char rec[], int recSize);
bool writeRecord(std::fstream& f, int offset, const char rec[], int recSize);
bool isDeleted(std::fstream& f, int offset, int recSize);
int addRecord(std::fstream& f, const char rec[], int recSize);
bool deleteRecord(std::fstream& f, int offset, int recSize);
void printDoctor(std::fstream& f, int offset);
