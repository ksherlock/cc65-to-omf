
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include "exprdefs.h"
#include "fileio.h"
#include "fragdefs.h"
#include "libdefs.h"
#include "objdefs.h"
#include "symdefs.h"


#include "to_omf.h"




bool flag_v = false;
const char *outfile = nullptr;

/*

In theory, exports could be handled by 
GEQU { name, relative offset, public }
HOWEVER, ORCA's Linker doesn't properly resolve them. (MPW IIgs linker does)
So, they need to be handled as inline GLOBAL entries.

*/



void push_back_global(std::vector<uint8_t> &data, const std::string &name, uint16_t length, uint8_t type, bool priv) {
	data.push_back(0xe6); // global
	push_back_string(data, name);
	data.push_back(length);
	data.push_back(length >> 8);
	data.push_back(type);
	data.push_back(priv ? 1 : 0);
}


void push_back_gequ(std::vector<uint8_t> &data, const std::string &name, uint16_t length, uint8_t type, bool priv, uint32_t value) {

	data.push_back(0xe7); // gequ
	push_back_string(data, name);
	data.push_back(length); // length
	data.push_back(length >> 8); // length
	data.push_back(type); // type - GEQU
	data.push_back(priv ? 1 : 0); // public
	// expression - literal.
	data.push_back(0x81);
	data.push_back(value >> 0);
	data.push_back(value >> 8);
	data.push_back(value >> 16);
	data.push_back(value >> 24);
	data.push_back(0x00); // end
}


struct file {
	unsigned number = 0;
	std::string name;
	std::vector<segment> segments;
};

std::vector<std::string> StringPool;
std::vector<std::string> Imports;
std::vector<segment> Segments;
std::vector<file> Files;

void reset() {
	StringPool.clear();
	Imports.clear();
	Segments.clear();
}


long save_omf_segment(FILE *f, const segment &seg, int segno) {

	uint8_t header[48 + 10 + 1];

	uint16_t kind = seg.omf_kind | 0x4000; // private.

	long n = seg.omf.size() + sizeof(header) + seg.name.size();
	header[0] = n >> 0; // byte count (4)
	header[1] = n >> 8;
	header[2] = n >> 16;
	header[3] = n >> 24;
	header[4] = 0; // reserved (4)
	header[5] = 0;
	header[6] = 0;
	header[7] = 0;
	header[8] = seg.size >> 0; // length
	header[9] = seg.size >> 8;
	header[10] = seg.size >> 16;
	header[11] = seg.size >> 24;
	header[12] = 0; // unused
	header[13] = 0; // label length - variable
	header[14] = 4; // numlen
	header[15] = 2; // version
	header[16] = static_cast<uint8_t>(0x010000 >> 0); // bank size
	header[17] = static_cast<uint8_t>(0x010000 >> 8);
	header[18] = static_cast<uint8_t>(0x010000 >> 16);
	header[19] = static_cast<uint8_t>(0x010000 >> 24);
	header[20] = kind >> 0; // kind
	header[21] = kind >> 8;
	header[22] = 0; // unused
	header[23] = 0;
	header[24] = 0; // org
	header[25] = 0;
	header[26] = 0;
	header[27] = 0;
	header[28] = 0; // alignment
	header[29] = 0;
	header[30] = 0;
	header[31] = 0;
	header[32] = 0; // little endian
	header[33] = 0; // unused;
	header[34] = segno >> 0; // segnum -- Apple's linker warns if 0.
	header[35] = segno >> 8;
	header[36] = 0; // entry 
	header[37] = 0;
	header[38] = 0;
	header[39] = 0;
	header[40] = 48 >> 0; // name displacement
	header[41] = 48 >> 8;
	n = 48 + 10 + 1 + seg.name.size();
	header[42] = n >> 0; // data displacement
	header[43] = n >> 8;
	header[44] = 0; // temporg (mpw)
	header[45] = 0;
	header[46] = 0;
	header[47] = 0;

	// load name
	for (int i = 0; i < 10; ++i) header[48 + i] = ' ';
	// seg name
	header[58] = seg.name.size();

	WriteData(f, header, sizeof(header));
	WriteData(f, seg.name.data(), seg.name.size());
	WriteData(f, seg.omf.data(), seg.omf.size());

	return seg.omf.size() + sizeof(header) + seg.name.size();
}


long save_omf_lib_header(FILE *f, const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, const std::vector<uint8_t> &c) {

	// sizeof("") includes trailing 0 byte.
	uint8_t header[44 + 10 + sizeof("LIBRARY")];

	const uint16_t kind = 0x08; // library

	long n = sizeof(header) + a.size() + b.size() + c.size() + 3 * 5 + 1; 
	header[0] = n >> 0; // byte count (4)
	header[1] = n >> 8;
	header[2] = n >> 16;
	header[3] = n >> 24;
	header[4] = 0; // reserved (4)
	header[5] = 0;
	header[6] = 0;
	header[7] = 0;
	header[8] = 0; // length
	header[9] = 0;
	header[10] = 0;
	header[11] = 0;
	header[12] = 0; // unused
	header[13] = 0; // label length - variable
	header[14] = 4; // numlen
	header[15] = 2; // version
	header[16] = 0; // bank size
	header[17] = 0;
	header[18] = 0;
	header[19] = 0;
	header[20] = kind >> 0; // kind
	header[21] = kind >> 8;
	header[22] = 0; // unused
	header[23] = 0;
	header[24] = 0; // org
	header[25] = 0;
	header[26] = 0;
	header[27] = 0;
	header[28] = 0; // alignment
	header[29] = 0;
	header[30] = 0;
	header[31] = 0;
	header[32] = 0; // little endian
	header[33] = 0; // unused;
	header[34] = 0; // segnum -- Apple's linker warns if 0.
	header[35] = 0;
	header[36] = 0; // entry 
	header[37] = 0;
	header[38] = 0;
	header[39] = 0;
	header[40] = 44 >> 0; // name displacement
	header[41] = 44 >> 8;
	n = sizeof(header);
	header[42] = n >> 0; // data displacement
	header[43] = n >> 8;

	// load name
	for (int i = 0; i < 18; ++i) header[44 + i] = "          \x07LIBRARY"[i];

	WriteData(f, header, sizeof(header));

	Write8(f, 0xf2); // lconst
	Write32(f,a.size());
	WriteData(f, a.data(), a.size());

	Write8(f, 0xf2); // lconst
	Write32(f,b.size());
	WriteData(f, b.data(), b.size());

	Write8(f, 0xf2); // lconst
	Write32(f,c.size());
	WriteData(f, c.data(), c.size());

	Write8(f, 0x00); // end

	return sizeof(header) + a.size() + b.size() + c.size() + 3 * 5 + 1;
}

int file_type(FILE *f) {

	uint32_t magic = Read32(f);
	rewind(f);

	if (magic == OBJ_MAGIC) return 0;
	if (magic == LIB_MAGIC) return 1;

	errx(1, "Unknown file type.");
	return -1;
}


void skip_info_list(FILE *f) {
	unsigned count = ReadVar(f);
	for (unsigned i = 0; i < count; ++i) {
		ReadVar(f);
	}
}

void read_strings(FILE *f, long size) {

	unsigned count = ReadVar(f);

	StringPool.reserve(count);
	for (unsigned i = 0; i < count; ++i) {
		std::string s = ReadString(f);
		StringPool.emplace_back(std::move(s));
	}
}

void read_imports(FILE *f, long size) {

	unsigned count = ReadVar(f);

	Imports.reserve(count);
	for (unsigned i = 0; i < count; ++i) {
		unsigned as = Read8(f); // address size. 
		unsigned nm = ReadVar(f); // string index.
		Imports.push_back(StringPool[nm]);
		skip_info_list(f);
		skip_info_list(f);
	}
}

void read_exports(FILE *f, long size) {

	unsigned count = ReadVar(f);

	std::vector<export_sym> global_exports;;

	for (unsigned i = 0; i < count; ++i) {

		unsigned type = ReadVar(f);
		if (type & 0x07) {
			errx(1,"Constructor/Destructor not yet supported.");
		}
		unsigned as = Read8(f);
		unsigned nm = ReadVar(f);

		export_sym ex;
		ex.name = StringPool[nm];

		if (type & SYM_EXPR) {
			ex.expr = read_expr(f);
			if (section_expr(ex.expr, ex.section, ex.offset)) {
				ex.sectional = true;
				ex.expr.clear();
			}
		} else {
			uint32_t value = Read32(f);
			ex.expr.emplace_back(EXPR_LITERAL, value);
		}

		unsigned size = 0;
		if (type & SYM_SIZE)
			size = ReadVar(f);

		skip_info_list(f);
		skip_info_list(f);


		if (ex.sectional) {
			Segments[ex.section].exports.emplace_back(std::move(ex));
		} else{
			global_exports.emplace_back(std::move(ex));
		}
	}

	// sort by address
	for (auto &s : Segments) {
		std::sort(s.exports.begin(), s.exports.end(), [](const export_sym &a, const export_sym &b){
			return a.offset < b.offset;
		});
	}

	if (!global_exports.empty()) {
		segment s;
		s.name = "GLOBALS";

		for (const auto &e : global_exports) {
			convert_gequ(e.name, e.expr, s.omf);
		}
		s.omf.push_back(0x00); // end!
		s.exports = std::move(global_exports);
		Segments.emplace_back(std::move(s));
	}
}

void flush_pending(std::vector<uint8_t> &omf, std::vector<uint8_t> &pending) {
	if (!pending.empty()) {
		auto n = pending.size();
		if (n <= 0xdf) {
			omf.push_back(n);
		} else {
			omf.push_back(0xf2); // lconst
			push_back_32(omf, n);
		}
		omf.insert(omf.end(), pending.begin(), pending.end());
		pending.clear();
	}
}

void process_segment(FILE *f, int segno) {

	uint32_t size = Read32(f);
	unsigned nm = ReadVar(f);
	unsigned flags = ReadVar(f);
	unsigned long expect_pc = ReadVar(f);
	unsigned align = ReadVar(f);
	unsigned as = Read8(f);
	unsigned count = ReadVar(f);

	unsigned i;

	auto &seg = Segments[segno];
	auto &omf = seg.omf;
	auto &exports = seg.exports;


	std::vector<uint8_t> pending;


	auto iter = exports.begin();
	auto end = exports.end();

	unsigned long next_export = -1;
	unsigned long pc = 0;

	next_export = iter == end ? - 1 : iter->offset;


	for (i = 0; i < count; ++i) {
		unsigned type = Read8(f);
		unsigned n;

		if (next_export < pc) {
			auto &e = *iter;
			errx(1, "Unable to assign export %s: ($%04lx) pc=$%04lx",
				e.name.c_str(), (long)e.offset, pc);
		}

		while (next_export == pc) {
			auto &e = *iter;

			flush_pending(omf, pending);

			push_back_global(omf, e.name, 0, 'N', false);
			++iter;
			next_export = iter == end ? - 1 : iter->offset;
		}

		size_t pos;
		switch(type & FRAG_TYPEMASK) {
			case FRAG_LITERAL:
				n = ReadVar(f);
				// n bytes of data...
				if (n == 0) break;

				pos = pending.size();
				pending.resize(pos + n);
				fread(pending.data() + pos, 1, n, f);

				pc += n;
				break;

			case FRAG_FILL:
				flush_pending(omf, pending);

				n = ReadVar(f);
				omf.push_back(0xf1); // DS
				push_back_32(omf, n);
				pc += n;
				break;

			case FRAG_EXPR:
			case FRAG_SEXPR:
				flush_pending(omf, pending);

				convert_expression(read_expr(f), type & FRAG_BYTEMASK, omf, segno);
				pc += type & FRAG_BYTEMASK;
				break;
		}
		skip_info_list(f);
	}

	flush_pending(omf, pending);

	// trailing exports.
	while (next_export == pc) {
		auto &e = *iter;

		push_back_global(omf, e.name, 0, 'N', false);
		++iter;
		next_export = iter == end ? - 1 : iter->offset;
	}
	if (iter != end) {
		while (iter != end) {
			const auto &e = *iter;
			warnx("Unable to assign export %s: ($%04lx) pc=$%04lx",
				e.name.c_str(), (long)e.offset, pc);
		}
		exit(1);
	}

	if (pc != expect_pc) errx(1, "PC Error");

	if (omf.size()) {
		omf.push_back(0x00); // end of segment opcode.
	}

}

void process_segments(FILE *f, long size) {

	unsigned n = ReadVar(f);

	for (unsigned i = 0; i < n; ++i)
		process_segment(f, i);
}


// pre-process the segment list.
void read_segments(FILE *f, long size) {


	unsigned count = ReadVar(f);

	// expressions can refer to segments.  Therefore
	// we need to scan first and build a map of segments
	// size is the on-disk size of the segment (excluding the size field)
	// pc is the size of the generated code, after linking.

	// default segs are generated, in this order:
	// CODE, RODATA, BSS, DATA, ZERO PAGE, NULL
	// ZEROPAGE has an address size of 1.
	for (unsigned i = 0; i < count; ++i) {

		long pos = ftell(f);

		uint32_t size = Read32(f);
		unsigned nm = ReadVar(f);
		unsigned flags = ReadVar(f);
		unsigned pc = ReadVar(f);
		unsigned align = ReadVar(f);
		unsigned as = Read8(f);
		unsigned count = ReadVar(f);

		segment seg;
		seg.name = StringPool[nm];
		seg.size = pc;

		seg.omf_kind = 0; // code
		if (seg.name == "ZEROPAGE" || as == 1)
			seg.omf_kind = 0x12; // dp stack segment

		Segments.emplace_back(std::move(seg));

		fseek(f, pos + size + 4, SEEK_SET);
	}


}

void process_obj(FILE *f, bool save) {


	ObjHeader h;

	long base = ftell(f);


	h.Magic = Read32(f);
	h.Version = Read16(f);
	h.Flags = Read16(f);
	h.OptionOffs = Read32(f);
	h.OptionSize = Read32(f);
	h.FileOffs = Read32(f);
	h.FileSize = Read32(f);
	h.SegOffs = Read32(f);
	h.SegSize = Read32(f);
	h.ImportOffs = Read32(f);
	h.ImportSize = Read32(f);
	h.ExportOffs = Read32(f);
	h.ExportSize = Read32(f);
	h.DbgSymOffs = Read32(f);
	h.DbgSymSize = Read32(f);
	h.LineInfoOffs = Read32(f);
	h.LineInfoSize = Read32(f);
	h.StrPoolOffs = Read32(f);
	h.StrPoolSize = Read32(f);
	h.AssertOffs = Read32(f);
	h.AssertSize = Read32(f);
	h.ScopeOffs = Read32(f);
	h.ScopeSize = Read32(f);
	h.SpanOffs = Read32(f);
	h.SpanSize = Read32(f);


	if (h.Magic != OBJ_MAGIC)
		errx(1, "Bad magic");
	if (h.Version != OBJ_VERSION)
		errx(1, "Bad version");


	// 1. read the string pool.
	// 2. read the imports
	// 3. read the exports
	// 4. convert the segments.

	fseek(f, base + h.StrPoolOffs, SEEK_SET);
	read_strings(f, h.StrPoolSize);

	fseek(f, base + h.ImportOffs, SEEK_SET);
	read_imports(f, h.ImportSize);


	fseek(f, base + h.SegOffs, SEEK_SET);
	read_segments(f, h.SegSize);	

	fseek(f, base + h.ExportOffs, SEEK_SET);
	read_exports(f, h.ExportSize);

	fseek(f, base + h.SegOffs, SEEK_SET);
	process_segments(f, h.SegSize);


	if (save) {
		if (!outfile) outfile = "out.omf";
		FILE *out = fopen(outfile, "wb");
		int segno = 0;
		for (auto &seg : Segments) {
			if (!seg.omf.empty())
				save_omf_segment(out, seg, ++segno);
		}
		fclose(out);
		set_prodos_file_type(outfile, 0xb1, 0x0000);
	}
}

void process_lib(FILE *f) {

	struct LibHeader h;

	h.Magic = Read32(f);
	h.Version = Read16(f);
	h.Flags = Read16(f);
	h.IndexOffs = Read32(f);


	if (h.Magic != LIB_MAGIC)
		errx(1, "Bad magic");
	if (h.Version != LIB_VERSION)
		errx(1, "Bad version");

	fseek(f, h.IndexOffs, SEEK_SET);

	unsigned count = ReadVar(f);
	for (unsigned i = 0; i < count; ++i) {
		std::string name = ReadString(f);
		unsigned flags = Read16(f);
		unsigned long mtime = Read32(f);
		unsigned long offset = Read32(f);
		unsigned long size = Read32(f);

		unsigned long pos = ftell(f);
		fseek(f, offset, SEEK_SET);
		process_obj(f, false);
		fseek(f, pos, SEEK_SET);

		file f;
		f.name = std::move(name);
		f.number = i + 1;
		f.segments = std::move(Segments);

		Files.emplace_back(std::move(f));

		reset();
	}

	// library segment consists of 3 lconst records:
	// 1. filenames
	// - { uint16_t fileno, pstring name}*
	// 2. symbol table
	// - { uint32_t name_displ, uint16_t fileno, uint16_t private, uint32_t segment_displ }*
	// 3. symbol names
	// - pstring*



	// now we can build everything...

	std::vector<uint8_t> file_names;
	std::vector<uint8_t> symbol_table;
	std::vector<uint8_t> symbol_names;
	std::unordered_map<std::string, uint32_t> symbol_map;


	// file names
	for (const auto &f : Files) {
		push_back_16(file_names, f.number);
		push_back_string(file_names, f.name);
	}

	unsigned symbol_count = 0;
	// symbol names
	for (const auto &f : Files) {
		for (const auto &seg : f.segments) {
			if (seg.omf.empty()) continue;

			symbol_count++;
			auto &name = seg.name;
			if (symbol_map.find(name) == symbol_map.end()) {
				uint32_t offset = symbol_names.size();
				symbol_map.emplace(name, offset);
				push_back_string(symbol_names, name);
			}
			for(const auto &e : seg.exports) {
				symbol_count++;
				auto &name = e.name;
				if (symbol_map.find(name) == symbol_map.end()) {
					uint32_t offset = symbol_names.size();
					symbol_map.emplace(name, offset);
					push_back_string(symbol_names, name);
				}
			}
		}
	}

	// symbols deferred until segment offset is known.
	symbol_table.reserve(symbol_count * 12);

	// lconst + end + segment header overhead.
	long address = 5 * 3 + 1 + 62 + file_names.size() + symbol_names.size() + symbol_count * 12;

	if (!outfile) outfile = "out.lib";
	FILE *out = fopen(outfile, "wb");
	fseek(out, address, SEEK_SET);

	for (const auto &f : Files) {
		unsigned segno = 0;
		for (const auto &seg : f.segments) {
			if (seg.omf.empty()) continue;
			// seg.address = address;

			auto &name = seg.name;

			push_back_32(symbol_table, symbol_map.at(name));
			push_back_16(symbol_table, f.number);
			push_back_16(symbol_table, 1); // private
			push_back_32(symbol_table, address);


			for (const auto &e : seg.exports) {
				auto &name = e.name;

				push_back_32(symbol_table, symbol_map.at(name));
				push_back_16(symbol_table, f.number);
				push_back_16(symbol_table, 0); // public
				push_back_32(symbol_table, address);

			}
			address += save_omf_segment(out, seg, ++segno);
		}
	}
	fseek(out, 0, SEEK_SET);
	save_omf_lib_header(out, file_names, symbol_table, symbol_names);
	fclose(out);
	set_prodos_file_type(outfile, 0xb2, 0x0000);
}


void show_usage(int ex) {

	fputs("cc65-to-omf [-o outfile] infile\n", stdout);
	exit(ex);
}


int main(int argc, char **argv) {


	int c;
	FILE *f;


	while ((c = getopt(argc, argv, "o:vh")) != -1) {
		switch(c) {
			case 'h':
				show_usage(0);
				break;
			case 'v':
				flag_v = true;
				break;
			case 'o':
				outfile = optarg;
				break;
			default:
				show_usage(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) show_usage(1);


	f = fopen(argv[0], "rb");
	if (!f) err(1, "Unable to open file %s", argv[0]);

	switch(file_type(f)) {
		case 0: process_obj(f, true); break;
		case 1: process_lib(f); break;
	}


	return 0;
} 