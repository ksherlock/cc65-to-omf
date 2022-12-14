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

struct expr_node {
	uint16_t op = 0;
	uint16_t section = 0;
	uint32_t value = 0;

	expr_node(uint16_t a, uint32_t b) : 
		op(a), value(b)
	{}
	expr_node(uint16_t a, uint16_t b, uint32_t c) : 
		op(a), section(b), value(c)
	{}


};

typedef std::vector<expr_node> expr_vector;


// "export" is reserved word in C++....
struct export_sym {
	std::string name;

	expr_vector expr;
	bool sectional = false;
	int section = 0;
	uint32_t offset = 0;
};

struct segment {
	std::string name;
	long size = 0;
	long address = 0;
	unsigned omf_kind = 0;
	std::vector<uint8_t> omf;
	std::vector<export_sym> exports;
};

extern std::vector<std::string> StringPool;
extern std::vector<std::string> Imports;
extern std::vector<segment> Segments;



// void export_expr(FILE *f, unsigned &section, long &offset);
void convert_expression(const expr_vector &, unsigned size, std::vector<uint8_t> &omf, unsigned section);
bool section_expr(const expr_vector &ev, int &section, uint32_t &offset);

void convert_gequ(const std::string &name, const expr_vector &ev, std::vector<uint8_t> &omf);


expr_vector read_expr(FILE *f);

int set_prodos_file_type(const std::string &path, uint16_t fileType, uint32_t auxType);


// #define EXPR_SECTION_REL 0x87
#endif
