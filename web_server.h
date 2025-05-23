#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "control_device.h"

// 웹 서버 관련 함수 선언
void send_http_response(int client_fd, const char* status, const char* content_type, const char* body);
char* read_html_file(const char* filename);
void send_json_response(int client_fd, const char* command, const char* response);
void handle_http_request(int client_fd, char* request);
int is_http_request(const char* buffer);

// 외부에서 필요한 함수들
extern int process_command(const char* command, char* response, int response_size);
extern void write_log(const char* format, ...);

#endif
