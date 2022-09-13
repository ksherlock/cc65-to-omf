#include <vector>
#include <string>

#include <stdio.h>
#include <stdint.h>

#include <err.h>

#include "exprdefs.h"
#include "fileio.h"

#include "to_omf.h"


// export segments need to be converted to globals.
// globals are inline at the specified location.

// in the .o file, globals are of the forms:
// Expr(section) or Expr(+, Expression(section), Expression(literal))




static void convert_expression_helper(FILE *f, std::vector<uint8_t> &omf, unsigned segno) {

	unsigned n;
	unsigned op = Read8(f);

	if (op == EXPR_NULL) return;

	if ((op & 0xc0) == 0x80) {
		switch(op) {
			case EXPR_NULL:
				break;
			case EXPR_LITERAL:
				omf.push_back(0x81);
				omf.push_back(Read8(f));
				omf.push_back(Read8(f));
				omf.push_back(Read8(f));
				omf.push_back(Read8(f));
				break;
			case EXPR_SYMBOL:
				n = ReadVar(f);
				omf.push_back(0x83);
				push_back_string(omf, Imports[n]);
				break;
			case EXPR_SECTION:
				n = Read8(f);
				if (n == segno) {
					omf.push_back(0x87); // rel
					push_back_32(omf, 0);
				} else {
					omf.push_back(0x83);
					push_back_string(omf, Segments[n].name);
				}
				break;
			default:
				errx(1,"Bad leaf node: $%02x", op);
		}
		return;
	}

	if ((op & 0xc0) == 0x40) {
		// unary
		std::vector<uint8_t> scratch;
		convert_expression_helper(f, omf, segno); // left
		convert_expression_helper(f, scratch, segno); // right - ignored.

		switch(op) {
			case EXPR_UNARY_MINUS:
				omf.push_back(0x06);
				break;

			case EXPR_NOT:
				omf.push_back(0x15);
				break;

			case EXPR_BOOLNOT:
				omf.push_back(0x0b);
				break;

			case EXPR_BYTE0:
				// x & 0xff, ie, nop it.
				break;				

			case EXPR_BYTE1:
				// (x >> 8) & 0xff
				omf.push_back(0x81); // literal
				push_back_32(omf, -8);
				omf.push_back(0x07);
				break;


			case EXPR_BYTE2:
				// (x >> 16) & 0xff
				omf.push_back(0x81); // literal
				push_back_32(omf, -16);
				omf.push_back(0x07);
				break;

			case EXPR_BYTE3:
				// (x >> 24) & 0xff
				omf.push_back(0x81); // literal
				push_back_32(omf, -24);
				omf.push_back(0x07);
				break;


			case EXPR_BANK:
			case EXPR_SWAP:

			case EXPR_WORD0:
			case EXPR_WORD1:
			case EXPR_FARADDR:
			case EXPR_DWORD:
			case EXPR_NEARADDR:
			default:
				errx(1,"Bad/unsupported unary node: $%02x", op);
		}
		return;
	}

	if ((op & 0xc0) == 0) {
		// binary ops

		// special case -- if this is a my_segment + literal
		// it can be converted to an OMF REL.
		if (op == EXPR_PLUS) {
			long pos = ftell(f);
			if (Read8(f) == EXPR_SECTION && Read8(f) == segno && Read8(f) == EXPR_LITERAL) {
				uint32_t offset = Read32(f);
				omf.push_back(0x87);
				push_back_32(omf, offset);
				return;
			}
			fseek(f, pos, SEEK_SET);
		} 


		convert_expression_helper(f, omf, segno); // left
		convert_expression_helper(f, omf, segno); // right

		switch(op) {
			case EXPR_PLUS:
				omf.push_back(0x01);
				break;
			case EXPR_MINUS:
				omf.push_back(0x02);
				break;
			case EXPR_MUL:
				omf.push_back(0x03);
				break;
			case EXPR_DIV:
				omf.push_back(0x04);
				break;
			case EXPR_MOD:
				// TODO -- verify modulo algorithm is the same
				omf.push_back(0x05);
				break;
			case EXPR_OR:
				omf.push_back(0x13);
				break;
			case EXPR_XOR:
				omf.push_back(0x14);
				break;
			case EXPR_AND:
				omf.push_back(0x12);
				break;
			case EXPR_SHL:
				omf.push_back(0x07);
				break;
			case EXPR_SHR:
				// TODO -- verify
				omf.push_back(0x06); // unary -
				omf.push_back(0x07); // shift
				break;
			case EXPR_EQ:
				omf.push_back(0x11);
				break;
			case EXPR_NE:
				omf.push_back(0x0e);
				break;
			case EXPR_LT:
				omf.push_back(0x0f);
				break;
			case EXPR_GT:
				omf.push_back(0x10);
				break;
			case EXPR_LE:
				omf.push_back(0x0c);
				break;
			case EXPR_GE:
				omf.push_back(0x0d);
				break;
			case EXPR_BOOLAND:
				omf.push_back(0x08);
				break;
			case EXPR_BOOLOR:
				omf.push_back(0x09);
				break;
			case EXPR_BOOLXOR:
				omf.push_back(0x0a);
				break;

			case EXPR_MAX:
			case EXPR_MIN:
			default:
				errx(1,"Bad binary node: $%02x", op);
		}
		return;
	}
	return;
}

void convert_expression(FILE *f, unsigned size, std::vector<uint8_t> &omf, unsigned segno) {

	omf.push_back(0xeb);
	omf.push_back(size);

	convert_expression_helper(f, omf, segno);

	omf.push_back(0x00); // end of expr
}



static unsigned export_expr_helper(FILE *f, unsigned &section, long &offset) {


	// parse an export segment expression.
	// supported types are Expression(section)
	// or Expression(+, Expression(section), Expression(literal))

	unsigned op;

	op = Read8(f);
	if (op == EXPR_SECTION) {
		section = Read8(f);
		return 1;
	}

	if (op == EXPR_LITERAL) {
		offset = Read32(f);
		return 2;
	}


	if (op == EXPR_PLUS) {
		unsigned n = 0;
		n |= export_expr_helper(f, section, offset);
		n |= export_expr_helper(f, section, offset);

		return n|4;
	}

	errx(1, "Unsupported export expression: $%02x", op);
}

void export_expr(FILE *f, unsigned &section, long &offset) {

	unsigned n = export_expr_helper(f, section, offset);
	if (n != 1 && n != 7)
		errx(1, "Unsupported export expression");
}
