#include <vector>
#include <string>

#include <stdio.h>
#include <stdint.h>

#include <err.h>

#include "exprdefs.h"
#include "fileio.h"

#include "to_omf.h"


// OMF expression operations
#define OMF_END 0x00
#define OMF_ADD 0x01
#define OMF_SUB 0x02
#define OMF_MUL 0x03
#define OMF_DIV 0x04
#define OMF_MOD 0x05
#define OMF_NEG 0x06
#define OMF_SHIFT 0x07
#define OMF_AND 0x08
#define OMF_OR 0x09
#define OMF_EOR 0x0a
#define OMF_NOT 0x0b
#define OMF_LE 0x0c
#define OMF_GE 0x0d
#define OMF_NE 0x0e
#define OMF_LT 0x0f
#define OMF_GT 0x10
#define OMF_EQ 0x11
#define OMF_BAND 0x12
#define OMF_BOR 0x13
#define OMF_BEOR 0x14
#define OMF_BNOT 0x15

#define OMF_PC 0x80
#define OMF_ABS 0x81
#define OMF_WEAK 0x82
#define OMF_LAB 0x83
#define OMF_LAB_LENGTH 0x84
#define OMF_LAB_TYPE 0x85
#define OMF_LAB_COUNT 0x86
#define OMF_REL 0x87


static bool simplify_expression_helper(expr_vector &ev, int ix) {

	auto &e = ev[ix];

	if (e.op == EXPR_PLUS) {
		auto &l = ev[e.value >> 16];
		auto &r = ev[e.value & 0xffff];

		if (l.op == EXPR_SECTION && r.op == EXPR_LITERAL) {
			e.op = EXPR_SECTION;
			e.section = l.section;
			e.value = l.value + r.value;
			l.op = EXPR_NULL;
			r.op = EXPR_NULL;
			return true;
		}
	}

	if ((e.op & EXPR_TYPEMASK) == EXPR_UNARYNODE) {
		return simplify_expression_helper(ev, e.value);
	}

	if ((e.op & EXPR_TYPEMASK) == EXPR_BINARYNODE) {

		int l = e.value >> 16;
		int r = e.value & 0xffff;

		bool delta = false;
		delta |= simplify_expression_helper(ev, l);
		delta |= simplify_expression_helper(ev, r);

		if (e.op == EXPR_PLUS) {

			auto &ll = ev[l];
			auto &rr = ev[r];

			if (ll.op == EXPR_SECTION && rr.op == EXPR_LITERAL) {
				e = ll;
				e.value += rr.value;

				ll.op = EXPR_NULL;
				rr.op = EXPR_NULL;
				delta = true;
			}
		}

		if (e.op == EXPR_MINUS) {

			auto &ll = ev[l];
			auto &rr = ev[r];

			if (ll.op == EXPR_SECTION && rr.op == EXPR_LITERAL) {
				e = ll;
				e.value -= rr.value;
				ll.op = EXPR_NULL;
				rr.op = EXPR_NULL;
				delta = true;
			}
		}


		return delta;
	}


	return false;
}

void simplify_expression(expr_vector &ev) {

	bool delta = simplify_expression_helper(ev, 0);

	if (delta) {
		while (!ev.empty() && ev.back().op == EXPR_NULL)
			ev.pop_back();
	}

}

// check if this is a section / section + offset.
bool section_expr(const expr_vector &ev, int &seg, uint32_t &offset) {

	if (ev.empty()) return false;

	auto e = ev.front();
	if (e.op == EXPR_SECTION) {
		seg = e.section;
		offset = e.value;
		return true;
	}

	return false;
	// TODO - any other section references aren't supported....
}

void read_expr_helper(FILE *f, expr_vector &rv) {

	uint16_t op = Read8(f);
	if (op == EXPR_NULL) errx(1, "Unexpected NULL expression");

	if ((op & EXPR_TYPEMASK) == EXPR_LEAFNODE) {
		switch(op) {
			case EXPR_LITERAL:
				rv.emplace_back( op, Read32(f));
				break;
			case EXPR_SYMBOL:
				rv.emplace_back( op, ReadVar(f));
				break;
			case EXPR_SECTION:
				rv.emplace_back( op, ReadVar(f), 0);
				break;
			default:
				errx(1,"Bad leaf node: $%02x", op);
		}
		return;
	}

	if ((op & EXPR_TYPEMASK) == EXPR_UNARYNODE) {
		// unary
		auto ix = rv.size();
		rv.emplace_back(op, 0 );

		int l = rv.size(); read_expr_helper(f, rv); // left
		
		// right side.  should be null...
		op = Read8(f);
		if (op) errx(1, "Expected NULL for unary operation.");
		rv[ix].value = l;
		return;
	}

	if ((op & EXPR_TYPEMASK) == EXPR_BINARYNODE) {
		// binary

		auto ix = rv.size();
		rv.emplace_back(op, 0);
		int l = rv.size(); read_expr_helper(f, rv); // left
		int r = rv.size(); read_expr_helper(f, rv); // right
		rv[ix].value = (l << 16) | r;

		return;
	}
}

expr_vector read_expr(FILE *f) {

	expr_vector rv;
	read_expr_helper(f, rv);
	simplify_expression(rv);
	return rv;
}



// export segments need to be converted to globals.
// globals are inline at the specified location.

// in the .o file, globals are of the forms:
// Expr(section) or Expr(+, Expression(section), Expression(literal))


static void omf_mask(std::vector<uint8_t> &omf, uint32_t mask) {
	omf.push_back(OMF_ABS);
	push_back_32(omf, mask);
	omf.push_back(OMF_BAND);
}


static void convert_expression_helper(const expr_vector &ev, int ix, std::vector<uint8_t> &omf, unsigned size, unsigned segno) {


	const auto e = ev[ix];
	auto op = e.op;


	if ((op & EXPR_TYPEMASK) == EXPR_LEAFNODE) {
		unsigned seg = 0;
		long offset = 0;
		switch (op) {
			case EXPR_LITERAL:
				push_back_8(omf, OMF_ABS);
				push_back_32(omf, e.value);
				return;

			case EXPR_SYMBOL:
				push_back_8(omf, OMF_LAB);
				push_back_string(omf, Imports[e.value]);
				return;

			case EXPR_SECTION:
				if (e.section == segno) {
					push_back_8(omf, OMF_REL);
					push_back_32(omf, e.value);
				} else {
					push_back_8(omf, OMF_LAB);
					push_back_string(omf, Segments[seg].name);
					if (e.value) {
						push_back_8(omf, OMF_ABS);
						push_back_32(omf, e.value);
						push_back_8(omf, OMF_ADD);
					}		
				}
				break;
			default:
				errx(1,"Bad leaf node: $%02x", op);
		}

		return;
	}

	if ((op & EXPR_TYPEMASK) == EXPR_UNARYNODE) {
		// unary

		convert_expression_helper(ev, e.value, omf, size, segno);

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
				// e & 0xff, ie, nop it.
				if (size > 1) omf_mask(omf, 0xff);
				break;

			case EXPR_BYTE1:
				// (e >> 8) & 0xff
				omf.push_back(OMF_ABS); // literal
				push_back_32(omf, -8);
				omf.push_back(0x07);
				if (size > 1) omf_mask(omf, 0xff);
				break;


			case EXPR_BYTE2:
				// (e >> 16) & 0xff
				omf.push_back(OMF_ABS); // literal
				push_back_32(omf, -16);
				omf.push_back(0x07);
				if (size > 1) omf_mask(omf, 0xff);
				break;

			case EXPR_BYTE3:
				// (e >> 24) & 0xff
				omf.push_back(OMF_ABS); // literal
				push_back_32(omf, -24);
				omf.push_back(0x07);

				// if this is a segment,
				// masking not necessary (24-bit addressing)

				if (size > 1) omf_mask(omf, 0xff);
				break;



			// TODO -- WORD needs to check the size
			// and & 0xff (or append 1-byte const 0)
			// if not 1-byte.
			// eg, .word ^$12345667 -> .word $0034

			case EXPR_WORD0:
				// e & 0xffff
				if (size > 2) omf_mask(omf, 0xffff);
				break;

			case EXPR_WORD1:
				// (e >> 16) & 0xffff
				omf.push_back(OMF_ABS); // literal
				push_back_32(omf, -16);
				omf.push_back(0x07);
				if (size > 2) omf_mask(omf, 0xffff);
				break;


			case EXPR_BANK:
				// (e >> 24)
				omf.push_back(OMF_ABS); // literal
				push_back_32(omf, -24);
				omf.push_back(0x07);
				break;

			case EXPR_DWORD:
				break;

			case EXPR_SWAP:
			case EXPR_FARADDR:
			case EXPR_NEARADDR:
			default:
				errx(1,"Bad/unsupported unary node: $%02x", op);
		}
		return;
	}

	if ((op & EXPR_TYPEMASK) == EXPR_BINARYNODE) {

		int l = e.value >> 16;
		int r = e.value & 0xffff;

		convert_expression_helper(ev, l, omf, size, segno);
		convert_expression_helper(ev, r, omf, size, segno);

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
				errx(1,"Bad/unsupported binary node: $%02x", op);
		}
		return;
	}
}

static int expr_size(unsigned op) {
	switch(op) {
		case EXPR_BYTE0:
		case EXPR_BYTE1:
		case EXPR_BYTE2:
		case EXPR_BYTE3:
			return 1;
		case EXPR_WORD0:
		case EXPR_WORD1:
		case EXPR_NEARADDR:
			return 2;
		case EXPR_DWORD:
			return 4;
		case EXPR_FARADDR:
			return 3;
		default: return 4;
	}
}

void convert_expression(const expr_vector &ev, unsigned size, std::vector<uint8_t> &omf, unsigned segno) {

	// OMF relocations only support +/- and shift
	// so special handling to zero-pad 1-byte (^<>) ops
	unsigned zpad = 0;
	int op = ev.front().op;
	int es = expr_size(op);
	if (es < size) {
		zpad = size - es;
		size = es;
	}
	omf.push_back(0xeb);
	omf.push_back(size);

	convert_expression_helper(ev, 0, omf, size, segno);

	omf.push_back(0x00); // end of expr

	if (zpad) {
		omf.push_back(zpad);
		for (unsigned i = 0; i < zpad; ++i)
			omf.push_back(0x00);
	}
}



void convert_gequ(const std::string &name, const expr_vector &ev, std::vector<uint8_t> &omf) {

	push_back_8(omf, 0xe7); // gequ
	push_back_string(omf, name);
	push_back_16(omf, 0); // length
	push_back_8(omf, 'N'); // type
	push_back_8(omf, 0); // public

	convert_expression_helper(ev, 0, omf, 4, -1);
	omf.push_back(0x00); // end of expr
}
