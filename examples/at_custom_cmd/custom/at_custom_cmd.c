/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_at.h"
#include "esp_http_client.h"
#include "esp_vfs_fat.h"

#define MAX_URL_LENGTH 512
#define MAX_FILE_PATH_LENGTH 256
#define DOWNLOAD_BUFFER_SIZE 4096

typedef struct {
    FILE* file;
    bool download_complete;
    esp_err_t error_code;
    int downloaded_bytes;
} download_context_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    download_context_t *context = (download_context_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                size_t bytes_written = fwrite(evt->data, 1, evt->data_len, context->file);
                if (bytes_written != evt->data_len) {
                    context->error_code = ESP_FAIL;
                    return ESP_FAIL;
                }
                context->downloaded_bytes += bytes_written;
            }
            break;

        case HTTP_EVENT_ERROR:
            context->error_code = ESP_FAIL;
            break;

        case HTTP_EVENT_ON_FINISH:
            context->download_complete = true;
            break;

        default:
            break;
    }
    return ESP_OK;
}

static uint8_t at_httpdownload_setup_cmd(uint8_t para_num)
{
    // Validate parameter count (2 parameters: URL and file path)
    if (para_num != 2) {
        esp_at_port_write_data((uint8_t *)"ERROR: Incorrect number of parameters\r\n", 39);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Parse URL parameter
    uint8_t *url = NULL;
    if (esp_at_get_para_as_str(0, &url) != ESP_AT_PARA_PARSE_RESULT_OK) {
        esp_at_port_write_data((uint8_t *)"ERROR: Invalid URL\r\n", 20);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Validate URL length
    if (strlen((char *)url) > MAX_URL_LENGTH) {
        esp_at_port_write_data((uint8_t *)"ERROR: URL too long\r\n", 21);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Parse file path parameter
    uint8_t *file_path = NULL;
    if (esp_at_get_para_as_str(1, &file_path) != ESP_AT_PARA_PARSE_RESULT_OK) {
        esp_at_port_write_data((uint8_t *)"ERROR: Invalid file path\r\n", 26);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Validate file path length
    if (strlen((char *)file_path) > MAX_FILE_PATH_LENGTH) {
        esp_at_port_write_data((uint8_t *)"ERROR: File path too long\r\n", 27);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Prepare download context
    download_context_t context = {0};
    context.file = fopen((char *)file_path, "wb");
    if (!context.file) {
        esp_at_port_write_data((uint8_t *)"ERROR: Cannot open destination file\r\n", 38);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // HTTP client configuration
    esp_http_client_config_t config = {
            .url = (char *)url,
            .event_handler = http_event_handler,
            .user_data = &context
    };

    // Perform download
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    // Close file
    fclose(context.file);

    // Handle download result
    if (err != ESP_OK || !context.download_complete) {
        unlink((char *)file_path);  // Remove partially downloaded file
        esp_at_port_write_data((uint8_t *)"ERROR: Download failed\r\n", 24);
        esp_http_client_cleanup(client);
        return ESP_AT_RESULT_CODE_ERROR;
    }

    // Cleanup
    esp_http_client_cleanup(client);

    // Prepare and send success response with downloaded bytes
    uint8_t response[64];
    int response_len = snprintf((char *)response, sizeof(response),
                                "OK\r\nDownloaded %d bytes\r\n",
                                context.downloaded_bytes);
    esp_at_port_write_data(response, response_len);

    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_httpdownload_test_cmd(uint8_t *cmd_name)
{
    uint8_t buffer[128] = {0};
    snprintf((char *)buffer, sizeof(buffer),
             "HTTPDOWNLOAD command: AT+HTTPDOWNLOAD=<url>,<file_path>\r\n"
             "Example: AT+HTTPDOWNLOAD=\"https://example.com/file.txt\",\"file.txt\"\r\n");
    esp_at_port_write_data(buffer, strlen((char *)buffer));
    return ESP_AT_RESULT_CODE_OK;
}

static const esp_at_cmd_struct at_custom_cmd[] = {
        {"+HTTPDOWNLOAD", at_httpdownload_test_cmd, NULL, at_httpdownload_setup_cmd, NULL},
};

bool esp_at_custom_cmd_register(void)
{
    return esp_at_custom_cmd_array_regist(at_custom_cmd, sizeof(at_custom_cmd) / sizeof(esp_at_cmd_struct));
}

ESP_AT_CMD_SET_INIT_FN(esp_at_custom_cmd_register, 1);