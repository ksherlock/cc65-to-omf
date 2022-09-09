#include <stdio.h>
#include <stdint.h>

#include "exprdefs.h"
#include "fileio.h"

// export segments need to be converted to globals.
// globals are inline at the specified location.

// in the .o file, globals are of the forms:
// Expr(section) or Expr(+, Expression(section), Expression(literal))

uint8_t omf_stack[100];
unsigned omf_stack_size;

convert_expression(FILE *f, unsigned size) {

	unsigned i;

	omf_stack[0] = 0xeb;
	omf_stack[1] = size;

	i = convert_expression_helper(f, 2);

	omf_stack[i++] = 0x00; // END of expr.
	omf_stack_size = i;
}


static unsigned convert_expression_helper(FILE *f, i) {

	unsigned n;
	unsigned op = Read8(f);

	if ((op & 0xc0) == 0x80) {
		switch(op) {
			case EXPR_NULL:
				return i;
			case EXPR_LITERAL:
				omf_stack[i++] = 0x81;
				omf_stack[i++] = Read8(f);
				omf_stack[i++] = Read8(f);
				omf_stack[i++] = Read8(f);
				omf_stack[i++] = Read8(f);
				break;
			case EXPR_SYMBOL:
				n = read_var(f);
				omf_stack[i++] = 0x83;
				// copy from imports...
				break;
			case EXPR_SECTION:
				n = Read8(f);
				// if matches current segment, convert to REL ($87)
				// otherwise, conver to to 0x83 + section name.
				break;
			default:
				errx(1,"Bad leaf node: $%02x", op);
		}
		return i;
	}

	if ((op & 0xc0) == 0x40) {
		// unary
		i = convert_expression(f, i); // left
		convert_expression(f, i); // right - ignored.

		switch(op) {
			case EXPR_UNARY_MINUS:
				omf_stack[i++] = 0x06;
				break;

			case EXPR_NOT:
				omf_stack[i++] = 0x15;
				break;

			case EXPR_BOOLNOT:
				omf_stack[i++] = 0x0b;
				break;

			case EXPR_BANK:
			case EXPR_SWAP:

			case EXPR_BYTE0:
			case EXPR_BYTE1:
			case EXPR_BYTE2:
			case EXPR_BYTE3:
			case EXPR_WORD0:
			case EXPR_WORD1:
			case EXPR_FARADDR:
			case EXPR_DWORD:
			case EXPR_NEARADDR:
			default:
				errx(1,"Bad unary node: $%02x", op);
		}
		return i;
	}

	if ((op & 0xc0) == 0) {
		// binary ops

		i = convert_expression(f, i); // left
		i = convert_expression(f, i); // right

		switch(op) {
			case EXPR_PLUS:
				omf_stack[i++] = 0x01;
				break;
			case EXPR_MINUS:
				omf_stack[i++] = 0x02;
				break;
			case EXPR_MUL:
				omf_stack[i++] = 0x03;
				break;
			case EXPR_DIV:
				omf_stack[i++] = 0x04;
				break;
			case EXPR_MOD:
				// TODO -- verify modulo algorithm is the same
				omf_stack[i++] = 0x05;
				break;
			case EXPR_OR:
				omf_stack[i++] = 0x13;
				break;
			case EXPR_XOR:
				omf_stack[i++] = 0x14;
				break;
			case EXPR_AND:
				omf_stack[i++] = 0x12;
				break;
			case EXPR_SHL:
				omf_stack[i++] = 0x07;
				break;
			case EXPR_SHR:
				// TODO -- verify
				omf_stack[i++] = 0x06; // unary -
				omf_stack[i++] = 0x07; // shift
				break;
			case EXPR_EQ:
				omf_stack[i++] = 0x11;
				break;
			case EXPR_NE:
				omf_stack[i++] = 0x0e;
				break;
			case EXPR_LT:
				omf_stack[i++] = 0x0f;
				break;
			case EXPR_GT:
				omf_stack[i++] = 0x10;
				break;
			case EXPR_LE:
				omf_stack[i++] = 0x0c;
				break;
			case EXPR_GE:
				omf_stack[i++] = 0x0d;
				break;
			case EXPR_BOOLAND:
				omf_stack[i++] = 0x08;
				break;
			case EXPR_BOOLOR:
				omf_stack[i++] = 0x09;
				break;
			case EXPR_BOOLXOR:
				omf_stack[i++] = 0x0a;
				break;
			case EXPR_MAX:
			case EXPR_MIN:
			default:
				errx(1,"Bad binary node: $%02x", op);
		}
		return i;
	}
	return i;
}


void export_expr(FILE *f, unsigned *section, uint32_t *offset) {
	*section = 0;
	*offset = 0;

	unsigned n = export_expr_helper(f, &section, &offset);
	if (n != 1 && n != 7)
		errx(1, "Unsupported export expression");
}

static unsigned export_expr_helper(FILE *f, unsigned *section, uint32_t *offset) {


	// parse an export segment expression.
	// supported types are Expression(section)
	// or Expression(+, Expression(section), Expression(literal))

	unsigned op;

	op = Read8(f);
	if (op == EXPR_SECTION) {
		*section = Read8(f);
		return 1;
	}

	if (op == EXPR_LITERAL) {
		*offset = read32(f);
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
