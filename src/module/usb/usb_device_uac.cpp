#include "config.h"
#include "logger.h"
#include "module/audio/encoder.h"
#include "module/usb/usb_device_uac.h"
#include "module/usb/tusb_config.h"
#include "module/usb/usb_descriptors.h"

#include "tusb.h"

/**********************************************/
/*               音频设备信息回调              */
/**********************************************/
// 音频的输出频率
static const uint32_t supported_freq[] = {16000, 32000, 48000, 96000, 192000};
#define SUPPORTED_FREQ_SIZE TU_ARRAY_SIZE(supported_freq)
static int32_t current_freq = 96000;
// 音频的输出比特
static const uint8_t supported_bit[CFG_TUD_AUDIO_FUNC_1_N_FORMATS] = {
    CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX,
    CFG_TUD_AUDIO_FUNC_1_FORMAT_2_RESOLUTION_RX,
    CFG_TUD_AUDIO_FUNC_1_FORMAT_3_RESOLUTION_RX};
static uint8_t current_bit = supported_bit[0];
// 音频的音量
enum
{
    VOLUME_CTRL_0_DB = 0,
    VOLUME_CTRL_10_DB = 2560,
    VOLUME_CTRL_20_DB = 5120,
    VOLUME_CTRL_30_DB = 7680,
    VOLUME_CTRL_40_DB = 10240,
    VOLUME_CTRL_50_DB = 12800,
    VOLUME_CTRL_60_DB = 15360,
    VOLUME_CTRL_70_DB = 17920,
    VOLUME_CTRL_80_DB = 20480,
    VOLUME_CTRL_90_DB = 23040,
    VOLUME_CTRL_100_DB = 25600,
    VOLUME_CTRL_SILENCE = 0x8000,
};
static int8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];    // +1 for master channel 0
static int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1]; // +1 for master channel 0

// 获取音频信息的回调
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    // 获取音频的频率信息
    if (request->bEntityID == UAC2_ENTITY_CLOCK)
    {
        switch (request->bControlSelector)
        {
        case AUDIO_CS_CTRL_SAM_FREQ:
        {
            switch (request->bRequest)
            {
            case AUDIO_CS_REQ_CUR:
            {
                logger::debugln("Clock get current freq %lu\r\n", current_freq);
                audio_control_cur_4_t curf = {(int32_t)tu_htole32(current_freq)};
                return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
            }
            case AUDIO_CS_REQ_RANGE:
            {
                audio_control_range_4_n_t(SUPPORTED_FREQ_SIZE) rangef = {.wNumSubRanges = tu_htole16(SUPPORTED_FREQ_SIZE)};
                logger::debugln("Clock get %d freq ranges\r\n", SUPPORTED_FREQ_SIZE);
                for (uint8_t i = 0; i < SUPPORTED_FREQ_SIZE; i++)
                {
                    rangef.subrange[i].bMin = (int32_t)supported_freq[i];
                    rangef.subrange[i].bMax = (int32_t)supported_freq[i];
                    rangef.subrange[i].bRes = 0;
                    logger::debugln("Range %d (%d, %d, %d)\r\n", i, (int)rangef.subrange[i].bMin, (int)rangef.subrange[i].bMax, (int)rangef.subrange[i].bRes);
                }
                return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
            }
            }
        }
        case AUDIO_CS_CTRL_CLK_VALID:
        {
            audio_control_cur_1_t cur_valid = {.bCur = 1};
            logger::debugln("Clock get is valid %u\r\n", cur_valid.bCur);
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
        }
        default:
        {
            logger::debugln("Clock get request not supported, entity = %u, selector = %u, request = %u\r\n", request->bEntityID, request->bControlSelector, request->bRequest);
            return false;
        }
        }
    }
    // 获取音频音量信息
    else if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
    {
        switch (request->bControlSelector)
        {
        case AUDIO_FU_CTRL_MUTE:
        {
            audio_control_cur_1_t mute1 = {.bCur = mute[request->bChannelNumber]};
            TU_LOG1("Get channel %u mute %d\r\n", request->bChannelNumber, mute1.bCur);
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
        }
        case AUDIO_FU_CTRL_VOLUME:
        {
            switch (request->bRequest)
            {
            case AUDIO_CS_REQ_RANGE:
            {
                audio_control_range_2_n_t(1) range_vol = {
                    .wNumSubRanges = tu_htole16(1),
                    .subrange = {
                        {.bMin = tu_htole16(-VOLUME_CTRL_50_DB), .bMax = tu_htole16(VOLUME_CTRL_0_DB), .bRes = (256)}},
                };
                logger::debugln("Get channel %u volume range (%d, %d, %u) dB\r\n", request->bChannelNumber, range_vol.subrange[0].bMin / 256, range_vol.subrange[0].bMax / 256, range_vol.subrange[0].bRes / 256);
                return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
            }
            case AUDIO_CS_REQ_CUR:
            {
                audio_control_cur_2_t cur_vol = {
                    .bCur = tu_htole16(volume[request->bChannelNumber])};
                logger::debugln("Get channel %u volume %d dB\r\n", request->bChannelNumber, cur_vol.bCur / 256);
                return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
            }
            }
        }
        default:
        {
            logger::debugln("Feature unit get request not supported, entity = %u, selector = %u, request = %u\r\n",
                            request->bEntityID, request->bControlSelector, request->bRequest);
            return false;
        }
        }
    }

    logger::debugln("Get request not handled, entity = %d, selector = %d, request = %d\r\n",
                    request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

// 设置音频信息的回调
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    // 设置音频频率
    if (request->bEntityID == UAC2_ENTITY_CLOCK)
    {
        switch (request->bControlSelector)
        {
        case AUDIO_CS_CTRL_SAM_FREQ:
        {
            TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
            current_freq = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
            logger::debugln("Clock set current freq: %ld\r\n", current_freq);
            return true;
        }
        default:
        {
            logger::debugln("Clock set request not supported, entity = %u, selector = %u, request = %u\r\n",
                            request->bEntityID, request->bControlSelector, request->bRequest);
            return false;
        }
        }
    }
    // 设置音频音量
    else if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
    {

        switch (request->bControlSelector)
        {
        case AUDIO_FU_CTRL_MUTE:
        {
            TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));
            mute[request->bChannelNumber] = ((audio_control_cur_1_t const *)buf)->bCur;
            logger::debugln("Set speaker channel %d Mute: %d\r\n", request->bChannelNumber, mute[request->bChannelNumber]);
            return true;
        }
        case AUDIO_FU_CTRL_VOLUME:
        {
            TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
            volume[request->bChannelNumber] = ((audio_control_cur_2_t const *)buf)->bCur;
            int volume_db = volume[request->bChannelNumber] / 256; // Convert to dB
            int volume = (volume_db + 50) * 2;                     // Map to range 0 to 100
            logger::debugln("Set speaker channel %d volume: %d dB (%d)\r\n", request->bChannelNumber, volume_db, volume);
            return true;
        }
        default:
        {
            logger::debugln("Feature unit set request not supported, entity = %u, selector = %u, request = %u\r\n",
                            request->bEntityID, request->bControlSelector, request->bRequest);
            return false;
        }
        }
    }

    logger::debugln("Set request not handled, entity = %d, selector = %d, request = %d\r\n",
                    request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

// 音频流格式变换回调
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    logger::debugln("Set interface %d alt %d\r\n", itf, alt);

    // Clear buffer when streaming format is changed
    if (alt != 0)
    {
        current_bit = supported_bit[alt - 1];
    }

    // if (s_uac_device->mic_itf_num == itf && alt != 0)
    // {
    //     s_uac_device->mic_data_size = 0;
    //     s_uac_device->mic_resolution = mic_resolutions_per_format[alt - 1];
    //     s_uac_device->mic_active = true;
    //     s_uac_device->mic_bytes_per_ms = s_uac_device->current_sample_rate / 1000 * MIC_CHANNEL_NUM * s_uac_device->mic_resolution / 8;
    //     xTaskNotifyGive(s_uac_device->mic_task_handle);
    //     TU_LOG1("Microphone interface %d-%d opened", itf, alt);
    //     printf("Microphone interface %d-%d opened\n", itf, alt);
    // }
    return true;
}
/**********************************************/
/**********************************************/
/**********************************************/

/**********************************************/
/*                音频流传输回调               */
/**********************************************/

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)itf;
    (void)ep_in;
    (void)cur_alt_setting;

    // tud_audio_write((uint8_t *)test_buffer_audio, (uint16_t)(sampFreq / (TUD_OPT_HIGH_SPEED ? 8000 : 1000) * bytesPerSample));

    // In read world application data flow is driven by I2S clock,
    // both tud_audio_tx_done_pre_load_cb() & tud_audio_tx_done_post_load_cb() are hardly used.
    // For example in your I2S receive callback:
    // void I2S_Rx_Callback(int channel, const void* data, uint16_t samples)
    // {
    //    tud_audio_write_support_ff(channel, data, samples * N_BYTES_PER_SAMPLE * N_CHANNEL_PER_FIFO);
    // }

    tud_audio_write((uint8_t *)audio::encoder::buffer, (uint16_t)(audio::encoder::buffer_size));

    return true;
}

bool tud_audio_tx_done_post_load_cb(uint8_t rhport, uint16_t n_bytes_copied, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)n_bytes_copied;
    (void)itf;
    (void)ep_in;
    (void)cur_alt_setting;

    return true;
}

// 收到关闭音频流信息
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
    uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

    // if (mic_itf_num == itf && alt == 0)
    // {
    //     logger::debugln("Microphone interface closed");
    //     mic_data_size = 0;
    //     mic_active = false;
    // }

    return true;
}
/**********************************************/
/**********************************************/
/**********************************************/