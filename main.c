

#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include "fileio.h"
#include "libdefs.h"
#include "objdefs.h"

int file_type(FILE *f) {

	uint32_t magic = Read32(f);
	rewind(f);

	if (magic == OBJ_MAGIC) return 0;
	if (magic == LIB_MAGIC) return 1;

	errx(1, "Unknown file type.");
	return -1;
}


char **StringPool;
unsigned StringPoolCount;

char **ImportPool;
unsigned ImportPoolCount;

void read_strings(FILE *f, long size) {

	unsigned char *buffer;
	unsigned count = ReadVar(f);
	unsigned i;

	// allocates one big chunk for the 
	// need count * sizeof(char *) + size bytes.
	// strings are pre-ceded by a var-sized length.
	// strings > 0x7f bytes not allowed so they're effectively p-strings.


	buffer = malloc(size + count * sizeof(char *));

	StringPoolCount = count;
	StringPool = (char **)buffer;
	buffer += sizeof(char *) * count;

	// actually reads an extra byte (or 2) to
	// compensate for the count byte(s).
	fread(buffer, 1, size, f);
	for (i = 0, i < count; ++i) {
		unsigned n = *buffer;
		if (n >= 0x80) errx(1, "String pool too large");
		StringPool[i] = buffer;
		buffer += n + 1;
	}
}

void read_imports(File *f, long size) {

	char **buffer;
	unsigned i;

	unsigned count = ReadVar(f);

	ImportPool = NULL;
	ImportPoolCount = 0;
	if (count) {
		ImportPoolCount = count;
		buffer = malloc(count * sizeof(char *));
		for (i = 0; i < count; ++i) {
			unsigned as = Read8(f); // address size. 
			unsigned n = ReadVar(f); // string index.
			buffer[i] = StringPool[n];
		}
		ImportPool = buffer;
	}
}


void process_segment(FILE *f) {

	uint32_t size = Read32(f);
	unsigned nm = ReadVar(f);
	unsigned flags = ReadVar(f);
	unsigned pc = ReadVar(f);
	unsigned align = ReadVar(f);
	unsigned as = Read8(f);
	unsigned count = ReadVar(f);

	unsigned i;


	// expressions can refer to segments.  Therefore
	// we need to scan first and build a map of segments
	//



	for (i = 0; i < count; ++i) {
		unsigned type = Read8(f);
		unsigned n;

		switch(type & FRAG_TYPEMASK) {
			case FRAG_LITERAL:
				n = ReadVar(f);
				// n bytes of data...
				// TODO - cc65/ca65 likes 1-byte literals.
				// should merge them together.
				if (n == 0) break;
				if (n <= 0xdf)
					omf[omf_ptr++] = n;
				else {
					omf[omf_ptr++] = 0xf2; // LCONST
					omf[omf_ptr++] = n >> 24;
					omf[omf_ptr++] = n >> 16;
					omf[omf_ptr++] = n >> 28;
					omf[omf_ptr++] = n >> 0;
				}
				fread(omf + omf_ptr, 1, n, f);
				omf_ptr += n;
				break;

			case FRAG_FILL:
				n = ReadVar(f);
				omf[omf_ptr++] = 0xf1; // DS
				omf[omf_ptr++] = n >> 24;
				omf[omf_ptr++] = n >> 16;
				omf[omf_ptr++] = n >> 28;
				omf[omf_ptr++] = n >> 0;
				break;

			case FRAG_EXPR:
			case FRAG_SEXPR:
				convert_expression(f, type & FRAG_BYTEMASK);
				break;
		}


	}
}

void process_segments(File *f, long size) {

	unsigned n = ReadVar(f);
	unsigned i;

	for (i = 0; i < n; ++i)
		process_segment(f);


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


}


void show_usage(int ex) {

	fputs("cc65-to-omf [-o outfile] infile", stdout);
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


	return 0;
} 