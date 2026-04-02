#include "pcm.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

// 增加缓冲区数量到 4，提高抗抖动能力，防止系统调度导致的卡顿
// 4个缓冲区 x 16ms ≈ 64ms 延迟，对游戏影响极小，但稳定性大幅提升
#define MAX_BUFFERS 4

// --- 内部结构体 ---
static HWAVEOUT hWaveOut = NULL;
static HANDLE hSemaphore = NULL; // 信号量，用于控制并发数量

typedef struct
{
    WAVEHDR hdr;
    char* data; // 实际数据内存
    size_t capacity; // 内存容量
} AudioBuffer;

static AudioBuffer audioBuffers[MAX_BUFFERS];
static volatile bool g_initialized = false;

// --- 音频回调函数 (由系统音频线程调用) ---
void CALLBACK WaveCallback(HWAVEOUT hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD dwParam1, DWORD dwParam2)
{
    if (uMsg == WOM_DONE)
    {
        // 一个缓冲区播放完成了
        // 释放信号量，让主线程可以提交新的数据
        ReleaseSemaphore(hSemaphore, 1, NULL);
    }
}

// --- 内部辅助函数 ---
static void FreeBuffers()
{
    for (int i = 0; i < MAX_BUFFERS; i++)
    {
        if (audioBuffers[i].data)
        {
            free(audioBuffers[i].data);
            audioBuffers[i].data = NULL;
        }
        ZeroMemory(&audioBuffers[i].hdr, sizeof(WAVEHDR));
    }
}

// --- API 实现 ---

int pcm_init(uint32_t sample_rate)
{
    if (g_initialized) return 0;

    // 初始化音频缓冲区结构
    ZeroMemory(audioBuffers, sizeof(audioBuffers));

    // 创建信号量：初始计数为 MAX_BUFFERS，最大计数为 MAX_BUFFERS
    // 作用：控制同时播放/排队的缓冲区数量不超过 MAX_BUFFERS
    hSemaphore = CreateSemaphore(NULL, MAX_BUFFERS, MAX_BUFFERS, NULL);
    if (!hSemaphore) return -1;

    // 配置音频格式：16位深度，单声道，指定采样率
    WAVEFORMATEX wfx;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    // 打开波形音频设备
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)WaveCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
    {
        CloseHandle(hSemaphore);
        return -1;
    }

    g_initialized = true;
    return 0;
}

int pcm_submit_buffer(const uint16_t* buffer, size_t sample_count)
{
    if (!g_initialized) return -1;

    size_t bytesNeeded = sample_count * sizeof(uint16_t);

    // 1. 等待空位
    // 如果当前已有 MAX_BUFFERS 个缓冲区在播放/排队，这里会阻塞，直到有缓冲区播完
    // 这实现了"播放速度控制"，防止模拟器跑得太快导致声音堆积。
    WaitForSingleObject(hSemaphore, INFINITE);

    // 2. 寻找一个空闲的缓冲区结构
    // 由于使用了信号量保护，这里一定有空闲槽位
    int index = -1;
    for (int i = 0; i < MAX_BUFFERS; i++)
    {
        if (audioBuffers[i].data == NULL || !(audioBuffers[i].hdr.dwFlags & WHDR_INQUEUE))
        {
            index = i;
            break;
        }
    }

    // 正常情况下一定有空位
    if (index == -1) return -1;

    AudioBuffer* pBuffer = &audioBuffers[index];

    // 3. 管理内存 (按需扩容)
    if (pBuffer->capacity < bytesNeeded)
    {
        if (pBuffer->data) free(pBuffer->data);
        pBuffer->data = (char*)malloc(bytesNeeded);
        if (!pBuffer->data)
        {
            pBuffer->capacity = 0;
            return -1;
        }
        pBuffer->capacity = bytesNeeded;
    }

    // 4. 拷贝数据
    memcpy(pBuffer->data, buffer, bytesNeeded);

    // 5. 准备并提交给声卡
    WAVEHDR* pHeader = &pBuffer->hdr;
    pHeader->lpData = pBuffer->data;
    pHeader->dwBufferLength = bytesNeeded;
    pHeader->dwFlags = 0; // 重置标志

    // 必须先 Prepare
    if (waveOutPrepareHeader(hWaveOut, pHeader, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
    {
        return -1;
    }

    // 写入声卡队列 (非阻塞，由系统调度播放)
    if (waveOutWrite(hWaveOut, pHeader, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
    {
        waveOutUnprepareHeader(hWaveOut, pHeader, sizeof(WAVEHDR));
        return -1;
    }

    return 0;
}

void pcm_stop(void)
{
    if (hWaveOut)
    {
        waveOutReset(hWaveOut);
    }
}

void pcm_cleanup(void)
{
    if (!g_initialized) return;

    g_initialized = false;

    if (hWaveOut)
    {
        waveOutReset(hWaveOut);
        waveOutClose(hWaveOut);
        hWaveOut = NULL;
    }

    if (hSemaphore)
    {
        CloseHandle(hSemaphore);
        hSemaphore = NULL;
    }

    FreeBuffers();
}

int pcm_play_file(const char* filename)
{
    if (!g_initialized) return -1;

    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;

    // 简单的 WAV 头检测与跳过
    char header[44];
    size_t headerSize = fread(header, 1, 44, fp);

    // 检测是否为 RIFF 格式
    if (headerSize >= 12 && strncmp(header, "RIFF", 4) == 0)
    {
        // 这是一个 WAV 文件，我们已经跳过了头部
    }
    else
    {
        // 不是标准 WAV 头，或者是 RAW PCM，重置指针到文件头
        fseek(fp, 0, SEEK_SET);
    }

#define CHUNK_SAMPLES 1024
    uint16_t tempBuf[CHUNK_SAMPLES];

    while (!feof(fp))
    {
        size_t samplesRead = fread(tempBuf, sizeof(uint16_t), CHUNK_SAMPLES, fp);
        if (samplesRead == 0) break;

        // 复用 pcm_submit_buffer 的阻塞/播放逻辑
        if (pcm_submit_buffer(tempBuf, samplesRead) != 0)
        {
            break; // 播放出错
        }
    }

    fclose(fp);
    return 0;
}

