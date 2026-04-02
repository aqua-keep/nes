// display.c
#include "display.h"
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH  256
#define HEIGHT 270
#define PIXELS (WIDTH * HEIGHT)

static HWND g_hwnd = NULL;
static CRITICAL_SECTION g_cs_frame;
static uint8_t* g_rgb888_buffer = NULL; // 存储转换后的 RGB888 数据（实际为BGR顺序）
static volatile int g_new_frame_ready = 0;

DWORD WINAPI DisplayThreadProc(LPVOID lpParam);

// 测试：四色旋转动画
void TestDisplayPattern(void)
{
    const int frames_per_rotation = 4; // 每圈4帧（每250ms一帧）
    const int total_rotations = 1;
    const int total_frames = frames_per_rotation * total_rotations;

    // 四种颜色（RGB888）
    uint8_t colors[4][3] = {
        {255, 0, 0}, // 红
        {0, 255, 0}, // 绿
        {0, 0, 255}, // 蓝
        {255, 255, 255} // 白
    };

    // 分配一整帧的 RGB565 缓冲区
    uint16_t* frame_buffer = (uint16_t*)malloc(WIDTH * HEIGHT * sizeof(uint16_t));
    if (!frame_buffer) return;

    int half_w = WIDTH / 2;
    int half_h = HEIGHT / 2;

    for (int frame = 0; frame < total_frames; ++frame)
    {
        // 当前旋转偏移：0=红在左上, 1=红在右上, 2=红在右下, 3=红在左下
        int offset = frame % 4;

        // 清空帧（可选）
        memset(frame_buffer, 0, WIDTH * HEIGHT * sizeof(uint16_t));

        // 遍历四个象限
        for (int quad = 0; quad < 4; ++quad)
        {
            uint8_t r = colors[(quad + offset) % 4][0];
            uint8_t g = colors[(quad + offset) % 4][1];
            uint8_t b = colors[(quad + offset) % 4][2];
            uint16_t color565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

            // 确定象限区域
            int x_start = (quad % 2) * half_w;
            int x_end = x_start + half_w;
            int y_start = (quad / 2) * half_h;
            int y_end = y_start + half_h;

            // 填充该象限
            for (int y = y_start; y < y_end; ++y)
            {
                for (int x = x_start; x < x_end; ++x)
                {
                    frame_buffer[y * WIDTH + x] = color565;
                }
            }
        }

        // 逐行发送到显示模块（模拟逐行扫描）
        for (int line = 0; line < HEIGHT; ++line)
        {
            display_write(&frame_buffer[line * WIDTH], line);
        }

        Sleep(250); // 每帧 250ms → 4帧/秒 → 1秒转一圈
    }

    free(frame_buffer);
}

void StartDisplayWindow(void)
{
    // 👇 添加这三行（仅用于调试！）
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    InitializeCriticalSection(&g_cs_frame);
    g_rgb888_buffer = (uint8_t*)malloc(WIDTH * HEIGHT * 3); // RGB888: 3 bytes/pixel
    if (!g_rgb888_buffer) return;

    _beginthreadex(NULL, 0, (unsigned(__stdcall *)(void*))DisplayThreadProc, NULL, 0, NULL);
    // 启动测试动画（在主线程中运行，避免窗口未创建就绘图）
    Sleep(100); // 等待窗口创建完成
    // TestDisplayPattern();
}


void display_write(uint16_t* rgb565_data, uint8_t line)
{
    if (!rgb565_data || !g_rgb888_buffer || line >= HEIGHT) return;

    uint8_t* out = g_rgb888_buffer + line * WIDTH * 3; // 计算起始位置

    for (int i = 0; i < WIDTH; ++i)
    {
        // 遍历每列
        uint16_t pixel = rgb565_data[i];
        uint8_t r = (pixel >> 11) & 0x1F;
        uint8_t g = (pixel >> 5) & 0x3F;
        uint8_t b = pixel & 0x1F;

        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);

        out[0] = b; // R
        out[1] = g; // G
        out[2] = r; // B

        out += 3;
    }

    EnterCriticalSection(&g_cs_frame);
    g_new_frame_ready = 1;
    LeaveCriticalSection(&g_cs_frame);

    InvalidateRect(g_hwnd, NULL, FALSE); // 触发重绘
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (g_new_frame_ready && g_rgb888_buffer)
            {
                BITMAPINFO bmi = {0};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = WIDTH;
                bmi.bmiHeader.biHeight = -HEIGHT;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 24;
                bmi.bmiHeader.biCompression = BI_RGB;

                SetDIBitsToDevice(hdc, 0, 0, WIDTH, HEIGHT, 0, 0, 0, HEIGHT, g_rgb888_buffer, &bmi, DIB_RGB_COLORS);
            }
            else
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

DWORD WINAPI DisplayThreadProc(LPVOID lpParam)
{
    const char CLASS_NAME[] = "RGB565DisplayClass";

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME,
        "RGB565 Display",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WIDTH, HEIGHT,
        NULL, NULL, wc.hInstance, NULL
    );

    if (!hwnd) return 1;

    EnterCriticalSection(&g_cs_frame);
    g_hwnd = hwnd;
    LeaveCriticalSection(&g_cs_frame);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    free(g_rgb888_buffer);
    DeleteCriticalSection(&g_cs_frame);
    return 0;
}
