
#include <string>
#include <vector>

#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include "fileio.h"
#include "fragdefs.h"
#include "libdefs.h"
#include "objdefs.h"
#include "symdefs.h"


#include "to_omf.h"

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



std::vector<std::string> StringPool;
std::vector<std::string> Imports;
std::vector<segment> Segments;

void init() {
	StringPool.clear();
	Imports.clear();
	Segments.clear();
}


void save_omf_segment(FILE *f, const segment &seg) {

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
	header[34] = 0; // segnum -- Apple's linker warns if 0.
	header[35] = 0;
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
	fwrite(header, sizeof(header), 1, f);
	fwrite(seg.name.data(), 1, seg.name.size(), f);
	fwrite(seg.omf.data(), 1, seg.omf.size(), f);
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

	for (unsigned i = 0; i < count; ++i) {

		unsigned type = ReadVar(f);
		if (type & 0x07) {
			errx(1,"Constructor/Destructor not yet supported.");
		}
		unsigned as = Read8(f);
		unsigned nm = ReadVar(f);

		export_sym ex;
		ex.name = StringPool[nm];

		unsigned segno = 0;
		if (type & SYM_EXPR) {
			// segment or segment + offset.
			long offset = 0;
			export_expr(f, segno, offset);
			ex.address = offset;
		} else {
			ex.address = Read32(f);
			ex.equ = true;
		}

		unsigned size = 0;
		if (type & SYM_SIZE)
			size = ReadVar(f);

		skip_info_list(f);
		skip_info_list(f);

		Segments[segno].exports.emplace_back(std::move(ex));
	}

	// sort by address, reverse order.  segment 0 numeric equates go last.
	for (auto &s : Segments) {
		std::sort(s.exports.begin(), s.exports.end(), [](const export_sym &a, const export_sym &b){
			return std::make_pair(!a.equ, a.address) > std::make_pair(!b.equ, b.address);
		});
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

	// literal value exports at the front.
	while(!exports.empty()) {
		auto &e = exports.back();
		if (!e.equ) break;
		push_back_gequ(omf, e.name, 0, 'G', false, e.address);
		exports.pop_back();
	}

	unsigned long next_export = -1;
	unsigned long pc = 0;

	next_export = exports.empty() ? -1 : exports.back().address;


	for (i = 0; i < count; ++i) {
		unsigned type = Read8(f);
		unsigned n;

		if (next_export < pc) {
			auto &e = exports.back();
			errx(1, "Unable to assign export %s: ($%04lx) pc=$%04lx",
				e.name.c_str(), e.address, pc);
		}

		while (next_export == pc) {
			auto &e = exports.back();

			flush_pending(omf, pending);

			push_back_global(omf, e.name, 0, 'N', false);
			exports.pop_back();

			next_export = exports.empty() ? -1 : exports.back().address;
		}

		size_t pos;
		switch(type & FRAG_TYPEMASK) {
			case FRAG_LITERAL:
				n = ReadVar(f);
				// n bytes of data...
				if (n == 0) break;
				#if 0
				if (n <= 0xdf) {
					omf.push_back(n);
				} else {
					omf.push_back(0xf2); // LCONST
					push_back_32(omf, n);
				}
				pos = omf.size();
				for (unsigned i = 0; i < n; ++i)
					omf.push_back(0x00); //
				fread(omf.data() + pos, 1, n, f);
				#else

				pos = pending.size();
				pending.resize(pos + n);
				fread(pending.data() + pos, 1, n, f);
				#endif
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

				convert_expression(f, type & FRAG_BYTEMASK, omf, segno);
				pc += type & FRAG_BYTEMASK;
				break;
		}
		skip_info_list(f);
	}

	flush_pending(omf, pending);

	// trailing exports.
	while (next_export == pc) {
		auto &e = exports.back();

		push_back_global(omf, e.name, 0, 'N', false);
		exports.pop_back();

		next_export = exports.empty() ? -1 : exports.back().address;
	}
	if (!exports.empty()) {
		errx(1, "Unable to assign exports\n");
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

void process_obj(FILE *f) {


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


	FILE *out = fopen("out.omf", "wb");
	for (auto &seg : Segments) {
		if (!seg.omf.empty())
			save_omf_segment(out, seg);
	}
	fclose(out);
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
	for (unsigned i = 0; i < count; ++ count) {
		std::string name = ReadString(f);
		unsigned flags = Read16(f);
		unsigned long mtime = Read32(f);
		unsigned long offset = Read32(f);
		unsigned long size = Read32(f);

		unsigned long pos = ftell(f);
		fseek(f, offset, SEEK_SET);
		process_obj(f);
		fseek(f, pos, SEEK_SET);
	}

}


void show_usage(int ex) {

	fputs("cc65-to-omf [-o outfile] infile\n", stdout);
	exit(ex);
}

int main(int argc, char **argv) {


	const char *outfile = NULL;
	int c;
	FILE *f;


	while ((c = getopt(argc, argv, "o:vh")) != -1) {
		switch(c) {
			case 'o':
				outfile = optarg;
				break;
			default:
				show_usage(0);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) show_usage(1);


	f = fopen(argv[0], "rb");
	if (!f) err(1, "Unable to open file %s", argv[0]);

	switch(file_type(f)) {
		case 0: process_obj(f); break;
		case 1: process_lib(f); break;
	}


	return 0;
} 