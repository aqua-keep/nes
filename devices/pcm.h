#ifndef PCM_H
#define PCM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {



#endif

/**
 * @brief 初始化 PCM 播放系统
 * @param sample_rate 采样率 (如 44100)
 * @return 0 成功, -1 失败
 */
int pcm_init(uint32_t sample_rate);

/**
 * @brief 提交音频数据到播放缓冲区 (流式播放)
 * @param buffer PCM 数据指针 (16位单声道)
 * @param sample_count 样本数 (非字节数)
 */
int pcm_submit_buffer(const uint16_t* buffer, size_t sample_count);

/**
 * @brief 播放 PCM 文件
 * @param filename 文件路径
 * @return 0 成功, -1 失败
 */
int pcm_play_file(const char* filename);

/**
 * @brief 停止播放
 */
void pcm_stop(void);

/**
 * @brief 清理资源
 */
void pcm_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* PCM_H */
