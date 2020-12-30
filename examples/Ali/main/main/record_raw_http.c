#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "wav_encoder.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "filter_resample.h"
#include "fatfs_stream.h"
#include "input_key_service.h"
#include <stdio.h>
#include <stdlib.h>
#include "raw_stream.h"
#include "wav_encoder.h"
#include "esp_vad.h"
#include "mp3_decoder.h"
#include "periph_sdcard.h"
#include "wav_decoder.h"
#include "esp_peripherals.h"
#include "periph_led.h"

#define OUT_PUT_REC_DIR     "/sdcard/REC.wav"
#define DOWNLOADED_FILE     "/sdcard/DOWNLOAD.wav"
#define HTTP_BOUNDARY       "MY_BOUNDARYY"
#define FILE_NAME           OUT_PUT_REC_DIR
#define HTTP_BUFFER_SIZE    8*1024

#define REQUEST_HEAD_SIZE   strlen(requestHead)
#define TAIL_SIZE           strlen(tail)
#define VAD_SAMPLE_RATE_HZ 16000
#define VAD_FRAME_LENGTH_MS 30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)
#define MAX_SEQ   100
#define MIDLE_LEN   MAX_SEQ*VAD_BUFFER_LENGTH



static const char *TAG_HTTP = "HTTP";
static const char *TAG_REC = "REC";
static const char *TAG_PLAY = "PLAY";
static const char *TAG_MAIN = "MAIN";

char requestHead[]  = "--MY_BOUNDARYY\r\nContent-Disposition: form-data; name=\"file\"; filename=\"ahang.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
char tail[] = "\r\n--MY_BOUNDARYY--\r\n";
audio_board_handle_t board_handle;
audio_element_handle_t i2s_stream_reader;
audio_element_info_t info = AUDIO_ELEMENT_INFO_DEFAULT();
esp_periph_handle_t led_handle;
// static const char *TAG = "SDCARD_MP3_EXAMPLE";
esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
esp_periph_set_handle_t set ;



void play_mp3(int music_num)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_reader, i2s_stream_writer, mp3_decoder;


    ESP_LOGI(TAG_PLAY, "[ 1 ] Mount sdcard");
    // Initialize peripherals management
    // esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    // esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    // Initialize SD Card peripheral
    // audio_board_sdcard_init(set);

    ESP_LOGI(TAG_PLAY, "[ 2 ] Start codec chip");
    // audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG_PLAY, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG_PLAY, "[3.1] Create fatfs stream to read data from sdcard");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG_PLAY, "[3.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG_PLAY, "[3.3] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG_PLAY, "[3.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG_PLAY, "[3.5] Link it together [sdcard]-->fatfs_stream-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"file", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG_PLAY, "[3.6] Set up  uri (file as fatfs_stream, mp3 as mp3 decoder, and default output is i2s)");
    char music_root[30];
    sprintf(music_root, "/sdcard/music/%d.mp3",music_num);
    audio_element_set_uri(fatfs_stream_reader, music_root);

    ESP_LOGI(TAG_PLAY, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG_PLAY, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG_PLAY, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG_PLAY, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    ESP_LOGI(TAG_PLAY, "[ 6 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_PLAY, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG_PLAY, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG_PLAY, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG_PLAY, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, fatfs_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, mp3_decoder);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    // esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(mp3_decoder);
    // esp_periph_set_destroy(set);
}


// static const char *TAG = "SDCARD_WAV_EXAMPLE";

void play_wav(void)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_reader, i2s_stream_writer, wav_decoder;

    // esp_log_level_set("*", ESP_LOG_WARN);
    // esp_log_level_set(TAG_PLAY, ESP_LOG_INFO);

    ESP_LOGI(TAG_PLAY, "[ 1 ] Mount sdcard");
    // Initialize peripherals management
    // esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    // esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    // Initialize SD Card peripheral
    // audio_board_sdcard_init(set);

    ESP_LOGI(TAG_PLAY, "[ 2 ] Start codec chip");
    // audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG_PLAY, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG_PLAY, "[3.1] Create fatfs stream to read data from sdcard");

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG_PLAY, "[3.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG_PLAY, "[3.3] Create wav decoder to decode wav file");
    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_decoder = wav_decoder_init(&wav_cfg);

    ESP_LOGI(TAG_PLAY, "[3.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, wav_decoder, "wav");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG_PLAY, "[3.5] Link it together [sdcard]-->fatfs_stream-->wav_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"file", "wav", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG_PLAY, "[3.6] Set up  uri (file as fatfs_stream, wav as wav decoder, and default output is i2s)");
    audio_element_set_uri(fatfs_stream_reader,DOWNLOADED_FILE);


    ESP_LOGI(TAG_PLAY, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG_PLAY, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG_PLAY, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);


    ESP_LOGI(TAG_PLAY, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    ESP_LOGI(TAG_PLAY, "[ 6 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_PLAY, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) wav_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(wav_decoder, &music_info);

            ESP_LOGI(TAG_PLAY, "[ * ] Receive music info from wav decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }
        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG_PLAY, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG_PLAY, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, fatfs_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, wav_decoder);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    // esp_periph_set_stop_all(set);

    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(wav_decoder);
    // esp_periph_set_destroy(set);
}

void reocrd_vad(char * buffer)
{
  
    audio_pipeline_handle_t pipeline1, pipeline2;
    audio_element_handle_t  filter, raw_read;
    audio_element_handle_t i2s_stream_writer, raw_write;


    ESP_LOGI(TAG_REC, "[ 2 ] Create audio pipeline1 for recording");
    audio_pipeline_cfg_t pipeline_cfg1 = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline1 = audio_pipeline_init(&pipeline_cfg1);
    mem_assert(pipeline1);
 

    ESP_LOGI(TAG_REC, "[2.1] Create i2s stream to read audio data from codec chip");
    i2s_stream_cfg_t i2s_cfg1 = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg1.i2s_config.sample_rate = VAD_SAMPLE_RATE_HZ;
    i2s_cfg1.type = AUDIO_STREAM_READER;
#if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
    i2s_cfg1.i2s_port = 1;
#endif
    i2s_stream_reader = i2s_stream_init(&i2s_cfg1);



    audio_element_getinfo(i2s_stream_reader, &info);


    ESP_LOGI(TAG_REC, "[2.3] Create raw to receive data");
    raw_stream_cfg_t raw_cfg1 = {
        .out_rb_size = 20 * 1024,
        .type = AUDIO_STREAM_READER,
    };
    raw_read = raw_stream_init(&raw_cfg1);


    ESP_LOGI(TAG_REC, "[ 3 ] Register all elements to audio pipeline1");
    audio_pipeline_register(pipeline1, i2s_stream_reader, "i2s");
    // audio_pipeline_register(pipeline1, filter, "filter");
    audio_pipeline_register(pipeline1, raw_read, "raw");
 
    ESP_LOGI(TAG_REC, "[ 4 ] Link elements together [codec_chip]-->i2s_stream-->filter-->raw-->[VAD]");
    const char *link_tag1[2] = {"i2s", "raw"};
    audio_pipeline_link(pipeline1, &link_tag1[0],2);

 
    ESP_LOGI(TAG_REC, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline1);

    ESP_LOGI(TAG_REC, "[ 6 ] Initialize VAD handle");
    vad_handle_t vad_inst = vad_create(VAD_MODE_2, VAD_SAMPLE_RATE_HZ, VAD_FRAME_LENGTH_MS);

#define rotate_count 20
    int16_t *vad_buff = (int16_t *)malloc(rotate_count*VAD_BUFFER_LENGTH * sizeof(short));
    int16_t *vad_buff_2 = (int16_t *)malloc(VAD_BUFFER_LENGTH * sizeof(short));
    if (vad_buff == NULL) {
        ESP_LOGE(TAG_REC, "Memory allocation vad_buff failed!");
        goto abort_speech_detection;
    }

    vad_state_t vad_state= VAD_SILENCE;
    int vad_count=0;
    
    //     esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    // esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
 
    // esp_periph_start(set, led_handle);
    
    while (1) {
        for(vad_count=0;vad_count<rotate_count;vad_count++){
            raw_stream_read(raw_read, (char *)vad_buff+ vad_count*VAD_BUFFER_LENGTH * sizeof(short), VAD_BUFFER_LENGTH * sizeof(short));
        }
        raw_stream_read(raw_read, (char *)vad_buff_2, VAD_BUFFER_LENGTH * sizeof(short));
        vad_state = vad_process(vad_inst,vad_buff_2);
        if (vad_state == VAD_SPEECH) {
            ESP_LOGI(TAG_REC, "SPEECH");
     		periph_led_blink(led_handle, get_green_led_gpio(), 100, 100, true, -1, 0);
            break;
        }
        else{

     		periph_led_blink(led_handle, get_green_led_gpio(), 500, 500, true, -1, 0);
            ESP_LOGI(TAG_REC, "SILECE");

        }
    }
    periph_led_stop(led_handle, get_green_led_gpio());
    int rec_seq=rotate_count+2;
    for(;rec_seq<MAX_SEQ;rec_seq++){
        raw_stream_read(raw_read, (char *)buffer + rec_seq*VAD_BUFFER_LENGTH * sizeof(short) , VAD_BUFFER_LENGTH * sizeof(short));
    }

    memcpy(buffer,vad_buff,rotate_count*VAD_BUFFER_LENGTH * sizeof(short));
    memcpy(buffer,vad_buff_2,VAD_BUFFER_LENGTH * sizeof(short));
 
 free(vad_buff);
 vad_buff=NULL;
 free(vad_buff_2);
 vad_buff_2=NULL;

abort_speech_detection:

    ESP_LOGI(TAG_REC, "[ 7 ] Destroy VAD");
    vad_destroy(vad_inst);

    ESP_LOGI(TAG_REC, "[ 8 ] Stop audio_pipeline and release all resources");
    audio_pipeline_stop(pipeline1);
    audio_pipeline_wait_for_stop(pipeline1);
    audio_pipeline_terminate(pipeline1);

    /* Terminate the pipeline1 before removing the listener */
    audio_pipeline_remove_listener(pipeline1);

    audio_pipeline_unregister(pipeline1, i2s_stream_reader);
    audio_pipeline_unregister(pipeline1, raw_read);

    /* Release all resources */
    audio_pipeline_deinit(pipeline1);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(raw_read);

    // esp_periph_set_stop_all(set);

    // esp_periph_set_destroy(set);



}




/* Example of Voice Activity Detection (VAD)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
 


// #define VAD_SAMPLE_RATE_HZ 16000
// #define VAD_FRAME_LENGTH_MS 30
// #define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)

void raw_play(char * buffer)
{

    audio_pipeline_handle_t  pipeline2;
    audio_element_handle_t i2s_stream_writer, raw_write;

    ESP_LOGI(TAG_REC, "[ 1 ] Start codec chip");
    // audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG_REC, "[ 2 ] Create audio pipeline1 for playing");
    audio_pipeline_cfg_t pipeline_cfg2 = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline2 = audio_pipeline_init(&pipeline_cfg2);
    mem_assert(pipeline2);
 
    ESP_LOGI(TAG_REC, "[2.1] Create i2s stream to play audio data in codec chip");
    i2s_stream_cfg_t i2s_cfg2 = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg2.i2s_config.sample_rate = VAD_SAMPLE_RATE_HZ;
    i2s_cfg2.type = AUDIO_STREAM_WRITER; 
    i2s_stream_writer = i2s_stream_init(&i2s_cfg2);
 
 
    ESP_LOGI(TAG_REC, "[2.3] Create raw to send data");
    raw_stream_cfg_t raw_cfg2 = {
        .out_rb_size = 8 * 1024,
        .type = AUDIO_STREAM_WRITER,
    };
    raw_write = raw_stream_init(&raw_cfg2);
 
    ESP_LOGI(TAG_REC, "[ 3 ] Register all elements to audio pipeline2");
    audio_pipeline_register(pipeline2, raw_write, "raw");
    audio_pipeline_register(pipeline2, i2s_stream_writer, "i2s");
 

    ESP_LOGI(TAG_REC, "[ 4 ] Link elements together raw-->i2s_stream-->[codec_chip]");
    const char *link_tag[2] = {"raw","i2s"};
    audio_pipeline_link(pipeline2, &link_tag[0], 2);

    ESP_LOGI(TAG_REC, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline2);

 
 
    int rec_seq=0;
    for(;rec_seq<MAX_SEQ;rec_seq++){
        raw_stream_write(raw_write, (char *)buffer + rec_seq*VAD_BUFFER_LENGTH * sizeof(short)  , VAD_BUFFER_LENGTH * sizeof(short));
    }
 

// abort_speech_player:

    ESP_LOGI(TAG_REC, "[ 7 ] Destroy VAD");
    // vad_destroy(vad_inst);

 

    audio_pipeline_stop(pipeline2);
    audio_pipeline_wait_for_stop(pipeline2);
    audio_pipeline_terminate(pipeline2);

    /* Terminate the pipeline1 before removing the listener */
    audio_pipeline_remove_listener(pipeline2);
 
    audio_pipeline_unregister(pipeline2, raw_write);
    audio_pipeline_unregister(pipeline2, i2s_stream_writer);
 

    audio_pipeline_deinit(pipeline2);
    audio_element_deinit(raw_write);
    audio_element_deinit(i2s_stream_writer);

}




void raw_wav_file(char * buffer)
{ 

    audio_pipeline_handle_t  pipeline2;
    audio_element_handle_t raw_write, wav_encoder, fatfs_stream_writer;

    ESP_LOGI(TAG_REC, "[ 1 ] Mount sdcard");
    // Initialize peripherals management

    ESP_LOGI(TAG_REC, "[ 1 ] Start codec chip");
    // audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
 
    ESP_LOGI(TAG_REC, "[ 2 ] Create audio pipeline1 for playing");
    audio_pipeline_cfg_t pipeline_cfg2 = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline2 = audio_pipeline_init(&pipeline_cfg2);
    mem_assert(pipeline2);
  
    
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    fatfs_stream_writer = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG_REC, "[3.3] Create wav encoder to encode wav file");
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    wav_encoder = wav_encoder_init(&wav_cfg);




    audio_element_setinfo(fatfs_stream_writer, &info);
 

    ESP_LOGI(TAG_REC, "[2.3] Create raw to send data");
    raw_stream_cfg_t raw_cfg2 = {
        // .out_rb_size = MIDDLE_BUFFER_LENGTH*2,//8 * 1024,
        .out_rb_size = 8 * 1024,//8 * 1024,
        .type = AUDIO_STREAM_WRITER,
    };
    raw_write = raw_stream_init(&raw_cfg2);

    ESP_LOGI(TAG_REC, "[ 3 ] Register all elements to audio pipeline1");
    ESP_LOGI(TAG_REC, "[ 4 ] Link elements together [codec_chip]-->i2s_stream-->raw");

    ESP_LOGI(TAG_REC, "[ 3 ] Register all elements to audio pipeline2");
    audio_pipeline_register(pipeline2, raw_write, "raw");
    audio_pipeline_register(pipeline2, wav_encoder, "wav");
    audio_pipeline_register(pipeline2, fatfs_stream_writer, "fatfs");


    ESP_LOGI(TAG_REC, "[ 4 ] Link elements together raw-->wav-->fatfs");
    const char *link_tag2[3] = {"raw", "wav", "fatfs"};
    audio_pipeline_link(pipeline2, &link_tag2[0], 3);


    audio_element_set_uri(fatfs_stream_writer, OUT_PUT_REC_DIR);



    ESP_LOGI(TAG_REC, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline2);

    int start_record;


        for(start_record=0;start_record<MAX_SEQ;start_record++){

            raw_stream_write(raw_write, (char *)buffer+  start_record*VAD_BUFFER_LENGTH * sizeof(short), VAD_BUFFER_LENGTH * sizeof(short));
        }


 
    ESP_LOGI(TAG_REC, "[ 8 ] Stop audio_pipeline and release all resources");

    audio_pipeline_stop(pipeline2);
    audio_pipeline_wait_for_stop(pipeline2);
    audio_pipeline_terminate(pipeline2);

    /* Terminate the pipeline1 before removing the listener */
    audio_pipeline_remove_listener(pipeline2);


    audio_pipeline_unregister(pipeline2, raw_write);
    audio_pipeline_unregister(pipeline2, fatfs_stream_writer); 

    /* Release all resources */

    audio_pipeline_deinit(pipeline2);
    audio_element_deinit(raw_write);
    audio_element_deinit(fatfs_stream_writer); 
    
}



static bool sendBody(esp_http_client_handle_t client, const char *fileName, int *ahang1Salam2No3, int *ahangNumber)
{
    esp_err_t err;
    FILE *file;
    *ahangNumber = -1;
    *ahang1Salam2No3 = 3;
    unsigned int contentLength = 0;
    unsigned int fileLenght = 0;

    err = esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Failed to set method: %s", esp_err_to_name(err));
        return err;
    }

    //snprintf(fileNameWithDir, sizeof(fileNameWithDir), "/sdcard/%s", fileName);
    //file = SD_open(fileNameWithDir, "rb");
    ESP_LOGI(TAG_HTTP,"%s",fileName);
    file = fopen(fileName, "rb");
    if(!file)
    {
        ESP_LOGW(TAG_HTTP, "Could not open file to send through wifi");
        fclose(file);
        return false;
    }
    
    struct stat st;
    if(stat(fileName, &st) == 0){
        fileLenght = st.st_size;
        ESP_LOGI(TAG_HTTP, "length: %d", fileLenght);
    }else{
        fclose(file);
        return false;
    }

    ESP_LOGI(TAG_HTTP, "requestHead: %s", requestHead);
    ESP_LOGI(TAG_HTTP, "tail: %s", tail);

    //Set Content-Length
    contentLength = REQUEST_HEAD_SIZE + fileLenght + TAIL_SIZE;
    char *httpBuffer = (char*)malloc(fileLenght);
    if (httpBuffer == NULL) {
        ESP_LOGE(TAG_HTTP, "Memory allocation failed!");
        return false;
    }
    char lengthStr[10]="";
    sprintf(lengthStr, "%i", contentLength);
    ESP_LOGI(TAG_HTTP,"length:%s", lengthStr);

    //=============================================================================================
    err = esp_http_client_open(client, contentLength);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        fclose(file);
    free(httpBuffer);
        return false;
    }

    ESP_LOGI(TAG_HTTP, "client connection open");
    int wret = 0, try_cnt = 0;
    do{
        wret = esp_http_client_write(client, requestHead, strlen(requestHead));
        try_cnt++;
    }while(wret<=0 && try_cnt<5);
    if (wret<=0) {
        ESP_LOGE(TAG_HTTP, "Failed to send requestHead");
        fclose(file);
        free(httpBuffer);
        return false;
    }

    int length = 1;
    while(length)
    {
        length = fread(httpBuffer, sizeof(char), fileLenght, file);
        if(length)
        {
            try_cnt = 0;
            do{
                wret = esp_http_client_write(client, httpBuffer, length);
                try_cnt++;
            }while(wret <= 0 && try_cnt <1);

            ESP_LOGI(TAG_HTTP, "upload file:\t%d/%d", wret, length);
            if(wret <= 0){
                ESP_LOGE(TAG_HTTP, "Failed to send packets");
                fclose(file);
            free(httpBuffer);
                return false;
            }
        }
    }
    fclose(file);
    
     free(httpBuffer);
    //finish Multipart request
    try_cnt = 0;
    do{
        wret = esp_http_client_write(client, tail, strlen(tail));
        try_cnt++;
    }while(wret<0 && try_cnt<5);
    if (wret<0) {
        ESP_LOGE(TAG_HTTP, "Failed to send tail");
        return false;
    }

    //Get response
    ESP_LOGI(TAG_HTTP, "fetch_headers:\t%d", esp_http_client_fetch_headers(client));
    ESP_LOGI(TAG_HTTP, "chunked:\t%d", esp_http_client_is_chunked_response(client));

    int responseLength = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG_HTTP, "responseLength:\t%d", responseLength);

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG_HTTP, "status:\t%d", status);

    char ahangNumberStr[2];
    char *value = NULL;
    err = esp_http_client_get_header(client, "Content-Type", &value);
    if ( err!= ESP_OK || value == NULL) {
        ESP_LOGW(TAG_HTTP, "Can't read Content-Type");
        return false;
    }else{
        ESP_LOGI(TAG_HTTP, "%s", value);
    }

    if (status==200){
        if(responseLength<=2){
            *ahang1Salam2No3 = 1;
            ESP_LOGI(TAG_HTTP, "Ahang");
        }else{
            *ahang1Salam2No3 = 2;
            ESP_LOGI(TAG_HTTP, "Salam");
        }
    }else if(status==400 && responseLength==43){
        *ahang1Salam2No3 = 3;
        ESP_LOGI(TAG_HTTP, "No ahang No salam");
    }else{
        *ahang1Salam2No3 = 4;
        ESP_LOGI(TAG_HTTP, "Undef");
        return false;
    }

    if(*ahang1Salam2No3 == 1)
    {
        if(esp_http_client_read(client, ahangNumberStr, responseLength) == responseLength){
            ESP_LOGI(TAG_HTTP, "Response: %.*s\n", responseLength, ahangNumberStr);
            *ahangNumber = atoi(ahangNumberStr);
            ESP_LOGI(TAG_HTTP, "Ahang number: %d\n", *ahangNumber);
        }
    }else if(*ahang1Salam2No3 == 2){
        //memset(httpBuffer, 0, HTTP_BUFFER_SIZE);
        file = fopen(DOWNLOADED_FILE, "wb");
    if(!file){
        ESP_LOGW(TAG_HTTP,"Can't open file");
    }else{
        httpBuffer = (char*)malloc(HTTP_BUFFER_SIZE);
        if (httpBuffer == NULL) {
            ESP_LOGE(TAG_HTTP, "Memory allocation failed!");
            return false;
        }
        while(responseLength>0){
            if(responseLength<HTTP_BUFFER_SIZE)
                length = responseLength;
            else
                length = HTTP_BUFFER_SIZE;

            try_cnt = 0;
            do{
                wret = esp_http_client_read(client, httpBuffer, length);
                try_cnt++;
            }while(wret<0 && try_cnt<5);
            if(wret<0){
                ESP_LOGE(TAG_HTTP, "Failed to recieve packets");
                fclose(file);
                break;
            }else{
                length = fwrite(httpBuffer, sizeof(char), wret, file);   
                ESP_LOGI(TAG_HTTP, "download file:\t%d/%d", wret, length);
                if(length<0){
                    ESP_LOGE(TAG_HTTP, "Failed to write file");
                    fclose(file);
                    break;
                }
                responseLength -= wret;
            }
            
        }
    free(httpBuffer);
    fclose(file);
    }
    }

    err = esp_http_client_close(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Failed to close HTTP connection: %s", esp_err_to_name(err));
    }

    return true;
}

esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            // ESP_LOGI(TAG_HTTP, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_HEADER");
            //printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }*/
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static bool sendFile(const char *fileName, int *ahang1Salam2No3, int *ahangNumber){
    
    esp_err_t err;
    esp_http_client_handle_t client;
    //char url[]= "http://192.168.1.54:8000/upload";
    char url[]= "http://5.160.218.105/AIBotHardware/";
    ESP_LOGI(TAG_HTTP, "url = %s", url);
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handle,
    };
    //for(int i=0;i<5;i++){
    client = esp_http_client_init(&config);
    ESP_LOGI(TAG_HTTP, "client initialized");

    //Set Multipart header
    char contentTypeStr[50] = "multipart/form-data; boundary=";
    strcat(contentTypeStr, HTTP_BOUNDARY);

    err = esp_http_client_set_header(client, "Content-Type", contentTypeStr);
    err = esp_http_client_set_header(client, "Accept", "*/*");
    /*  */
    err = esp_http_client_set_header(client, "Connection", "keep-alive");
    err = esp_http_client_set_header(client, "Accept-Language", "en-us");

    bool state = false;
    if(err==ESP_OK){
        state = sendBody(client, fileName, ahang1Salam2No3, ahangNumber);
    }

    esp_http_client_cleanup(client);
    return state;
}



void recoredeee()
{
 

    ESP_LOGI(TAG_MAIN, "[ 1 ] Start RECORD");

    int16_t *buffer_middle = (int16_t *)malloc(MIDLE_LEN * sizeof(short));

    reocrd_vad( buffer_middle);
    // ESP_LOG_BUFFER_HEXDUMP( TAG, buffer_middle, MIDLE_LEN * sizeof(short), LOG_LOCAL_LEVEL );
    raw_play(buffer_middle);
    raw_wav_file(buffer_middle);
    free(buffer_middle);
    buffer_middle=NULL;
}

void app_main(void){
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG_HTTP, ESP_LOG_INFO);
    esp_log_level_set(TAG_REC, ESP_LOG_INFO);
    esp_log_level_set(TAG_PLAY, ESP_LOG_INFO);
    esp_log_level_set(TAG_MAIN, ESP_LOG_INFO);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG_MAIN, "[ 0 ] tcpip_adapter_init");

    tcpip_adapter_init();

    ESP_LOGI(TAG_MAIN, "[ 0 ] audio_board_init");

    board_handle = audio_board_init();
    // audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG_MAIN, "[ 3 ] periph_wifi_init");

 	set = esp_periph_set_init(&periph_cfg);

    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);

    // Start wifi & button peripheral
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    // Initialize SD Card peripheral
    ESP_LOGI(TAG_MAIN, "[ 3 ] Mount sdcard");

    audio_board_sdcard_init(set);

    ESP_LOGI(TAG_MAIN, "[ 4 ] periph_led_init");
    
    periph_led_cfg_t led_cfg = {
        .led_speed_mode = LEDC_LOW_SPEED_MODE,
        .led_duty_resolution = LEDC_TIMER_10_BIT,
        .led_timer_num = LEDC_TIMER_0,
        .led_freq_hz = 5000,
    };

    led_handle = periph_led_init(&led_cfg);
    esp_periph_start(set, led_handle);

    play_mp3(1);
    
    #define RETRY 3
    bool state;
    while(1){
        recoredeee();

        int ahang1Salam2No3, ahangNumber;
        ESP_LOGI(TAG_MAIN, "sending file");
        for(int i=0;i<RETRY;i++){
        	state=sendFile(OUT_PUT_REC_DIR, &ahang1Salam2No3, &ahangNumber);
        	if(state){
             	ESP_LOGI(TAG_MAIN, "receive ok");

        		break;
        	}
        	else{
             	ESP_LOGE(TAG_MAIN, "receive failed");
        	}

        }
        if (!state){
        	continue;
        }
        if(ahang1Salam2No3==1){
            play_mp3(ahangNumber);
        }
        else if(ahang1Salam2No3==2){
            play_wav();
        }
        else{
             ESP_LOGW(TAG_MAIN, "no salam no ahang");
             ESP_LOGW(TAG_MAIN, "no salam no ahang");
             ESP_LOGW(TAG_MAIN, "no salam no ahang");

        }

    }
    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    esp_periph_set_destroy(set);
}



