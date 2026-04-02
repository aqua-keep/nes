#ifndef NES_CPU_H
#define NES_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {



#endif

// 控制是否非官方指令（0=关闭，1=开启）
#define ENABLE_ILLEGAL_OPCODE 1

// ----------------- 常量、标志位 -----------------
#define FLAG_C 0x01  // Carry Flag（进位标志）
#define FLAG_Z 0x02  // Zero Flag（零标志）
#define FLAG_I 0x04  // Interrupt Disable Flag（中断禁止标志）
#define FLAG_D 0x08  // Decimal Mode Flag（十进制模式标志）
#define FLAG_B 0x10  // Break Flag（软件中断标志）
#define FLAG_R 0x20  // Unused / Reserved Bit（保留位）
#define FLAG_V 0x40  // Overflow Flag（溢出标志）
#define FLAG_N 0x80  // Negative Flag（负数标志 / 符号标志）

// 向量地址
#define IRQ_VECTOR  0xFFFE
#define RES_VECTOR  0xFFFC
#define NMI_VECTOR  0xFFFA

// ----------------- 从外部回调（由工程其他部分提供） -----------------
// 导入的函数
extern uint8_t asm_Mapper_ReadLow(uint16_t wAddr);
extern void asm_Mapper_Write(uint8_t byData, uint16_t wAddr);
extern void asm_Mapper_WriteLow(uint8_t byData, uint16_t wAddr);

extern void Apu_Write(uint8_t value, uint32_t address);
extern void Apu_Write4015(uint8_t value, uint32_t address);
extern void Apu_Write4017(uint8_t value, uint32_t address);
extern uint8_t Apu_Read4015(uint32_t address);

extern void PPU_WriteToPort(uint8_t data, uint16_t addr);
extern uint8_t PPU_ReadFromPort(uint16_t addr);

// 导入的变量
extern uint8_t* NES_RAM; // 指向 0x800 区域（0x0000 - 0x07FF 实际 RAM）
extern uint8_t* NES_SRAM; // 外部保存器 (0x6000-0x7FFF)
extern uint8_t* spr_ram; // sprite ram 指针
extern uint8_t* romfile; // 指向整个 ROM 映像（包含 header），cpu6502_init 会读取
extern uint8_t PADdata0; //手柄0键值
extern uint8_t PADdata1; //手柄1键值

// ----------------- 向外部导出（名称与汇编一致） -----------------
// 导出的变量
extern uint8_t cpunmi; // 中断标志（cpunmif）
extern uint8_t cpuirq; // IRQ 标志（cpuirqf）
extern uint32_t clocks; // APU 需要的 CPU 时钟（clocksh）

// ----------------- 6502 导出函数（与汇编符号名保持一致） -----------------、
// 导出的函数
void cpu6502_init(void); // 初始化（映射、表、reset）
void CPU_reset(void); // 复位
void run6502(uint32_t cycles); // 运行：参数 r0 = 要运行的 cpu.md 周期 * 256 （与汇编约定）
void map67_(int8_t page);
void map89_(int8_t page);
void mapAB_(int8_t page);
void mapCD_(int8_t page);
void mapEF_(int8_t page);
uint8_t K6502_Read(uint16_t addr); // 直接内存读写（被其他模块可能调用）
void K6502_Write(uint16_t addr, uint8_t val); // 直接内存读写（被其他模块可能调用）
#ifdef __cplusplus
}
#endif
#endif //NES_CPU_H
