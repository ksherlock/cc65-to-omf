#ifndef CC65_TO_OMF
#define CC65_TO_OMF

#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>

inline void push_back_string(std::vector<uint8_t> &data, const std::string &s) {
	if (s.size() > 0xff) errx(1, "symbol too big: %s", s.c_str());
	data.push_back(s.size());
	data.insert(data.end(), s.begin(), s.end());
}

inline void push_back_8(std::vector<uint8_t> &data, uint8_t x) {
	data.push_back(x);
}

inline void push_back_16(std::vector<uint8_t> &data, uint16_t x) {
	data.push_back(x);
	data.push_back(x >> 8);
}

inline void push_back_32(std::vector<uint8_t> &data, uint32_t x) {
	data.push_back(x);
	data.push_back(x >> 8);
	data.push_back(x >> 16);
	data.push_back(x >> 24);
}


// "export" is reserved word in C++....
struct export_sym {
	std::string name;
	unsigned long address = 0;
	bool equ = false;
};

struct segment {
	std::string name;
	long size = 0;
	unsigned omf_kind = 0;
	std::vector<uint8_t> omf;
	std::vector<export_sym> exports;
};

extern std::vector<std::string> StringPool;
extern std::vector<std::string> Imports;
extern std::vector<segment> Segments;

void export_expr(FILE *f, unsigned &section, long &offset);
void convert_expression(FILE *f, unsigned size, std::vector<uint8_t> &omf, unsigned segno);


#endif
