CC = gcc
CFLAGS = -Wall -fPIC -std=c99
LDFLAGS = -shared
LIBS = -lwiringPi -lpthread
# 공유 라이브러리 파일들
SHARED_LIBS = libled.so libsegment.so libbuzzer.so libcds.so
# 메인 실행 파일
TARGET = iot_server
# 기본 타겟
all: $(SHARED_LIBS) $(TARGET)
# 개별 공유 라이브러리 생성
libled.so: libled.c control_device.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
libsegment.so: libsegment.c control_device.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS) -ldl
libbuzzer.so: libbuzzer.c control_device.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
libcds.so: libcds.c control_device.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
# 메인 서버 프로그램 (동적 링크)
$(TARGET): main.c web_server.c control_device.h web_server.h
	$(CC) -o $@ main.c web_server.c -ldl -lpthread

# 웹 디렉토리 생성
web-setup:
	@mkdir -p web
	@echo "web 디렉토리 생성 완료"
	@echo "web/index.html 파일을 수동으로 생성하세요"
# 실행 관련
run-daemon: $(TARGET)
	sudo ./$(TARGET) -d
stop:
	sudo pkill -f $(TARGET)
status:
	ps aux | grep $(TARGET)
# 정리
clean:
	@echo "빌드 파일 정리 중..."
	rm -f $(SHARED_LIBS) $(TARGET)
	@echo "정리 완료"
.PHONY: all clean help web-setup run-daemon stop status
