#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <time.h>
#include "web_server.h"

// HTTP 응답 전송 함수
void send_http_response(int client_fd, const char* status, const char* content_type, const char* body) {
    char response[4096];
    int content_length = strlen(body);
    
    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status, content_type, content_length, body);
    
    send(client_fd, response, strlen(response), 0);
}

// HTML 파일 읽기
char* read_html_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) return NULL;
    
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = malloc(length + 1);
    if (content) {
        fread(content, 1, length, file);
        content[length] = '\0';
    }
    
    fclose(file);
    return content;
}

// JSON 응답 생성
void send_json_response(int client_fd, const char* command, const char* response) {
    char json_body[2048];
    char escaped_response[1024];
    
    // 응답에서 특수문자 이스케이프
    int j = 0;
    for (int i = 0; response[i] && j < sizeof(escaped_response) - 2; i++) {
        if (response[i] == '"') {
            escaped_response[j++] = '\\';
            escaped_response[j++] = '"';
        } else if (response[i] == '\n') {
            escaped_response[j++] = '\\';
            escaped_response[j++] = 'n';
        } else if (response[i] == '\r') {
            escaped_response[j++] = '\\';
            escaped_response[j++] = 'r';
        } else {
            escaped_response[j++] = response[i];
        }
    }
    escaped_response[j] = '\0';
    
    snprintf(json_body, sizeof(json_body),
        "{\n"
        "  \"command\": \"%s\",\n"
        "  \"response\": \"%s\",\n"
        "  \"timestamp\": %ld\n"
        "}", command, escaped_response, time(NULL));
    
    send_http_response(client_fd, "200 OK", "application/json", json_body);
}

// HTTP 요청인지 확인
int is_http_request(const char* buffer) {
    return (strncmp(buffer, "GET ", 4) == 0 || 
            strncmp(buffer, "POST ", 5) == 0 || 
            strncmp(buffer, "OPTIONS ", 8) == 0);
}

// HTTP 요청 파싱 및 처리
void handle_http_request(int client_fd, char* request) {
    char method[16], path[256], version[16];
    sscanf(request, "%s %s %s", method, path, version);
    
    write_log("HTTP 요청: %s %s", method, path);
    
    // OPTIONS 요청 처리 (CORS)
    if (strcmp(method, "OPTIONS") == 0) {
        send_http_response(client_fd, "200 OK", "text/plain", "");
        return;
    }
    
    // 메인 페이지 요청
    if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        char* html_content = read_html_file("web/index.html");
        if (html_content) {
            send_http_response(client_fd, "200 OK", "text/html; charset=utf-8", html_content);
            free(html_content);
        } else {
            // HTML 파일이 없으면 기본 내용
            const char* default_html = 
                "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>IoT Control</title></head>"
                "<body><h1>IoT 장치 제어</h1><p>web/index.html 파일을 생성하세요.</p>"
                "<button onclick=\"fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},"
                "body:JSON.stringify({command:'HELP'})}).then(r=>r.json()).then(d=>alert(d.response))\">테스트</button></body></html>";
            send_http_response(client_fd, "200 OK", "text/html; charset=utf-8", default_html);
        }
        return;
    }
    
    // API 명령 처리
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/command") == 0) {
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            write_log("HTTP 바디: %s", body_start);
            
            // JSON 파싱
            char* cmd_start = strstr(body_start, "\"command\"");
            if (cmd_start) {
                char* colon = strchr(cmd_start, ':');
                if (colon) {
                    char* quote_start = strchr(colon, '"');
                    if (quote_start) {
                        quote_start++;
                        char* quote_end = strchr(quote_start, '"');
                        if (quote_end) {
                            int cmd_len = quote_end - quote_start;
                            if (cmd_len > 0 && cmd_len < 256) {
                                char command[256] = {0};
                                strncpy(command, quote_start, cmd_len);
                                
                                write_log("파싱된 명령어: [%s]", command);
                                
                                char response[MAX_RESPONSE_SIZE] = {0};
                                process_command(command, response, sizeof(response));
                                
                                write_log("명령 응답: [%s]", response);
                                send_json_response(client_fd, command, response);
                                return;
                            }
                        }
                    }
                }
            }
        }
        
        write_log("JSON 파싱 실패");
        send_json_response(client_fd, "UNKNOWN", "ERROR: 명령 파싱 실패");
        return;
    }
    
    // 404 에러
    const char* not_found = "<!DOCTYPE html><html><body><h1>404 Not Found</h1></body></html>";
    send_http_response(client_fd, "404 Not Found", "text/html", not_found);
}
