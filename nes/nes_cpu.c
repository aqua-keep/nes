#include "nes_cpu.h"
#include "nes_main.h"
#include <string.h>
static uint8_t* CPU_ROM_banks[5] = {0};
static uint8_t joy0_shift; // 手柄1移位寄存器
static uint8_t joy1_shift; // 手柄2移位寄存器
static uint8_t A, X, Y, S, P;
static uint16_t PC;
static uint8_t temp8;
static uint16_t temp16; // Used for effective address (EA)
static int g_page_crossed = 0;
static uint16_t g_bad_addr = 0;
#if ENABLE_ILLEGAL_OPCODE
static int cpu_jam = 0; // CPU Jam state for KIL instructions
#endif
#define PAGE_CROSS(a, b) ((((a) ^ (b)) & 0x100) != 0)
#define READ_WORD(addr) (K6502_Read(addr) | (K6502_Read((addr) + 1) << 8))
#define setZ(v)    (P = (P & ~FLAG_Z) | ((v) == 0 ? FLAG_Z : 0))
#define setN(v)    (P = (P & ~FLAG_N) | ((v) & 0x80))
#define setNZ(v)   do { setZ(v); setN(v); } while(0)
static const uint8_t cycles_map[256] = {
	7, 6, 0, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
	0, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
	6, 6, 0, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
	0, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
	6, 6, 0, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
	0, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
	6, 6, 0, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
	0, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
	2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
	0, 6, 0, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
	2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
	0, 5, 0, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
	2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
	0, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
	2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
	0, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7
};

uint8_t K6502_Read(uint16_t addr)
{
	if (addr < 0x2000)
	{
		return NES_RAM[addr & 0x07FF];
	}
	else if (addr < 0x4000)
	{
		return PPU_ReadFromPort(addr & 7);
	}
	else if (addr < 0x4020)
	{
		switch (addr & 0x1F)
		{
		case 0x15: return Apu_Read4015(addr);
		case 0x16:
			{
				uint8_t result = (joy0_shift & 1) | 0x60;
				joy0_shift >>= 1;
				return result;
			}
		case 0x17:
			{
				uint8_t result = (joy1_shift & 1) | 0x60;
				joy1_shift >>= 1;
				return result;
			}
		default: return 0;
		}
	}
	else if (addr >= 0x5000 && addr < 0x6000)
	{
		return asm_Mapper_ReadLow(addr);
	}
	else if (addr < 0x8000)
	{
		return CPU_ROM_banks[0] ? CPU_ROM_banks[0][addr & 0x1FFF] : 0;
	}
	else if (addr < 0xA000)
	{
		return CPU_ROM_banks[1] ? CPU_ROM_banks[1][addr & 0x1FFF] : 0;
	}
	else if (addr < 0xC000)
	{
		return CPU_ROM_banks[2] ? CPU_ROM_banks[2][addr & 0x1FFF] : 0;
	}
	else if (addr < 0xE000)
	{
		return CPU_ROM_banks[3] ? CPU_ROM_banks[3][addr & 0x1FFF] : 0;
	}
	else
	{
		return CPU_ROM_banks[4] ? CPU_ROM_banks[4][addr & 0x1FFF] : 0;
	}
}

void K6502_Write(uint16_t addr, uint8_t val)
{
	if (addr < 0x2000)
	{
		NES_RAM[addr & 0x07FF] = val;
	}
	else if (addr < 0x4000)
	{
		PPU_WriteToPort(val, addr & 7);
	}
	else if (addr < 0x4020)
	{
		switch (addr & 0x1F)
		{
		case 0x14:
			{
				uint16_t src_addr = (uint16_t)val << 8;
				for (int i = 0; i < 256; i++)
				{
					spr_ram[i] = K6502_Read(src_addr + i);
				}
				clocks += 514;
				break;
			}
		case 0x15: Apu_Write4015(val, addr);
			break;
		case 0x16:
			if ((val & 1) == 0)
			{
				joy0_shift = PADdata0;
				joy1_shift = PADdata1;
			}
			break;
		case 0x17: Apu_Write4017(val, addr);
			break;
		default: Apu_Write(val, addr);
			break;
		}
	}
	else if (addr >= 0x5000 && addr < 0x6000)
	{
		asm_Mapper_WriteLow(val, addr);
	}
	else if (addr < 0x8000)
	{
		NES_SRAM[addr - 0x6000] = val;
	}
	else
	{
		asm_Mapper_Write(val, addr);
	}
}

static inline void push(uint8_t val)
{
	K6502_Write(0x100 + S, val);
	S = (S - 1) & 0xFF;
}

static inline uint8_t pop(void)
{
	S = (S + 1) & 0xFF;
	return K6502_Read(0x100 + S);
}

static void ADC(uint8_t val)
{
	uint16_t sum = A + val + (P & FLAG_C);
	P = (P & ~(FLAG_V | FLAG_C)) | (((~(A ^ val) & (A ^ sum)) & 0x80) ? FLAG_V : 0) | ((sum > 0xFF) ? FLAG_C : 0);
	A = sum & 0xFF;
	setNZ(A);
}

static void SBC(uint8_t val)
{
	ADC(~val);
}

static void CMP(uint8_t a, uint8_t b)
{
	temp8 = a - b;
	P = (P & ~FLAG_C) | (a >= b ? FLAG_C : 0);
	setNZ(temp8);
}

// ---------------- Addressing Modes ----------------
static void am_IMP(void)
{
}

static void am_IMM(void)
{
	temp16 = PC;
	PC++;
}

static void am__ZP(void)
{
	temp16 = K6502_Read(PC++);
}

static void am_ZPX(void)
{
	temp16 = (K6502_Read(PC++) + X) & 0xFF;
}

static void am_ZPY(void)
{
	temp16 = (K6502_Read(PC++) + Y) & 0xFF;
}

static void am_ABS(void)
{
	temp16 = READ_WORD(PC);
	PC += 2;
}

static void am_ABX(void)
{
	uint16_t base = READ_WORD(PC);
	PC += 2;
	temp16 = (base + X) & 0xFFFF;
	if (PAGE_CROSS(base, temp16))
	{
		g_page_crossed = 1;
		g_bad_addr = (base & 0xFF00) | ((base + X) & 0xFF);
	}
}

static void am_ABY(void)
{
	uint16_t base = READ_WORD(PC);
	PC += 2;
	temp16 = (base + Y) & 0xFFFF;
	if (PAGE_CROSS(base, temp16))
	{
		g_page_crossed = 1;
		g_bad_addr = (base & 0xFF00) | ((base + Y) & 0xFF);
	}
}

static void am_IZX(void)
{
	uint8_t ptr = (K6502_Read(PC++) + X) & 0xFF;
	temp16 = K6502_Read(ptr) | (K6502_Read((ptr + 1) & 0xFF) << 8);
}

static void am_IZY(void)
{
	uint8_t ptr = K6502_Read(PC++);
	uint16_t base = K6502_Read(ptr) | (K6502_Read((ptr + 1) & 0xFF) << 8);
	temp16 = (base + Y) & 0xFFFF;
	if (PAGE_CROSS(base, temp16))
	{
		g_page_crossed = 1;
		g_bad_addr = (base & 0xFF00) | ((base + Y) & 0xFF);
	}
}

static void am_REL(void)
{
	int8_t offset = (int8_t)K6502_Read(PC);
	uint16_t target = (PC + 1 + offset) & 0xFFFF;
	if (PAGE_CROSS(PC + 1, target))
	{
		g_page_crossed = 1;
	}
	PC++;
	temp16 = target;
}

static void am_ACC(void)
{
	temp16 = 0xDEAD;
}

static void am_IND(void)
{
	uint16_t ptr = READ_WORD(PC);
	PC += 2;
	uint8_t low = K6502_Read(ptr);
	uint8_t high;
	if ((ptr & 0x00FF) == 0x00FF)
	{
		high = K6502_Read(ptr & 0xFF00);
	}
	else
	{
		high = K6502_Read(ptr + 1);
	}
	temp16 = (high << 8) | low;
}

// ---------------- Operations ----------------
static void op_ADC(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	ADC(K6502_Read(temp16));
}

static void op_AND(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	A &= K6502_Read(temp16);
	setNZ(A);
}

static void op_ASL(void)
{
	if (temp16 == 0xDEAD)
	{
		P = (P & ~FLAG_C) | ((A & 0x80) ? FLAG_C : 0);
		A <<= 1;
		setNZ(A);
	}
	else
	{
		temp8 = K6502_Read(temp16);
		P = (P & ~FLAG_C) | ((temp8 & 0x80) ? FLAG_C : 0);
		temp8 <<= 1;
		K6502_Write(temp16, temp8);
		setNZ(temp8);
	}
}

static void op_BCC(void)
{
	if (!(P & FLAG_C))
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BCS(void)
{
	if (P & FLAG_C)
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BEQ(void)
{
	if (P & FLAG_Z)
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BIT(void)
{
	temp8 = K6502_Read(temp16);
	P = (P & ~(FLAG_N | FLAG_V | FLAG_Z)) | (temp8 & (FLAG_N | FLAG_V)) | ((A & temp8) == 0 ? FLAG_Z : 0);
}

static void op_BMI(void)
{
	if (P & FLAG_N)
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BNE(void)
{
	if (!(P & FLAG_Z))
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BPL(void)
{
	if (!(P & FLAG_N))
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BRK(void)
{
	PC++;
	push((PC >> 8) & 0xFF);
	push(PC & 0xFF);
	push(P | FLAG_B | FLAG_R);
	P |= FLAG_I;
	PC = READ_WORD(IRQ_VECTOR);
}

static void op_BVC(void)
{
	if (!(P & FLAG_V))
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_BVS(void)
{
	if (P & FLAG_V)
	{
		PC = temp16;
		clocks += 1 + g_page_crossed;
	}
}

static void op_CLC(void) { P &= ~FLAG_C; }
static void op_CLD(void) { P &= ~FLAG_D; }
static void op_CLI(void) { P &= ~FLAG_I; }
static void op_CLV(void) { P &= ~FLAG_V; }

static void op_CMP(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	CMP(A, K6502_Read(temp16));
}

static void op_CPX(void)
{
	CMP(X, K6502_Read(temp16));
}

static void op_CPY(void)
{
	CMP(Y, K6502_Read(temp16));
}

static void op_DEC(void)
{
	temp8 = (K6502_Read(temp16) - 1) & 0xFF;
	K6502_Write(temp16, temp8);
	setNZ(temp8);
}

static void op_DEX(void)
{
	X = (X - 1) & 0xFF;
	setNZ(X);
}

static void op_DEY(void)
{
	Y = (Y - 1) & 0xFF;
	setNZ(Y);
}

static void op_EOR(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	A ^= K6502_Read(temp16);
	setNZ(A);
}

static void op_INC(void)
{
	temp8 = (K6502_Read(temp16) + 1) & 0xFF;
	K6502_Write(temp16, temp8);
	setNZ(temp8);
}

static void op_INX(void)
{
	X = (X + 1) & 0xFF;
	setNZ(X);
}

static void op_INY(void)
{
	Y = (Y + 1) & 0xFF;
	setNZ(Y);
}

static void op_JMP(void)
{
	PC = temp16;
}

static void op_JSR(void)
{
	PC--;
	push((PC >> 8) & 0xFF);
	push(PC & 0xFF);
	PC = temp16;
}

static void op_LDA(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	A = K6502_Read(temp16);
	setNZ(A);
}

static void op_LDX(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	X = K6502_Read(temp16);
	setNZ(X);
}

static void op_LDY(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	Y = K6502_Read(temp16);
	setNZ(Y);
}

static void op_LSR(void)
{
	if (temp16 == 0xDEAD)
	{
		P = (P & ~FLAG_C) | (A & 1);
		A >>= 1;
		setNZ(A);
	}
	else
	{
		temp8 = K6502_Read(temp16);
		P = (P & ~FLAG_C) | (temp8 & 1);
		temp8 >>= 1;
		K6502_Write(temp16, temp8);
		setNZ(temp8);
	}
}

static void op_NOP(void)
{
}

static void op_ORA(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	A |= K6502_Read(temp16);
	setNZ(A);
}

static void op_PHA(void) { push(A); }
static void op_PHP(void) { push(P | FLAG_B | FLAG_R); }

static void op_PLA(void)
{
	A = pop();
	setNZ(A);
}

static void op_PLP(void) { P = pop() | FLAG_R | FLAG_B; }

static void op_ROL(void)
{
	uint8_t carry_in = (P & FLAG_C) ? 1 : 0;
	if (temp16 == 0xDEAD)
	{
		P = (P & ~FLAG_C) | ((A & 0x80) ? FLAG_C : 0);
		A = (A << 1) | carry_in;
		setNZ(A);
	}
	else
	{
		temp8 = K6502_Read(temp16);
		P = (P & ~FLAG_C) | ((temp8 & 0x80) ? FLAG_C : 0);
		temp8 = (temp8 << 1) | carry_in;
		K6502_Write(temp16, temp8);
		setNZ(temp8);
	}
}

static void op_ROR(void)
{
	uint8_t carry_in = (P & FLAG_C) ? 0x80 : 0;
	if (temp16 == 0xDEAD)
	{
		P = (P & ~FLAG_C) | (A & 1);
		A = (A >> 1) | carry_in;
		setNZ(A);
	}
	else
	{
		temp8 = K6502_Read(temp16);
		P = (P & ~FLAG_C) | (temp8 & 1);
		temp8 = (temp8 >> 1) | carry_in;
		K6502_Write(temp16, temp8);
		setNZ(temp8);
	}
}

static void op_RTI(void)
{
	P = pop();
	temp16 = pop();
	temp16 |= pop() << 8;
	PC = temp16;
}

static void op_RTS(void)
{
	temp16 = pop();
	temp16 |= pop() << 8;
	PC = temp16 + 1;
}

static void op_SBC(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	SBC(K6502_Read(temp16));
}

static void op_SEC(void) { P |= FLAG_C; }
static void op_SED(void) { P |= FLAG_D; }
static void op_SEI(void) { P |= FLAG_I; }
static void op_STA(void) { K6502_Write(temp16, A); }
static void op_STX(void) { K6502_Write(temp16, X); }
static void op_STY(void) { K6502_Write(temp16, Y); }

static void op_TAX(void)
{
	X = A;
	setNZ(X);
}

static void op_TAY(void)
{
	Y = A;
	setNZ(Y);
}

static void op_TSX(void)
{
	X = S;
	setNZ(X);
}

static void op_TXA(void)
{
	A = X;
	setNZ(A);
}

static void op_TXS(void) { S = X; }

static void op_TYA(void)
{
	A = Y;
	setNZ(A);
}

// ---------------- Illegal Operations ----------------
#if ENABLE_ILLEGAL_OPCODE
// NOP Read: NOPs that read memory (side effects)
static void op_NRD(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	K6502_Read(temp16);
}

// SLO: ASL memory then ORA with A
static void op_SLO(void)
{
	temp8 = K6502_Read(temp16);
	P = (P & ~FLAG_C) | ((temp8 & 0x80) ? FLAG_C : 0);
	temp8 <<= 1;
	K6502_Write(temp16, temp8);
	A |= temp8;
	setNZ(A);
}

// RLA: ROL memory then AND with A
static void op_RLA(void)
{
	uint8_t carry_in = (P & FLAG_C) ? 1 : 0;
	temp8 = K6502_Read(temp16);
	P = (P & ~FLAG_C) | ((temp8 & 0x80) ? FLAG_C : 0);
	temp8 = (temp8 << 1) | carry_in;
	K6502_Write(temp16, temp8);
	A &= temp8;
	setNZ(A);
}

// SRE: LSR memory then EOR with A
static void op_SRE(void)
{
	temp8 = K6502_Read(temp16);
	P = (P & ~FLAG_C) | (temp8 & 1);
	temp8 >>= 1;
	K6502_Write(temp16, temp8);
	A ^= temp8;
	setNZ(A);
}

// RRA: ROR memory then ADC with A
static void op_RRA(void)
{
	uint8_t carry_in = (P & FLAG_C) ? 0x80 : 0;
	temp8 = K6502_Read(temp16);
	P = (P & ~FLAG_C) | (temp8 & 1);
	temp8 = (temp8 >> 1) | carry_in;
	K6502_Write(temp16, temp8);
	ADC(temp8);
}

// SAX: Store A & X
static void op_SAX(void)
{
	K6502_Write(temp16, A & X);
}

// LAX: Load A and X
static void op_LAX(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	A = K6502_Read(temp16);
	X = A;
	setNZ(A);
}

// DCP: DEC memory then CMP with A
static void op_DCP(void)
{
	temp8 = (K6502_Read(temp16) - 1) & 0xFF;
	K6502_Write(temp16, temp8);
	CMP(A, temp8);
}

// ISB: INC memory then SBC with A
static void op_ISB(void)
{
	temp8 = (K6502_Read(temp16) + 1) & 0xFF;
	K6502_Write(temp16, temp8);
	SBC(temp8);
}

// ANC: AND immediate, set carry from bit 7
static void op_ANC(void)
{
	A &= K6502_Read(temp16);
	setNZ(A);
	if (A & 0x80) P |= FLAG_C;
	else P &= ~FLAG_C;
}

// ALR: AND immediate then LSR A
static void op_ALR(void)
{
	A &= K6502_Read(temp16);
	P = (P & ~FLAG_C) | (A & 1);
	A >>= 1;
	setNZ(A);
}

// ARR: AND immediate then ROR A
// Complex flag behavior
static void op_ARR(void)
{
	A &= K6502_Read(temp16);
	uint8_t carry_in = (P & FLAG_C) ? 0x80 : 0;
	A = (A >> 1) | carry_in;
	setNZ(A);
	// Set C based on bit 5 and 6 (overflow logic)
	P = (P & ~FLAG_C) | ((A & 0x40) ? FLAG_C : 0);
	// Set V based on bit 5 XOR bit 6
	if (((A >> 5) ^ (A >> 6)) & 1) P |= FLAG_V;
	else P &= ~FLAG_V;
}

// XAA: TXA then AND immediate (A = X & Imm)
// Behavior varies by CPU revision, this is a common stable implementation.
static void op_XAA(void)
{
	A = X & K6502_Read(temp16);
	setNZ(A);
}

// AXS: Store X & A, then subtract (X = (A & X) - Imm)
static void op_AXS(void)
{
	uint8_t val = K6502_Read(temp16);
	uint16_t res = (A & X) - val;
	X = res & 0xFF;
	setNZ(X);
	P = (P & ~FLAG_C) | (res < 0x100 ? FLAG_C : 0);
}

// AHX: Store A & X & HighByte
static void op_AHX(void)
{
	uint8_t val = A & X & (uint8_t)(temp16 >> 8);
	K6502_Write(temp16, val);
}

// SHY: Store Y & (BaseAddrHi + 1)
static void op_SHY(void)
{
	uint16_t addr;
	uint8_t val;
	if (g_page_crossed)
	{
		addr = g_bad_addr;
		val = Y & ((g_bad_addr >> 8) + 1);
	}
	else
	{
		addr = temp16;
		val = Y & ((temp16 >> 8) + 1);
	}
	K6502_Write(addr, val);
}

// SHX: Store X & (BaseAddrHi + 1)
static void op_SHX(void)
{
	uint16_t addr;
	uint8_t val;
	if (g_page_crossed)
	{
		addr = g_bad_addr;
		val = X & ((g_bad_addr >> 8) + 1);
	}
	else
	{
		addr = temp16;
		val = X & ((temp16 >> 8) + 1);
	}
	K6502_Write(addr, val);
}

// TAS: Store A & X to S, then Store S & HighByte
static void op_TAS(void)
{
	S = A & X;
	uint8_t val = S & (uint8_t)(temp16 >> 8);
	K6502_Write(temp16, val);
}

// LAS: Load A, X, S with Mem & S
static void op_LAS(void)
{
	if (g_page_crossed)
	{
		K6502_Read(g_bad_addr);
		clocks++;
	}
	A = X = S = (K6502_Read(temp16) & S);
	setNZ(A);
}

// KIL: Jam the CPU
static void op_KIL(void)
{
	cpu_jam = 1;
	PC--; // Stay on current instruction
}
#endif

typedef void (*OpFunc)(void);
static OpFunc op_handlers[256][2] = {
#if ENABLE_ILLEGAL_OPCODE
	[0x00] = {am_IMP, op_BRK}, [0x01] = {am_IZX, op_ORA}, [0x02] = {am_IMP, op_KIL}, [0x03] = {am_IZX, op_SLO},
	[0x04] = {am__ZP, op_NRD}, [0x05] = {am__ZP, op_ORA}, [0x06] = {am__ZP, op_ASL}, [0x07] = {am__ZP, op_SLO},
	[0x08] = {am_IMP, op_PHP}, [0x09] = {am_IMM, op_ORA}, [0x0A] = {am_ACC, op_ASL}, [0x0B] = {am_IMM, op_ANC},
	[0x0C] = {am_ABS, op_NRD}, [0x0D] = {am_ABS, op_ORA}, [0x0E] = {am_ABS, op_ASL}, [0x0F] = {am_ABS, op_SLO},
	[0x10] = {am_REL, op_BPL}, [0x11] = {am_IZY, op_ORA}, [0x12] = {am_IMP, op_KIL}, [0x13] = {am_IZY, op_SLO},
	[0x14] = {am_ZPX, op_NRD}, [0x15] = {am_ZPX, op_ORA}, [0x16] = {am_ZPX, op_ASL}, [0x17] = {am_ZPX, op_SLO},
	[0x18] = {am_IMP, op_CLC}, [0x19] = {am_ABY, op_ORA}, [0x1A] = {am_IMP, op_NOP}, [0x1B] = {am_ABY, op_SLO},
	[0x1C] = {am_ABX, op_NRD}, [0x1D] = {am_ABX, op_ORA}, [0x1E] = {am_ABX, op_ASL}, [0x1F] = {am_ABX, op_SLO},
	[0x20] = {am_ABS, op_JSR}, [0x21] = {am_IZX, op_AND}, [0x22] = {am_IMP, op_KIL}, [0x23] = {am_IZX, op_RLA},
	[0x24] = {am__ZP, op_BIT}, [0x25] = {am__ZP, op_AND}, [0x26] = {am__ZP, op_ROL}, [0x27] = {am__ZP, op_RLA},
	[0x28] = {am_IMP, op_PLP}, [0x29] = {am_IMM, op_AND}, [0x2A] = {am_ACC, op_ROL}, [0x2B] = {am_IMM, op_ANC},
	[0x2C] = {am_ABS, op_BIT}, [0x2D] = {am_ABS, op_AND}, [0x2E] = {am_ABS, op_ROL}, [0x2F] = {am_ABS, op_RLA},
	[0x30] = {am_REL, op_BMI}, [0x31] = {am_IZY, op_AND}, [0x32] = {am_IMP, op_KIL}, [0x33] = {am_IZY, op_RLA},
	[0x34] = {am_ZPX, op_NRD}, [0x35] = {am_ZPX, op_AND}, [0x36] = {am_ZPX, op_ROL}, [0x37] = {am_ZPX, op_RLA},
	[0x38] = {am_IMP, op_SEC}, [0x39] = {am_ABY, op_AND}, [0x3A] = {am_IMP, op_NOP}, [0x3B] = {am_ABY, op_RLA},
	[0x3C] = {am_ABX, op_NRD}, [0x3D] = {am_ABX, op_AND}, [0x3E] = {am_ABX, op_ROL}, [0x3F] = {am_ABX, op_RLA},
	[0x40] = {am_IMP, op_RTI}, [0x41] = {am_IZX, op_EOR}, [0x42] = {am_IMP, op_KIL}, [0x43] = {am_IZX, op_SRE},
	[0x44] = {am__ZP, op_NRD}, [0x45] = {am__ZP, op_EOR}, [0x46] = {am__ZP, op_LSR}, [0x47] = {am__ZP, op_SRE},
	[0x48] = {am_IMP, op_PHA}, [0x49] = {am_IMM, op_EOR}, [0x4A] = {am_ACC, op_LSR}, [0x4B] = {am_IMM, op_ALR},
	[0x4C] = {am_ABS, op_JMP}, [0x4D] = {am_ABS, op_EOR}, [0x4E] = {am_ABS, op_LSR}, [0x4F] = {am_ABS, op_SRE},
	[0x50] = {am_REL, op_BVC}, [0x51] = {am_IZY, op_EOR}, [0x52] = {am_IMP, op_KIL}, [0x53] = {am_IZY, op_SRE},
	[0x54] = {am_ZPX, op_NRD}, [0x55] = {am_ZPX, op_EOR}, [0x56] = {am_ZPX, op_LSR}, [0x57] = {am_ZPX, op_SRE},
	[0x58] = {am_IMP, op_CLI}, [0x59] = {am_ABY, op_EOR}, [0x5A] = {am_IMP, op_NOP}, [0x5B] = {am_ABY, op_SRE},
	[0x5C] = {am_ABX, op_NRD}, [0x5D] = {am_ABX, op_EOR}, [0x5E] = {am_ABX, op_LSR}, [0x5F] = {am_ABX, op_SRE},
	[0x60] = {am_IMP, op_RTS}, [0x61] = {am_IZX, op_ADC}, [0x62] = {am_IMP, op_KIL}, [0x63] = {am_IZX, op_RRA},
	[0x64] = {am__ZP, op_NRD}, [0x65] = {am__ZP, op_ADC}, [0x66] = {am__ZP, op_ROR}, [0x67] = {am__ZP, op_RRA},
	[0x68] = {am_IMP, op_PLA}, [0x69] = {am_IMM, op_ADC}, [0x6A] = {am_ACC, op_ROR}, [0x6B] = {am_IMM, op_ARR},
	[0x6C] = {am_IND, op_JMP}, [0x6D] = {am_ABS, op_ADC}, [0x6E] = {am_ABS, op_ROR}, [0x6F] = {am_ABS, op_RRA},
	[0x70] = {am_REL, op_BVS}, [0x71] = {am_IZY, op_ADC}, [0x72] = {am_IMP, op_KIL}, [0x73] = {am_IZY, op_RRA},
	[0x74] = {am_ZPX, op_NRD}, [0x75] = {am_ZPX, op_ADC}, [0x76] = {am_ZPX, op_ROR}, [0x77] = {am_ZPX, op_RRA},
	[0x78] = {am_IMP, op_SEI}, [0x79] = {am_ABY, op_ADC}, [0x7A] = {am_IMP, op_NOP}, [0x7B] = {am_ABY, op_RRA},
	[0x7C] = {am_ABX, op_NRD}, [0x7D] = {am_ABX, op_ADC}, [0x7E] = {am_ABX, op_ROR}, [0x7F] = {am_ABX, op_RRA},
	[0x80] = {am_IMM, op_NOP}, [0x81] = {am_IZX, op_STA}, [0x82] = {am_IMM, op_NOP}, [0x83] = {am_IZX, op_SAX},
	[0x84] = {am__ZP, op_STY}, [0x85] = {am__ZP, op_STA}, [0x86] = {am__ZP, op_STX}, [0x87] = {am__ZP, op_SAX},
	[0x88] = {am_IMP, op_DEY}, [0x89] = {am_IMM, op_NOP}, [0x8A] = {am_IMP, op_TXA}, [0x8B] = {am_IMM, op_XAA},
	[0x8C] = {am_ABS, op_STY}, [0x8D] = {am_ABS, op_STA}, [0x8E] = {am_ABS, op_STX}, [0x8F] = {am_ABS, op_SAX},
	[0x90] = {am_REL, op_BCC}, [0x91] = {am_IZY, op_STA}, [0x92] = {am_IMP, op_KIL}, [0x93] = {am_IZY, op_AHX},
	[0x94] = {am_ZPX, op_STY}, [0x95] = {am_ZPX, op_STA}, [0x96] = {am_ZPY, op_STX}, [0x97] = {am_ZPY, op_SAX},
	[0x98] = {am_IMP, op_TYA}, [0x99] = {am_ABY, op_STA}, [0x9A] = {am_IMP, op_TXS}, [0x9B] = {am_ABY, op_TAS},
	[0x9C] = {am_ABX, op_SHY}, [0x9D] = {am_ABX, op_STA}, [0x9E] = {am_ABY, op_SHX}, [0x9F] = {am_ABY, op_AHX},
	[0xA0] = {am_IMM, op_LDY}, [0xA1] = {am_IZX, op_LDA}, [0xA2] = {am_IMM, op_LDX}, [0xA3] = {am_IZX, op_LAX},
	[0xA4] = {am__ZP, op_LDY}, [0xA5] = {am__ZP, op_LDA}, [0xA6] = {am__ZP, op_LDX}, [0xA7] = {am__ZP, op_LAX},
	[0xA8] = {am_IMP, op_TAY}, [0xA9] = {am_IMM, op_LDA}, [0xAA] = {am_IMP, op_TAX}, [0xAB] = {am_IMM, op_LAX},
	[0xAC] = {am_ABS, op_LDY}, [0xAD] = {am_ABS, op_LDA}, [0xAE] = {am_ABS, op_LDX}, [0xAF] = {am_ABS, op_LAX},
	[0xB0] = {am_REL, op_BCS}, [0xB1] = {am_IZY, op_LDA}, [0xB2] = {am_IMP, op_KIL}, [0xB3] = {am_IZY, op_LAX},
	[0xB4] = {am_ZPX, op_LDY}, [0xB5] = {am_ZPX, op_LDA}, [0xB6] = {am_ZPY, op_LDX}, [0xB7] = {am_ZPY, op_LAX},
	[0xB8] = {am_IMP, op_CLV}, [0xB9] = {am_ABY, op_LDA}, [0xBA] = {am_IMP, op_TSX}, [0xBB] = {am_ABY, op_LAS},
	[0xBC] = {am_ABX, op_LDY}, [0xBD] = {am_ABX, op_LDA}, [0xBE] = {am_ABY, op_LDX}, [0xBF] = {am_ABY, op_LAX},
	[0xC0] = {am_IMM, op_CPY}, [0xC1] = {am_IZX, op_CMP}, [0xC2] = {am_IMM, op_NOP}, [0xC3] = {am_IZX, op_DCP},
	[0xC4] = {am__ZP, op_CPY}, [0xC5] = {am__ZP, op_CMP}, [0xC6] = {am__ZP, op_DEC}, [0xC7] = {am__ZP, op_DCP},
	[0xC8] = {am_IMP, op_INY}, [0xC9] = {am_IMM, op_CMP}, [0xCA] = {am_IMP, op_DEX}, [0xCB] = {am_IMM, op_AXS},
	[0xCC] = {am_ABS, op_CPY}, [0xCD] = {am_ABS, op_CMP}, [0xCE] = {am_ABS, op_DEC}, [0xCF] = {am_ABS, op_DCP},
	[0xD0] = {am_REL, op_BNE}, [0xD1] = {am_IZY, op_CMP}, [0xD2] = {am_IMP, op_KIL}, [0xD3] = {am_IZY, op_DCP},
	[0xD4] = {am_ZPX, op_NRD}, [0xD5] = {am_ZPX, op_CMP}, [0xD6] = {am_ZPX, op_DEC}, [0xD7] = {am_ZPX, op_DCP},
	[0xD8] = {am_IMP, op_CLD}, [0xD9] = {am_ABY, op_CMP}, [0xDA] = {am_IMP, op_NOP}, [0xDB] = {am_ABY, op_DCP},
	[0xDC] = {am_ABX, op_NRD}, [0xDD] = {am_ABX, op_CMP}, [0xDE] = {am_ABX, op_DEC}, [0xDF] = {am_ABX, op_DCP},
	[0xE0] = {am_IMM, op_CPX}, [0xE1] = {am_IZX, op_SBC}, [0xE2] = {am_IMM, op_NOP}, [0xE3] = {am_IZX, op_ISB},
	[0xE4] = {am__ZP, op_CPX}, [0xE5] = {am__ZP, op_SBC}, [0xE6] = {am__ZP, op_INC}, [0xE7] = {am__ZP, op_ISB},
	[0xE8] = {am_IMP, op_INX}, [0xE9] = {am_IMM, op_SBC}, [0xEA] = {am_IMP, op_NOP}, [0xEB] = {am_IMM, op_SBC},
	[0xEC] = {am_ABS, op_CPX}, [0xED] = {am_ABS, op_SBC}, [0xEE] = {am_ABS, op_INC}, [0xEF] = {am_ABS, op_ISB},
	[0xF0] = {am_REL, op_BEQ}, [0xF1] = {am_IZY, op_SBC}, [0xF2] = {am_IMP, op_KIL}, [0xF3] = {am_IZY, op_ISB},
	[0xF4] = {am_ZPX, op_NRD}, [0xF5] = {am_ZPX, op_SBC}, [0xF6] = {am_ZPX, op_INC}, [0xF7] = {am_ZPX, op_ISB},
	[0xF8] = {am_IMP, op_SED}, [0xF9] = {am_ABY, op_SBC}, [0xFA] = {am_IMP, op_NOP}, [0xFB] = {am_ABY, op_ISB},
	[0xFC] = {am_ABX, op_NRD}, [0xFD] = {am_ABX, op_SBC}, [0xFE] = {am_ABX, op_INC}, [0xFF] = {am_ABX, op_ISB},
#else
	[0x00] = {am_IMP, op_BRK}, [0x01] = {am_IZX, op_ORA}, [0x02] = {am_IMP, op_NOP}, [0x03] = {am_IMP, op_NOP},
	[0x04] = {am_IMP, op_NOP}, [0x05] = {am__ZP, op_ORA}, [0x06] = {am__ZP, op_ASL}, [0x07] = {am_IMP, op_NOP},
	[0x08] = {am_IMP, op_PHP}, [0x09] = {am_IMM, op_ORA}, [0x0A] = {am_ACC, op_ASL}, [0x0B] = {am_IMP, op_NOP},
	[0x0C] = {am_IMP, op_NOP}, [0x0D] = {am_ABS, op_ORA}, [0x0E] = {am_ABS, op_ASL}, [0x0F] = {am_IMP, op_NOP},
	[0x10] = {am_REL, op_BPL}, [0x11] = {am_IZY, op_ORA}, [0x12] = {am_IMP, op_NOP}, [0x13] = {am_IMP, op_NOP},
	[0x14] = {am_IMP, op_NOP}, [0x15] = {am_ZPX, op_ORA}, [0x16] = {am_ZPX, op_ASL}, [0x17] = {am_IMP, op_NOP},
	[0x18] = {am_IMP, op_CLC}, [0x19] = {am_ABY, op_ORA}, [0x1A] = {am_IMP, op_NOP}, [0x1B] = {am_IMP, op_NOP},
	[0x1C] = {am_IMP, op_NOP}, [0x1D] = {am_ABX, op_ORA}, [0x1E] = {am_ABX, op_ASL}, [0x1F] = {am_IMP, op_NOP},
	[0x20] = {am_ABS, op_JSR}, [0x21] = {am_IZX, op_AND}, [0x22] = {am_IMP, op_NOP}, [0x23] = {am_IMP, op_NOP},
	[0x24] = {am__ZP, op_BIT}, [0x25] = {am__ZP, op_AND}, [0x26] = {am__ZP, op_ROL}, [0x27] = {am_IMP, op_NOP},
	[0x28] = {am_IMP, op_PLP}, [0x29] = {am_IMM, op_AND}, [0x2A] = {am_ACC, op_ROL}, [0x2B] = {am_IMP, op_NOP},
	[0x2C] = {am_ABS, op_BIT}, [0x2D] = {am_ABS, op_AND}, [0x2E] = {am_ABS, op_ROL}, [0x2F] = {am_IMP, op_NOP},
	[0x30] = {am_REL, op_BMI}, [0x31] = {am_IZY, op_AND}, [0x32] = {am_IMP, op_NOP}, [0x33] = {am_IMP, op_NOP},
	[0x34] = {am_IMP, op_NOP}, [0x35] = {am_ZPX, op_AND}, [0x36] = {am_ZPX, op_ROL}, [0x37] = {am_IMP, op_NOP},
	[0x38] = {am_IMP, op_SEC}, [0x39] = {am_ABY, op_AND}, [0x3A] = {am_IMP, op_NOP}, [0x3B] = {am_IMP, op_NOP},
	[0x3C] = {am_IMP, op_NOP}, [0x3D] = {am_ABX, op_AND}, [0x3E] = {am_ABX, op_ROL}, [0x3F] = {am_IMP, op_NOP},
	[0x40] = {am_IMP, op_RTI}, [0x41] = {am_IZX, op_EOR}, [0x42] = {am_IMP, op_NOP}, [0x43] = {am_IMP, op_NOP},
	[0x44] = {am_IMP, op_NOP}, [0x45] = {am__ZP, op_EOR}, [0x46] = {am__ZP, op_LSR}, [0x47] = {am_IMP, op_NOP},
	[0x48] = {am_IMP, op_PHA}, [0x49] = {am_IMM, op_EOR}, [0x4A] = {am_ACC, op_LSR}, [0x4B] = {am_IMP, op_NOP},
	[0x4C] = {am_ABS, op_JMP}, [0x4D] = {am_ABS, op_EOR}, [0x4E] = {am_ABS, op_LSR}, [0x4F] = {am_IMP, op_NOP},
	[0x50] = {am_REL, op_BVC}, [0x51] = {am_IZY, op_EOR}, [0x52] = {am_IMP, op_NOP}, [0x53] = {am_IMP, op_NOP},
	[0x54] = {am_IMP, op_NOP}, [0x55] = {am_ZPX, op_EOR}, [0x56] = {am_ZPX, op_LSR}, [0x57] = {am_IMP, op_NOP},
	[0x58] = {am_IMP, op_CLI}, [0x59] = {am_ABY, op_EOR}, [0x5A] = {am_IMP, op_NOP}, [0x5B] = {am_IMP, op_NOP},
	[0x5C] = {am_IMP, op_NOP}, [0x5D] = {am_ABX, op_EOR}, [0x5E] = {am_ABX, op_LSR}, [0x5F] = {am_IMP, op_NOP},
	[0x60] = {am_IMP, op_RTS}, [0x61] = {am_IZX, op_ADC}, [0x62] = {am_IMP, op_NOP}, [0x63] = {am_IMP, op_NOP},
	[0x64] = {am_IMP, op_NOP}, [0x65] = {am__ZP, op_ADC}, [0x66] = {am__ZP, op_ROR}, [0x67] = {am_IMP, op_NOP},
	[0x68] = {am_IMP, op_PLA}, [0x69] = {am_IMM, op_ADC}, [0x6A] = {am_ACC, op_ROR}, [0x6B] = {am_IMP, op_NOP},
	[0x6C] = {am_IND, op_JMP}, [0x6D] = {am_ABS, op_ADC}, [0x6E] = {am_ABS, op_ROR}, [0x6F] = {am_IMP, op_NOP},
	[0x70] = {am_REL, op_BVS}, [0x71] = {am_IZY, op_ADC}, [0x72] = {am_IMP, op_NOP}, [0x73] = {am_IMP, op_NOP},
	[0x74] = {am_IMP, op_NOP}, [0x75] = {am_ZPX, op_ADC}, [0x76] = {am_ZPX, op_ROR}, [0x77] = {am_IMP, op_NOP},
	[0x78] = {am_IMP, op_SEI}, [0x79] = {am_ABY, op_ADC}, [0x7A] = {am_IMP, op_NOP}, [0x7B] = {am_IMP, op_NOP},
	[0x7C] = {am_IMP, op_NOP}, [0x7D] = {am_ABX, op_ADC}, [0x7E] = {am_ABX, op_ROR}, [0x7F] = {am_IMP, op_NOP},
	[0x80] = {am_IMP, op_NOP}, [0x81] = {am_IZX, op_STA}, [0x82] = {am_IMP, op_NOP}, [0x83] = {am_IMP, op_NOP},
	[0x84] = {am__ZP, op_STY}, [0x85] = {am__ZP, op_STA}, [0x86] = {am__ZP, op_STX}, [0x87] = {am_IMP, op_NOP},
	[0x88] = {am_IMP, op_DEY}, [0x89] = {am_IMP, op_NOP}, [0x8A] = {am_IMP, op_TXA}, [0x8B] = {am_IMP, op_NOP},
	[0x8C] = {am_ABS, op_STY}, [0x8D] = {am_ABS, op_STA}, [0x8E] = {am_ABS, op_STX}, [0x8F] = {am_IMP, op_NOP},
	[0x90] = {am_REL, op_BCC}, [0x91] = {am_IZY, op_STA}, [0x92] = {am_IMP, op_NOP}, [0x93] = {am_IMP, op_NOP},
	[0x94] = {am_ZPX, op_STY}, [0x95] = {am_ZPX, op_STA}, [0x96] = {am_ZPY, op_STX}, [0x97] = {am_IMP, op_NOP},
	[0x98] = {am_IMP, op_TYA}, [0x99] = {am_ABY, op_STA}, [0x9A] = {am_IMP, op_TXS}, [0x9B] = {am_IMP, op_NOP},
	[0x9C] = {am_IMP, op_NOP}, [0x9D] = {am_ABX, op_STA}, [0x9E] = {am_IMP, op_NOP}, [0x9F] = {am_IMP, op_NOP},
	[0xA0] = {am_IMM, op_LDY}, [0xA1] = {am_IZX, op_LDA}, [0xA2] = {am_IMM, op_LDX}, [0xA3] = {am_IMP, op_NOP},
	[0xA4] = {am__ZP, op_LDY}, [0xA5] = {am__ZP, op_LDA}, [0xA6] = {am__ZP, op_LDX}, [0xA7] = {am_IMP, op_NOP},
	[0xA8] = {am_IMP, op_TAY}, [0xA9] = {am_IMM, op_LDA}, [0xAA] = {am_IMP, op_TAX}, [0xAB] = {am_IMP, op_NOP},
	[0xAC] = {am_ABS, op_LDY}, [0xAD] = {am_ABS, op_LDA}, [0xAE] = {am_ABS, op_LDX}, [0xAF] = {am_IMP, op_NOP},
	[0xB0] = {am_REL, op_BCS}, [0xB1] = {am_IZY, op_LDA}, [0xB2] = {am_IMP, op_NOP}, [0xB3] = {am_IMP, op_NOP},
	[0xB4] = {am_ZPX, op_LDY}, [0xB5] = {am_ZPX, op_LDA}, [0xB6] = {am_ZPY, op_LDX}, [0xB7] = {am_IMP, op_NOP},
	[0xB8] = {am_IMP, op_CLV}, [0xB9] = {am_ABY, op_LDA}, [0xBA] = {am_IMP, op_TSX}, [0xBB] = {am_IMP, op_NOP},
	[0xBC] = {am_ABX, op_LDY}, [0xBD] = {am_ABX, op_LDA}, [0xBE] = {am_ABY, op_LDX}, [0xBF] = {am_IMP, op_NOP},
	[0xC0] = {am_IMM, op_CPY}, [0xC1] = {am_IZX, op_CMP}, [0xC2] = {am_IMP, op_NOP}, [0xC3] = {am_IMP, op_NOP},
	[0xC4] = {am__ZP, op_CPY}, [0xC5] = {am__ZP, op_CMP}, [0xC6] = {am__ZP, op_DEC}, [0xC7] = {am_IMP, op_NOP},
	[0xC8] = {am_IMP, op_INY}, [0xC9] = {am_IMM, op_CMP}, [0xCA] = {am_IMP, op_DEX}, [0xCB] = {am_IMP, op_NOP},
	[0xCC] = {am_ABS, op_CPY}, [0xCD] = {am_ABS, op_CMP}, [0xCE] = {am_ABS, op_DEC}, [0xCF] = {am_IMP, op_NOP},
	[0xD0] = {am_REL, op_BNE}, [0xD1] = {am_IZY, op_CMP}, [0xD2] = {am_IMP, op_NOP}, [0xD3] = {am_IMP, op_NOP},
	[0xD4] = {am_IMP, op_NOP}, [0xD5] = {am_ZPX, op_CMP}, [0xD6] = {am_ZPX, op_DEC}, [0xD7] = {am_IMP, op_NOP},
	[0xD8] = {am_IMP, op_CLD}, [0xD9] = {am_ABY, op_CMP}, [0xDA] = {am_IMP, op_NOP}, [0xDB] = {am_IMP, op_NOP},
	[0xDC] = {am_IMP, op_NOP}, [0xDD] = {am_ABX, op_CMP}, [0xDE] = {am_ABX, op_DEC}, [0xDF] = {am_IMP, op_NOP},
	[0xE0] = {am_IMM, op_CPX}, [0xE1] = {am_IZX, op_SBC}, [0xE2] = {am_IMP, op_NOP}, [0xE3] = {am_IMP, op_NOP},
	[0xE4] = {am__ZP, op_CPX}, [0xE5] = {am__ZP, op_SBC}, [0xE6] = {am__ZP, op_INC}, [0xE7] = {am_IMP, op_NOP},
	[0xE8] = {am_IMP, op_INX}, [0xE9] = {am_IMM, op_SBC}, [0xEA] = {am_IMP, op_NOP}, [0xEB] = {am_IMM, op_SBC},
	[0xEC] = {am_ABS, op_CPX}, [0xED] = {am_ABS, op_SBC}, [0xEE] = {am_ABS, op_INC}, [0xEF] = {am_IMP, op_NOP},
	[0xF0] = {am_REL, op_BEQ}, [0xF1] = {am_IZY, op_SBC}, [0xF2] = {am_IMP, op_NOP}, [0xF3] = {am_IMP, op_NOP},
	[0xF4] = {am_IMP, op_NOP}, [0xF5] = {am_ZPX, op_SBC}, [0xF6] = {am_ZPX, op_INC}, [0xF7] = {am_IMP, op_NOP},
	[0xF8] = {am_IMP, op_SED}, [0xF9] = {am_ABY, op_SBC}, [0xFA] = {am_IMP, op_NOP}, [0xFB] = {am_IMP, op_NOP},
	[0xFC] = {am_IMP, op_NOP}, [0xFD] = {am_ABX, op_SBC}, [0xFE] = {am_ABX, op_INC}, [0xFF] = {am_IMP, op_NOP},
#endif
};


static void map_bank(int8_t page, uint8_t bank_idx)
{
	uint32_t num_banks = RomHeader ? RomHeader->num_16k_rom_banks : 2;
	uint32_t rommask = (num_banks * 0x4000) - 1;
	if (page < 0)
	{
		page = (int8_t)((num_banks * 2 + page) % (num_banks * 2));
	}
	uint32_t page_idx = ((uint32_t)page << 13) & rommask;
	uint32_t rom_offset = 16;
	if (RomHeader && (RomHeader->flags_1 & 0x04))
	{
		rom_offset += 512;
	}
	CPU_ROM_banks[bank_idx] = romfile + rom_offset + page_idx;
}

void map67_(int8_t page) { map_bank(page, 0); }
void map89_(int8_t page) { map_bank(page, 1); }
void mapAB_(int8_t page) { map_bank(page, 2); }
void mapCD_(int8_t page) { map_bank(page, 3); }
void mapEF_(int8_t page) { map_bank(page, 4); }

void cpu6502_init(void)
{
	memset(NES_RAM, 0, 0x800);
	uint32_t rom_offset = 16;
	if (RomHeader && (RomHeader->flags_1 & 0x04)) rom_offset += 512;
	uint32_t num_16k_banks = RomHeader ? RomHeader->num_16k_rom_banks : 2;
	uint32_t num_8k_banks = num_16k_banks * 2;
	CPU_ROM_banks[0] = NES_SRAM;
	CPU_ROM_banks[1] = romfile + rom_offset;
	CPU_ROM_banks[2] = romfile + rom_offset + 0x2000;
	uint32_t last_bank_idx = num_8k_banks - 1;
	uint32_t prev_bank_idx = (num_8k_banks > 1) ? (num_8k_banks - 2) : 0;
	CPU_ROM_banks[3] = romfile + rom_offset + (prev_bank_idx * 0x2000);
	CPU_ROM_banks[4] = romfile + rom_offset + (last_bank_idx * 0x2000);
	CPU_reset();
	LOG("CPU初始化成功！！！");
}

void CPU_reset(void)
{
	A = X = Y = 0;
	S = 0xFD;
	P = FLAG_R | FLAG_I;
	cpunmi = 0;
	cpuirq = 0;
	clocks = 0;
#if ENABLE_ILLEGAL_OPCODE
	cpu_jam = 0;
#endif
	PC = READ_WORD(RES_VECTOR);
}

void ISR6502(uint16_t VECTOR)
{
	push((PC >> 8) & 0xFF);
	push(PC & 0xFF);
	push(P & ~FLAG_B);
	P |= FLAG_I;
	PC = READ_WORD(VECTOR);
	clocks += 7;
}

void run6502(uint32_t cyc)
{
	uint32_t target_cycles = clocks + cyc;
	while (clocks < target_cycles)
	{
#if ENABLE_ILLEGAL_OPCODE
		if (cpu_jam) return; // Stop if CPU is jammed
#endif

		if (cpunmi)
		{
			cpunmi = 0;
			ISR6502(NMI_VECTOR);
		}
		if (cpuirq && !(P & FLAG_I))
		{
			cpuirq = 0;
			ISR6502(IRQ_VECTOR);
		}
		g_page_crossed = 0;
		uint8_t opcode = K6502_Read(PC++);
		op_handlers[opcode][0]();
		op_handlers[opcode][1]();
		clocks += cycles_map[opcode];
	}
}
