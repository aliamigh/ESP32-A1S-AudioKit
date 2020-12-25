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
#include "esp_peripherals.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "filter_resample.h"
#include "fatfs_stream.h"
#include "input_key_service.h"

static const char *TAG = "REC_RAW_HTTP";

#define HTTP_BOUNDARY "MY_BOUNDARYY"
#define FILE_NAME     "ahang.wav"
#define HTTP_BUFFER_SIZE 35090

char fileNameWithDir[100];
char keyHeader[100] = "";
char requestHead[200] = "";
char tail[50] = "";
//Send file parts
char buffer[HTTP_BUFFER_SIZE]="";

static bool sendFile(esp_http_client_handle_t client, const char *fileName)
{
    esp_err_t err;
    FILE *file;

    snprintf(fileNameWithDir, sizeof(fileNameWithDir), "/sdcard/%s", fileName);
    //file = SD_open(fileNameWithDir, "rb");
    file = fopen(fileNameWithDir, "rb");
    if(!file)
    {
        ESP_LOGW(TAG, "Could not open file to send through wifi");
        return false;
    }
    unsigned int fileLenght = 0;
    struct stat st;
    if(stat(fileNameWithDir, &st) == 0){
        fileLenght = st.st_size;
	ESP_LOGI(TAG, "length: %d", fileLenght);
    }
    //return true;
    time_t fileTransferStart = 0;
    time(&fileTransferStart);

    char contentType[] = "audio/wav";
    //=============================================================================================
    //key header
    /*strcat(keyHeader, "--");
    strcat(keyHeader, HTTP_BOUNDARY);
    strcat(keyHeader, "\r\n");

    strcat(keyHeader, "Content-Disposition: form-data; name=\"key\"\r\n\r\n");
    strcat(keyHeader, "${filename}\r\n");
    ESP_LOGI(TAG, "keyHeader: %s", keyHeader);*/

    //request header
    strcat(requestHead, "--");
    strcat(requestHead, HTTP_BOUNDARY);
    strcat(requestHead, "\r\n");
    strcat(requestHead, "Content-Disposition: form-data; name=\"file\"; filename=\"");
    strcat(requestHead, fileName);
    strcat(requestHead, "\"\r\n");
    strcat(requestHead, "Content-Type: ");
    strcat(requestHead, contentType);
    strcat(requestHead, "\r\n\r\n");
    ESP_LOGI(TAG, "requestHead: %s", requestHead);

    //request tail
    strcat(tail, "\r\n--");
    strcat(tail, HTTP_BOUNDARY);
    strcat(tail, "--\r\n\r\n");
    ESP_LOGI(TAG, "tail: %s", tail);

    //Set Content-Length
    int contentLength = strlen(requestHead) + fileLenght + strlen(tail);
    //HTTP_BUFFER_SIZE=contentLength;
    ESP_LOGI(TAG, "length: %d", contentLength);
    char lengthStr[10]="";
    sprintf(lengthStr, "%i", contentLength);
    ESP_LOGI(TAG,"%s", lengthStr);
    err = esp_http_client_set_header(client, "Content-Length", lengthStr);

    //=============================================================================================

    err = esp_http_client_open(client, HTTP_BUFFER_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
    }

    ESP_LOGI(TAG, "client connection open");

    //ESP_LOGI(TAG, "keyHeader:\t%d", esp_http_client_write(client, keyHeader, strlen(keyHeader)));
    ESP_LOGI(TAG, "requestHead:\t%d", esp_http_client_write(client, requestHead, strlen(requestHead)));

    unsigned int fileProgress = 0;
    int length = 1;
    while(length)
    {
        length = fread(buffer, sizeof(char), HTTP_BUFFER_SIZE, file);
        if(length)
        {
            int wret = esp_http_client_write(client, buffer, length);
            ESP_LOGI(TAG, "write file:\t%d/%d", wret, length);
            if(wret < 0)
                 return false;

            fileProgress += wret;
            //ESP_LOGI(TAG, "%.*s", length, buffer);
            time_t now;
            time(&now);
            if((float)(now - fileTransferStart) / 1024 > 0)
                ESP_LOGI(TAG, "%u/%u bytes sent total %.02f KiB/s", fileProgress, fileLenght, fileProgress  / (float)(now - fileTransferStart) / 1024);
        }
    }
    fclose(file);
    //finish Multipart request
    ESP_LOGI(TAG, "tail:\t%d", esp_http_client_write(client, tail, strlen(tail)));

    //Get response
    ESP_LOGI(TAG, "fetch_headers:\t%d", esp_http_client_fetch_headers(client));
    ESP_LOGI(TAG, "chunked:\t%d", esp_http_client_is_chunked_response(client));

    int responseLength = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "responseLength:\t%d", responseLength);

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "status:\t%d", status);

    if(responseLength)
    {
        if(esp_http_client_read(client, buffer, responseLength) == responseLength)
            ESP_LOGI(TAG, "Response: %.*s", responseLength, buffer);
    }

    err = esp_http_client_close(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to close HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
    }
    return status == 200 || status == 409;
}

esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static void https_send_file()
{
    //char url[200]= "http://192.168.1.51:8000/upload";
    char url[200]= "http://5.160.218.105/AIBotHardware/";
    ESP_LOGI(TAG, "url = %s", url);
    esp_err_t err;
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handle,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_LOGI(TAG, "client initialized");

    err = esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set method: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
    }

    //Set Multipart header
    char contentTypeStr[50] = "multipart/form-data; boundary=";
    strcat(contentTypeStr, HTTP_BOUNDARY);

    err = esp_http_client_set_header(client, "Content-Type", contentTypeStr);
    err = esp_http_client_set_header(client, "Accept", "*/*");
    //err = esp_http_client_set_header(client, "Accept-Encoding", "gzip,deflate");
    //err = esp_http_client_set_header(client, "Accept-Charset", "ISO-8859-1,utf-8;q=0.7,*;q=0.7");
    //err = esp_http_client_set_header(client, "User-Agent", "Test");
    //err = esp_http_client_set_header(client, "Keep-Alive", "300");
    err = esp_http_client_set_header(client, "Connection", "keep-alive");
    err = esp_http_client_set_header(client, "Accept-Language", "en-us");

    char fileName[] = "ahang.wav";
    ESP_LOGI(TAG, "sending file: %s", fileName);

    sendFile(client, fileName);

    err = esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clean HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
    }

    //vTaskDelete(NULL);
    return;
}

void app_main(void){
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    tcpip_adapter_init();

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);

    // Start wifi & button peripheral
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    // Initialize SD Card peripheral
    audio_board_sdcard_init(set);

    https_send_file();

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    esp_periph_set_destroy(set);
}

