#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rv_emu.h"
#include "bits.h"

#define DEBUG 0

static void unsupported(char *s, uint32_t n)
{
	printf("unsupported %s 0x%x\n", s, n);
	exit(-1);
}

void emu_r_type(struct rv_state *rsp, uint32_t iw)
{
	uint32_t rd = get_bits(iw, 7, 5);
	uint32_t rs1 = get_bits(iw, 15, 5);
	uint32_t rs2 = get_bits(iw, 20, 5);
	uint32_t funct3 = get_bits(iw, 12, 3);
	uint32_t funct7 = get_bits(iw, 25, 7);

	switch (funct3) {
	case 0b000:
		if (funct7 == 0b0000000) /* ADD */
			rsp->regs[rd] = rsp->regs[rs1] + rsp->regs[rs2];
		else if (funct7 == 0b0100000) /* SUB */
			rsp->regs[rd] = rsp->regs[rs1] - rsp->regs[rs2];
		else if (funct7 == 0b0000001) /* MUL */
			rsp->regs[rd] = rsp->regs[rs1] * rsp->regs[rs2];
		else
			unsupported("R-type funct7", funct7);
		break;
	case 0b001:
		if (funct7 == 0b0000000) /* SLL */
			rsp->regs[rd] = rsp->regs[rs1] << rsp->regs[rs2];
		else
			unsupported("R-type funct7", funct7);
		break;
	case 0b100:
		if (funct7 == 0b0000001) /* DIV */
			rsp->regs[rd] = rsp->regs[rs1] / rsp->regs[rs2];
		else
			unsupported("R-type funct7", funct7);
		break;
	case 0b101:
		if (funct7 == 0b0000000) /* SRL */
			rsp->regs[rd] = rsp->regs[rs1] >> rsp->regs[rs2];
		else
			unsupported("R-type funct7", funct7);
		break;
	case 0b110:
		if (funct7 == 0b0000001) /* REM */
			rsp->regs[rd] = rsp->regs[rs1] % rsp->regs[rs2];
		else
			unsupported("R-type funct7", funct7);
		break;
	case 0b111:
		if (funct7 == 0b0000000) /* AND */
			rsp->regs[rd] = rsp->regs[rs1] & rsp->regs[rs2];
		else
			unsupported("R-type funct7", funct7);
		break;
	default:
		unsupported("R-type funct3", funct3);
	}

	rsp->pc += 4; /* Next instruction */
}

void emu_i_type(struct rv_state *rsp, uint32_t iw)
{
	uint32_t rd = get_bits(iw, 7, 5);
	uint32_t rs1 = get_bits(iw, 15, 5);
	uint32_t shamt = get_bits(iw, 20, 5);
	uint32_t funct3 = get_bits(iw, 12, 3);
	uint32_t funct7 = get_bits(iw, 25, 7);
	uint32_t imm = get_bits(iw, 20, 12);
	if (funct3 == 0b101 && funct7 == 0b0)
		rsp->regs[rd] = rsp->regs[rs1] >> shamt;
	else if (funct3 == 0b000)
		rsp->regs[rd] = rsp->regs[rs1] + sign_extend(imm, 12);
	else
		unsupported("I-type funct3", funct3);
	rsp->pc += 4; /* Next instruction */
}

void emu_load(struct rv_state *rsp, uint32_t iw)
{
	uint32_t rs1 = get_bits(iw, 15, 5);
	uint32_t rd = get_bits(iw, 7, 5);
	uint32_t funct3 = get_bits(iw, 12, 3);
	uint32_t imm = get_bits(iw, 20, 12);
	uint64_t offset_rs1 = (uint64_t)((uint8_t *)rsp->regs[rs1] + imm);
	switch (funct3) {
	case 0b000: /* LB */
		rsp->regs[rd] = *((uint8_t *)offset_rs1);
		break;
	case 0b011: /* LD */
		rsp->regs[rd] = *((uint64_t *)offset_rs1);
		break;
	default:
		unsupported("I-type funct3 (load)", funct3);
	}
	rsp->pc += 4; /* Next instruction */
}

void emu_s_type(struct rv_state *rsp, uint32_t iw)
{
	uint32_t imm4_0 = get_bits(iw, 7, 5);
	uint32_t imm11_5 = get_bits(iw, 25, 7);
	uint32_t imm = imm4_0 | (imm11_5 << 5);
	uint32_t rs1 = get_bits(iw, 15, 5);
	uint32_t rs2 = get_bits(iw, 20, 5);
	uint32_t funct3 = get_bits(iw, 12, 3);
	uint64_t offset_rs1 = (uint64_t)((uint8_t *)rsp->regs[rs1] + imm);
	switch (funct3) {
	case 0b000: /* SB */
		*((uint8_t *)offset_rs1) = rsp->regs[rs2];
		break;
	case 0b011: /* SD */
		*((uint64_t *)offset_rs1) = rsp->regs[rs2];
		break;
	default:
		unsupported("S-type funct3", funct3);
	}
	rsp->pc += 4;
}

void emu_uncond_j(struct rv_state *rsp, uint32_t iw)
{
	uint32_t rd = get_bits(iw, 7, 5);
	if (rd != 0) /* jal, link ra, else j, ra remain unchanged  */
		rsp->regs[rd] = (uint64_t)rsp->pc + 4;

	uint32_t imm20 = get_bit(iw, 31);
	uint32_t imm10_1 = get_bits(iw, 21, 10);
	uint32_t imm11 = get_bit(iw, 20);
	uint32_t imm19_12= get_bits(iw, 12, 8);
	uint32_t imm = (imm20 << 20) |
		       (imm10_1 << 1) |
		       (imm11 << 11) |
		       (imm19_12 << 12);
	int32_t signed_imm = sign_extend(imm, 21);

	rsp->pc += signed_imm;
}

void emu_jalr(struct rv_state *rsp, uint32_t iw)
{
	uint32_t rs1 = get_bits(iw, 15, 5);  /* Will be ra (aka x1) */
	uint64_t val = rsp->regs[rs1];  /* Value of regs[1] */

	rsp->pc = val;  /* PC = return address */
}

void emu_b_type(struct rv_state *rsp, uint32_t iw)
{
	uint32_t rs1 = get_bits(iw, 15, 5);
	uint32_t rs2 = get_bits(iw, 20, 5);
	uint32_t funct3 = get_bits(iw, 12, 3);
	uint32_t imm4_1 = get_bits(iw, 8, 4);
	uint32_t imm11 = get_bits(iw, 7, 1);
	uint32_t imm12 = get_bits(iw, 31, 1);
	uint32_t imm10_5 = get_bits(iw, 25, 6);
	uint32_t imm = (imm4_1 << 1) |
		       (imm11 << 11) |
		       (imm12 << 12) |
		       (imm10_5 << 5);

	/* Register values for comparison and the label offset are signed */
	int64_t signed_rs1_val = (int64_t)rsp->regs[rs1];
	int64_t signed_rs2_val = (int64_t)rsp->regs[rs2];
	int32_t signed_imm = sign_extend(imm, 13);

	switch (funct3) {
	case 0b000: /* BEQ */
		if (signed_rs1_val == signed_rs2_val)
			rsp->pc += signed_imm; /* jump to label */
		else
			rsp->pc += 4; /* Next instruction */
		break;
	case 0b100: /* BLT */
		if (signed_rs1_val < signed_rs2_val)
			rsp->pc += signed_imm; /* jump to label */
		else
			rsp->pc += 4; /* Next instruction */
		break;
	case 0b001: /* BNE */
		if (signed_rs1_val != signed_rs2_val)
			rsp->pc += signed_imm;
		else
			rsp->pc += 4; /* Next instruction */
		break;
	default:
		unsupported("B-type funct3", funct3);
	}
}

static void rv_one(struct rv_state *rsp)
{
	uint32_t iw = *((uint32_t *)rsp->pc);
	//iw = cache_lookup(&state->i_cache, (uint64_t) state->pc);

	uint32_t opcode = get_bits(iw, 0, 7);

#if DEBUG
	printf("iw: %x\n", iw);
#endif

	switch (opcode) {
	case 0b0110011:
		emu_r_type(rsp, iw);
		break;
	case 0b0010011:
		emu_i_type(rsp, iw);
		break;
	case 0b0000011: /* variant of I-type instructions */
		emu_load(rsp, iw);
		break;
	case 0b0100011:
		emu_s_type(rsp, iw);
		break;
	case 0b1101111:
		emu_uncond_j(rsp, iw);
		break;
	case 0b1100111:
		/* JALR (RET) is a variant of I-type instructions */
		emu_jalr(rsp, iw);
		break;
	case 0b1100011:
		emu_b_type(rsp, iw);
		break;
	default:
		unsupported("Unknown opcode", opcode);
	}
}

void rv_init(struct rv_state *state, uint32_t *target,
	     uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	state->pc = (uint64_t) target;
	state->regs[RV_A0] = a0;
	state->regs[RV_A1] = a1;
	state->regs[RV_A2] = a2;
	state->regs[RV_A3] = a3;

	state->regs[RV_ZERO] = 0;	// zero is always 0  (:
	state->regs[RV_RA] = RV_STOP;
	state->regs[RV_SP] = (uint64_t) & state->stack[STACK_SIZE];

	memset(&state->analysis, 0, sizeof(rv_analysis));
	cache_init(&state->i_cache);
}

uint64_t rv_emulate(struct rv_state *state)
{
	while (state->pc != RV_STOP) {
		rv_one(state);
	}
	return state->regs[RV_A0];
}

static void print_pct(char *fmt, int numer, int denom)
{
	double pct = 0.0;

	if (denom)
		pct = (double)numer / (double)denom *100.0;
	printf(fmt, numer, pct);
}

void rv_print(rv_analysis *a)
{
	int b_total = a->b_taken + a->b_not_taken;

	printf("=== Analysis\n");
	print_pct("Instructions Executed  = %d\n", a->i_count, a->i_count);
	print_pct("R-type + I-type        = %d (%.2f%%)\n", a->ir_count,
							    a->i_count);
	print_pct("Loads                  = %d (%.2f%%)\n", a->ld_count,
							    a->i_count);
	print_pct("Stores                 = %d (%.2f%%)\n", a->st_count,
							    a->i_count);
	print_pct("Jumps/JAL/JALR         = %d (%.2f%%)\n", a->j_count,
							    a->i_count);
	print_pct("Conditional branches   = %d (%.2f%%)\n", b_total,
							    a->i_count);
	print_pct("  Branches taken       = %d (%.2f%%)\n", a->b_taken,
							    b_total);
	print_pct("  Branches not taken   = %d (%.2f%%)\n", a->b_not_taken,
							    b_total);
}
