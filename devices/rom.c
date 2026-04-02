#include "rom.h"
#include <stdio.h>
#include <wchar.h>
#include <windows.h>    // MultiByteToWideChar
#include "../nes/interface.h"

// ========== 新增：UTF-8 转 wchar_t 辅助函数 ==========
static wchar_t* utf8_to_wchar(const char* utf8_str)
{
    if (!utf8_str) return NULL;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wlen <= 0) return NULL;

    wchar_t* wstr = (wchar_t*)malloc(wlen * sizeof(wchar_t));
    if (!wstr) return NULL;

    MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wstr, wlen);
    return wstr;
}

// ========== 修改：rom_get_size ==========
size_t rom_get_size(const char* file)
{
    if (!file)
    {
        LOG("file not found\n");
        return 0;
    }


    wchar_t* wfile = utf8_to_wchar(file);
    if (!wfile)
    {
        LOG("utf8 convert failed\n");
        return 0;
    }
    FILE* fp = _wfopen(wfile, L"rb");
    free(wfile);


    if (!fp)
    {
        LOG("file not open\n");
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        LOG("file not seek end\n");
        fclose(fp);
        return 0;
    }

    size_t size = ftell(fp);
    fclose(fp);
    LOG("File size: %d\n", size);
    return size;
}

// ========== 修改：rom_read ==========
int rom_read(const char* file, uint8_t* romfile)
{
    if (!file || !romfile)
    {
        return -1;
    }


    wchar_t* wfile = utf8_to_wchar(file);
    if (!wfile)
    {
        return -1;
    }
    FILE* fp = _wfopen(wfile, L"rb");
    free(wfile);


    if (!fp)
    {
        return -2; // 打开失败
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return -3;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -4;
    }

    long size = ftell(fp);
    if (size <= 0)
    {
        fclose(fp);
        return -5;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return -6;
    }

    size_t bytes_read = fread(romfile, 1, (size_t)size, fp);
    fclose(fp);

    if (bytes_read != (size_t)size)
    {
        return -7; // 读取不完整
    }

    LOG("File bytes_read: %d\n", bytes_read);
    return bytes_read; // 成功
}
