#include "nes_main.h"
#include "devices/pcm.h"

int main()
{
    // === CPU基础测试 ===
    // nes_load("../test_rom/nestest/nestest.nes");

    // === instr_test-v5 ===
    // 1. 测试所有指令包括非法指令
    // nes_load("../test_rom/instr_test-v5/all_instrs.nes");
    // 2. 只测试官方指令
    // nes_load("../test_rom/instr_test-v5/official_only.nes");

    // mapper0
    // nes_load("../rom/LanMaster.nes");
    // nes_load("../rom/[234]  角色类 - 超级马利兄弟.NES");

    // mapper1
    // nes_load("../rom/[152]  角色类 - 雪人兄弟.NES");

    // mapper2
    // nes_load("../rom/[155]  角色类 - 小美人鱼.nes");

    // mapper3
    // nes_load("../rom/[022]  桌面类 - 俄罗斯方块.NES");

    // mapper4
    // nes_load("../rom/[031]  智力类 - 最终幻想3  .NES");

    // mapper15
    // nes_load("../rom/[037]  智力类 - 岳飞传.NES");

    // mapper23
    // nes_load("../rom/[218]  角色类 - 魂斗罗1.nes");

    // test_pcm();
}


