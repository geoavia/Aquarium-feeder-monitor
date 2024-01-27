//////////////////////////////////////////////
// Author: geoavia@gmail.com

#include "main.h"

#include "esp_http_client.h"
#include <UrlEncode.h>

void sendMail(String ip)
{

    String surl = "http://geoavia.com/php/send-email.php?sbj=";
    surl += urlEncode("Aquarium Feeder Monitor");
    surl += "&msg=";
    surl += urlEncode("New Address is: http://" + ip + ":8080/");

    esp_http_client_config_t config = {
        .url = surl.c_str()
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK)
    {
        return;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
}

String getGlobalIpAddress()
{
    char buffer[128] = {
        0,
    };
    esp_http_client_config_t config = {
        .url = "http://api.ipify.org/"
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    if ((err = esp_http_client_open(client, 0)) != ESP_OK)
    {
        return "";
    }
    int content_length = esp_http_client_fetch_headers(client);
    int total_read_len = 0, read_len;
    if (total_read_len < content_length && content_length <= 128)
    {
        read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len <= 0)
        {
            ESP_LOGE("getGlobalIpAddress", "Error read data");
        }
        buffer[read_len] = 0;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return String(buffer);
}
