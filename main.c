#include <windows.h>

#include "display.h"
#include "nes_cpu.h"
#include "nes_main.h"
#include "pcm.h"
#include "aa.h"
#include "nes_apu.h"

int main()
{
#if !ENABLE_LOG
    StartDisplayWindow();
#endif
    pcm_init(APU_SAMPLE_RATE / 2);

    // === CPU基础测试 ===
    // nes_load("../test_rom/nestest/nestest.nes");

    // === instr_test-v5 ===
    // 1. 测试所有指令包括非法指令
    // nes_load("../test_rom/instr_test-v5/all_instrs.nes");
    // 2. 只测试官方指令
    // nes_load("../test_rom/instr_test-v5/official_only.nes");

    // mapper0
    // nes_load("../rom/LanMaster.nes");
    // nes_load("../rom/cjml.nes");
    // nes_load("../rom/tkdz.nes");
    // nes_load("../rom/dkq2.nes");
    // nes_load("../rom/cdr.nes");
    // nes_load("../rom/zxd.nes");

    // mapper1
    // nes_load("../rom/tly.nes");

    // mapper2
    // nes_load("../rom/hdl.nes");

    // mapper3
    nes_load("../rom/mxd.nes");
    // nes_load("../rom/elsfk.nes");

    // // mapper4
    // nes_load("../rom/yjs.nes");

    pcm_cleanup();
}


