////////////////////////////////////////////////////////////////////
// Deveopment: geoavia@gmail.com

#include "main.h"
#include "index.h"

#include "esp_http_server.h"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char *_PASSPHRASE = "glofish";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    static int64_t last_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->format != PIXFORMAT_JPEG)
            {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted)
                {
                    Serial.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            }
            else
            {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        Serial.printf("MJPG: %uB %ums (%.1ffps)\n", (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    }

    last_frame = 0;
    return res;
}

bool getRequestParams(httpd_req_t *req, int n, String keys[], String values[])
{
    char *buf;
    size_t buf_len;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            for (size_t i = 0; i < n; i++)
            {
                char val[32] = { 0, };
                if (httpd_query_key_value(buf, keys[i].c_str(), val, sizeof(val)) != ESP_OK)
                {
                    free(buf);
                    return false;
                }
                values[i] = String(val);
            }
        }
        else
        {
            free(buf);
            return false;
        }
        free(buf);
    }
    else
    {
        return false;
    }
    return true;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    String values[3];
    String keys[] = { "var" , "val" , "pass" };

    if (!getRequestParams(req, 3, keys, values))
        return httpd_resp_send_404(req);

    if (!values[2].equals(_PASSPHRASE))
        return httpd_resp_send_404(req);
    
    String variable = values[0];
    String val = values[1];
    //sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    Serial.printf("=> var=[%s], val=[%s], pass=[%s]\n", variable.c_str(), val.c_str(), values[2].c_str());

    // feeder related controls
    if (variable.equals("feed")) 
        feedNow(1);
    else if (variable.equals("topled")) 
        setTopLed(val.toInt(), true);
    else if (variable.equals("topledbr")) 
        setTopLedBrightness(val.toInt());
    else if (variable.equals("flashbr")) 
        setFlashBrightness(val.toInt());
    else if (variable.equals("flash")) 
        setFlash(val.toInt());
    else if (variable.equals("feedtimes")) 
        setFeedTimes(val);
    else if (variable.equals("foodamount")) 
        setFoodAmount(val.toInt());
    else if (variable.equals("lighttime"))
    {
        int end = val.indexOf(",");
        if (end >= 0) 
        {
            setTopLedOnTime(val.substring(0, end).toInt());
            setTopLedOnDuration(val.substring(end+1).toInt());
        }
    }
    else if (variable.equals("servo"))
    {
        servoTo(val.toInt());
    }
    // camera related controls
    // else if (variable.equals("contrast"))
    //     res = s->set_contrast(s, val);
    // else if (variable.equals("brightness"))
    //     res = s->set_brightness(s, val);
    // else if (variable.equals("saturation"))
    //     res = s->set_saturation(s, val);
    // else if (variable.equals("lenc"))
    //     res = s->set_lenc(s, val);
    else
        res = -1;

    if (res) 
        return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];

    // sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++ = '{';

    // feeder settings
    p += sprintf(p, "\"topled\":%d,", getTopLed());
    p += sprintf(p, "\"topledbr\":%d,", getTopLedBrightness());
    p += sprintf(p, "\"flashbr\":%d,", getFlashBrightness());
    p += sprintf(p, "\"flash\":%d,", getFlash());
    p += sprintf(p, "\"feedtimes\":\"%s\",", getFeedTimes().c_str());
    p += sprintf(p, "\"foodamount\":%d,", getFoodAmount());
    p += sprintf(p, "\"lighttime\":\"%d,%d\"", getTopLedOnTime(), getTopLedOnDuration());
    // camera settings
    // p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
    // p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
    // p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
    // p += sprintf(p, "\"lenc\":%u", s->status.lenc);
    *p++ = '}';
    *p++ = 0;
    Serial.println(json_response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz, sizeof(index_html_gz));
}

static esp_err_t feedlog_handler(httpd_req_t *req)
{
    String values[1];
    String keys[] = { "clear" };
    if (getRequestParams(req, 1, keys, values))
    {
        clearEvents();
    }
    httpd_resp_set_type(req, "text/html");
    String feedlog = getFeedLog();
    return httpd_resp_send(req, (const char *)feedlog.c_str(), feedlog.length());
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t feedlog_uri = {
        .uri = "/feedlog",
        .method = HTTP_GET,
        .handler = feedlog_handler,
        .user_ctx = NULL};

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &feedlog_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
    }

    config.server_port = 8181;
    config.ctrl_port = 8181;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
