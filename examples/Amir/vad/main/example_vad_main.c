/* Example of Voice Activity Detection (VAD)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "board.h"
#include "audio_common.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "wav_encoder.h"
#include "fatfs_stream.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "esp_vad.h"

static const char *TAG = "EXAMPLE-VAD";

#define VAD_SAMPLE_RATE_HZ 	16000
#define VAD_FRAME_LENGTH_MS 	30
#define VAD_BUFFER_LENGTH 	(VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000) //480
// #define MIDDLE_BUFFER_LENGTH	50*1024
#define SEQUNCE_BUFF 			70
#define MIDDLE_BUFFER_LENGTH	VAD_BUFFER_LENGTH*SEQUNCE_BUFF

void app_main()
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    audio_pipeline_handle_t pipeline1, pipeline2;
    audio_element_handle_t i2s_stream_reader, filter, raw_read;
    audio_element_handle_t raw_write, wav_encoder, fatfs_stream_writer;

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    // Initialize SD Card peripheral
    audio_board_sdcard_init(set);

    ESP_LOGI(TAG, "[ 1 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[ 2 ] Create audio pipeline1 for recording");
    audio_pipeline_cfg_t pipeline_cfg1 = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline1 = audio_pipeline_init(&pipeline_cfg1);
    mem_assert(pipeline1);

    ESP_LOGI(TAG, "[ 2 ] Create audio pipeline1 for playing");
    audio_pipeline_cfg_t pipeline_cfg2 = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline2 = audio_pipeline_init(&pipeline_cfg2);
    mem_assert(pipeline2);

    ESP_LOGI(TAG, "[2.1] Create i2s stream to read audio data from codec chip");
    i2s_stream_cfg_t i2s_cfg1 = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg1.i2s_config.sample_rate = 16000;
    i2s_cfg1.type = AUDIO_STREAM_READER;

   	i2s_cfg1.multi_out_num = 1;
    i2s_cfg1.task_core = 1;
    i2s_cfg1.i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;


    i2s_stream_reader = i2s_stream_init(&i2s_cfg1);

    /*ESP_LOGI(TAG, "[2.1] Create i2s stream to play audio data in codec chip");
    i2s_stream_cfg_t i2s_cfg2 = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg2.i2s_config.sample_rate = 16000;
    i2s_cfg2.type = AUDIO_STREAM_WRITER;
#if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
    i2s_cfg2.i2s_port = 1;
#endif
    i2s_stream_writer = i2s_stream_init(&i2s_cfg2);*/

    /*ESP_LOGI(TAG, "[2.2] Create filter to resample audio data");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = 48000;
    rsp_cfg.src_ch = 2;
    rsp_cfg.dest_rate = VAD_SAMPLE_RATE_HZ;
    rsp_cfg.dest_ch = 1;
    filter = rsp_filter_init(&rsp_cfg);*/
    
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_WRITER;
    fatfs_stream_writer = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG, "[3.3] Create wav encoder to encode wav file");
    wav_encoder_cfg_t wav_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    wav_encoder = wav_encoder_init(&wav_cfg);




    audio_element_info_t info = AUDIO_ELEMENT_INFO_DEFAULT();
    audio_element_getinfo(i2s_stream_reader, &info);
    audio_element_setinfo(fatfs_stream_writer, &info);


    ESP_LOGI(TAG, "[2.3] Create raw to receive data");
    raw_stream_cfg_t raw_cfg1 = {
        .out_rb_size = MIDDLE_BUFFER_LENGTH*2,//8 * 1024,
        .type = AUDIO_STREAM_READER,
    };
    raw_read = raw_stream_init(&raw_cfg1);

    ESP_LOGI(TAG, "[2.3] Create raw to send data");
    raw_stream_cfg_t raw_cfg2 = {
        .out_rb_size = MIDDLE_BUFFER_LENGTH*2,//8 * 1024,
        .type = AUDIO_STREAM_WRITER,
    };
    raw_write = raw_stream_init(&raw_cfg2);

    ESP_LOGI(TAG, "[ 3 ] Register all elements to audio pipeline1");
    audio_pipeline_register(pipeline1, i2s_stream_reader, "i2s");
    //audio_pipeline_register(pipeline1, filter, "filter");
    audio_pipeline_register(pipeline1, raw_read, "raw");

    ESP_LOGI(TAG, "[ 4 ] Link elements together [codec_chip]-->i2s_stream-->raw");
    const char *link_tag1[2] = {"i2s", "raw"};
    audio_pipeline_link(pipeline1, &link_tag1[0], 2);





    ESP_LOGI(TAG, "[ 3 ] Register all elements to audio pipeline2");
    audio_pipeline_register(pipeline2, raw_write, "raw");
    audio_pipeline_register(pipeline2, wav_encoder, "wav");
    audio_pipeline_register(pipeline2, fatfs_stream_writer, "fatfs");


    ESP_LOGI(TAG, "[ 4 ] Link elements together raw-->wav-->fatfs");
    const char *link_tag2[3] = {"raw", "wav", "fatfs"};
    audio_pipeline_link(pipeline2, &link_tag2[0], 3);


    audio_element_set_uri(fatfs_stream_writer, "/sdcard/rec_out.wav");



    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline1);
    audio_pipeline_run(pipeline2);

    ESP_LOGI(TAG, "[ 6 ] Initialize VAD handle");
    vad_handle_t vad_inst = vad_create(VAD_MODE_4, VAD_SAMPLE_RATE_HZ, VAD_FRAME_LENGTH_MS);

/*    int16_t *vad_buff = (int16_t *)malloc(VAD_BUFFER_LENGTH * sizeof(short));
    if (vad_buff == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed!");
        goto abort_speech_detection;
    }*/

    int16_t *middle_buff = (int16_t *)malloc(MIDDLE_BUFFER_LENGTH * sizeof(short));
    memset(middle_buff,0,MIDDLE_BUFFER_LENGTH * sizeof(short));
    if (middle_buff == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed!");
        goto abort_speech_detection;
    }
    int start_record =0;
vad_state_t vad_state;
    while (1) {
        raw_stream_read(raw_read, (char *)middle_buff, VAD_BUFFER_LENGTH* sizeof(short));
            ESP_LOGW(TAG, "raw_stream_read");
               vad_state = vad_process(vad_inst, middle_buff);
            if (vad_state == VAD_SPEECH) {
                ESP_LOGI(TAG, "Speech detected");
        		raw_stream_read(raw_read,  (char *)middle_buff + VAD_BUFFER_LENGTH*start_record, VAD_BUFFER_LENGTH * sizeof(short));
                start_record++;
                if(start_record>3)
                	break;
            }

        }

        for(;start_record<SEQUNCE_BUFF;start_record++){

        	raw_stream_read(raw_read,  (char *)middle_buff + VAD_BUFFER_LENGTH*start_record, VAD_BUFFER_LENGTH * sizeof(short));
            ESP_LOGW(TAG, "raw_stream_read/.//////////////////");
        }

        for(start_record=0;start_record<SEQUNCE_BUFF;start_record++){

        	raw_stream_write(raw_write, (char *)middle_buff+ VAD_BUFFER_LENGTH*start_record, VAD_BUFFER_LENGTH * sizeof(short));
            ESP_LOGW(TAG, "raw_stream_write/.//////////////////");
        }

        // Feed samples to the VAD process and get the result
        // if(start_record<2){
        //    vad_state = vad_process(vad_inst, middle_buff);
        // }
        // if (vad_state == VAD_SPEECH) {
        //     ESP_LOGI(TAG, "Speech detected");
        //      start_record +=1;
        // }
        // else{
        //     ESP_LOGW(TAG, "SILENCE");

        // }

        // if(start_record > 100) {
        //     ESP_LOGI(TAG, "FINISH recording");
        //     break;
        // }
// start_record++;
// 	       raw_stream_write(raw_write, (char *)middle_buff, VAD_BUFFER_LENGTH * sizeof(short));
//             ESP_LOGI(TAG, "raw_stream_write");

//         if(start_record>5){
//            // raw_stream_write(raw_write, (char *)middle_buff, VAD_BUFFER_LENGTH * sizeof(short));
//             break;
//         }
//     }


abort_speech_detection:

    ESP_LOGI(TAG, "[ 7 ] Destroy VAD");
    vad_destroy(vad_inst);

    ESP_LOGI(TAG, "[ 8 ] Stop audio_pipeline and release all resources");
    audio_pipeline_stop(pipeline1);
    audio_pipeline_wait_for_stop(pipeline1);
    audio_pipeline_terminate(pipeline1);

    audio_pipeline_stop(pipeline2);
    audio_pipeline_wait_for_stop(pipeline2);
    audio_pipeline_terminate(pipeline2);

    /* Terminate the pipeline1 before removing the listener */
    audio_pipeline_remove_listener(pipeline1);
    audio_pipeline_remove_listener(pipeline2);

    audio_pipeline_unregister(pipeline1, i2s_stream_reader);
    //audio_pipeline_unregister(pipeline1, filter);
    audio_pipeline_unregister(pipeline1, raw_read);

    audio_pipeline_unregister(pipeline2, raw_write);
    //audio_pipeline_unregister(pipeline2, i2s_stream_writer);

    /* Release all resources */
    audio_pipeline_deinit(pipeline1);
    audio_element_deinit(i2s_stream_reader);
    //audio_element_deinit(filter);
    audio_element_deinit(raw_read);

    audio_pipeline_deinit(pipeline2);
    audio_element_deinit(raw_write);
    
    // free(middle_buff);
    // middle_buff = NULL;
    //audio_element_deinit(i2s_stream_writer);
    
}

