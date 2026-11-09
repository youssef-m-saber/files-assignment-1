#include "Binary_file.h"
#include <iostream>
#include <fstream>
#include <cstring>
using namespace std;

void setField(char field[], int size, const char text[]) {
    memset(field, ' ', size);
    int len = static_cast<int>(strlen(text));
    if (len > size) len = size;
    memcpy(field, text, len);
}
void getField(const char field[], int size, char out[]) {
    int len = size;
    while (len > 0 && field[len - 1] == ' ') len--;
    memcpy(out, field, len);
    out[len] = '\0';
}
Doctor makeDoctor(const char id[], const char name[], const char spec[], const char addr[]) {
    Doctor d;
    setField(d.id, 15, id);
    setField(d.name, 30, name);
    setField(d.specialty, 20, spec);
    setField(d.address, 30, addr);
    return d;
}
Appointment makeAppointment(const char id[], const char date[], const char docId[]) {
    Appointment a;
    setField(a.id, 15, id);
    setField(a.date, 30, date);
    setField(a.doctorId, 15, docId);
    return a;
}
void createFile(const char fileName[]) {
    fstream f(fileName, ios::in | ios::binary);
    if (f.is_open()) { f.close(); return; }
    f.clear();
    ofstream out(fileName, ios::binary);
    int head = -1;
    out.write((char*)&head, sizeof(int));
    out.close();
}
int getHeader(fstream& f) {
    int head;
    f.clear();
    f.seekg(0, ios::beg);
    f.read((char*)&head, sizeof(int));
    return head;
}
void setHeader(fstream& f, int head) {
    f.clear();
    f.seekp(0, ios::beg);
    f.write((char*)&head, sizeof(int));
    f.flush();
}
int getFileEnd(fstream& f) {
    f.clear();
    f.seekg(0, ios::end);
    return (int)f.tellg();
}
bool validOffset(fstream& f, int offset, int recSize) {
    if (offset < HEADER_SIZE) return false;
    if (recSize <= 0 || offset > getFileEnd(f) - recSize) return false;
    if ((offset - HEADER_SIZE) % recSize != 0) return false;
    return true;
}
bool readRecord(fstream& f, int offset, char rec[], int recSize) {
    if (!validOffset(f, offset, recSize)) return false;
    f.clear();
    f.seekg(offset, ios::beg);
    return static_cast<bool>(f.read(rec, recSize));
}
bool writeRecord(fstream& f, int offset, const char rec[], int recSize) {
    if (!validOffset(f, offset, recSize)) return false;
    f.clear();
    f.seekp(offset, ios::beg);
    f.write(rec, recSize);
    f.flush();
    return static_cast<bool>(f);
}
bool isDeleted(fstream& f, int offset, int recSize) {
    char rec[100];
    if (!readRecord(f, offset, rec, recSize)) return false;
    return rec[0] == '*';
}

int addRecord(fstream& f, const char rec[], int recSize) {
    int head = getHeader(f);
    if (head == -1) {
        int offset = getFileEnd(f);
        f.clear();
        f.seekp(offset, ios::beg);
        f.write(rec, recSize);
        f.flush();
        return offset;
    }
    int next;
    f.clear();
    f.seekg(head + 1, ios::beg);
    f.read((char*)&next, sizeof(int));

    writeRecord(f, head, rec, recSize);
    setHeader(f, next);
    return head;
}

bool deleteRecord(fstream& f, int offset, int recSize) {
    if (!validOffset(f, offset, recSize)) return false;
    if (isDeleted(f, offset, recSize)) return false;

    char rec[100];
    readRecord(f, offset, rec, recSize);

    int oldHead = getHeader(f);
    rec[0] = '*';
    memcpy(rec + 1, &oldHead, sizeof(int));

    writeRecord(f, offset, rec, recSize);
    setHeader(f, offset);
    return true;
}
void printDoctor(fstream& f, int offset) {
    char rec[DOC_SIZE];
    if (!readRecord(f, offset, rec, DOC_SIZE)) { cout << "Cannot read doctor at offset " << offset << endl; return; }
    if (rec[0] == '*') { cout << "offset " << offset << " : [DELETED]" << endl; return; }
    Doctor d;
    memcpy(&d, rec, DOC_SIZE);
    char id[16], name[31];
    getField(d.id, 15, id);
    getField(d.name, 30, name);
    cout << "offset " << offset << " : " << id << " , " << name << endl;
}
