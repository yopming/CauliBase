#pragma once

#include <iosfwd>

class CauliBase;

void handlePut(CauliBase &db, std::istringstream &iss);
void handleGet(CauliBase &db, std::istringstream &iss);
void handleDel(CauliBase &db, std::istringstream &iss);
void handleFlush(CauliBase &db);
void handleCompact(CauliBase &db);
void handleDebug(CauliBase &db);
void handleHelp();
