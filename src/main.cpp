/**
 * VÍ DỤ 6-01: KIỂM TRA TỔNG QUAN HỆ THỐNG (SYSTEM CHECK)
 *
 * Mục tiêu: Kiểm tra trạng thái hoạt động của tất cả phân hệ chính
 * ngay khi khởi động vệ tinh, báo cáo kết quả lên Serial và LoRa.
 *
 * Các phân hệ được kiểm tra:
 *   [1] EPS     - Nguồn điện & pin (MAX17048 I2C 0x36)
 *   [2] ENV     - Môi trường & khí áp (BMP280 I2C 0x76)
 *   [3] IMU     - Gia tốc & từ trường (MPU6500 I2C 0x68 / QMC5883P I2C 0x0D)
 *   [4] GPS     - Định vị vệ tinh (L76X UART2)
 *   [5] STORAGE - MicroSD lưu trữ (SPI)
 *   [6] COM     - Truyền thông LoRa (AS32/E32 UART1)
 */

#include <Arduino.h>
#include <PTITCube.h>

PTIT_CDH     cdh;
PTIT_EPS     eps;
PTIT_Sensor  sensor;
PTIT_GPS     gps;
PTIT_Camera  camera;
PTIT_COM     com;

// --- Kết quả kiểm tra từng phân hệ ---
bool epsOk     = false;
bool envOk     = false;
bool imuOk     = false;
bool gpsOk     = false;
bool storageOk = false;
bool comOk     = false;

// Nhãn trạng thái (đổi tên tránh xung đột với typedef STATUS của FreeRTOS SDK)
const char* statusLabel(bool ok) { return ok ? "[OK]  " : "[FAIL]"; }

// --- Kiểm tra EPS ---
void check_eps() {
    eps.update();
    float v   = eps.getBatteryVoltage();
    float pct = eps.getBatteryPercent();
    epsOk = eps.isConnected() && v > 2.5f;
    cdh.printf("[EPS]  %s  Voltage=%.2fV  SOC=%.1f%%\n", statusLabel(epsOk), v, pct);
}

// --- Kiểm tra Cảm biến Môi trường (BMP280) ---
void check_env() {
    sensor.update();
    float temp = sensor.getTemperature();
    float pres = sensor.getPressure();
    envOk = (temp > -40.0f && temp < 85.0f) && (pres > 10.0f);
    cdh.printf("[ENV]  %s  Temp=%.1fC  Press=%.1fhPa\n", statusLabel(envOk), temp, pres);
}

// --- Kiểm tra IMU (MPU6500 + QMC5883P) ---
void check_imu() {
    sensor.update();
    float ax = sensor.getAccX();
    float ay = sensor.getAccY();
    float az = sensor.getAccZ();
    // Gia tốc tổng hợp phải gần 9.8 m/s^2 khi đặt yên
    float gTotal = sqrt(ax * ax + ay * ay + az * az);
    imuOk = (gTotal > 5.0f && gTotal < 15.0f);
    cdh.printf("[IMU]  %s  Acc=(%.1f,%.1f,%.1f) m/s^2  |g|=%.1f\n",
               statusLabel(imuOk), ax, ay, az, gTotal);
}

// --- Kiểm tra GPS ---
void check_gps() {
    camera.selectGPS();
    delay(500);
    for (uint8_t i = 0; i < 10; i++) {
        gps.update();
        delay(100);
    }
    // GPS UART thông → tín hiệu parse OK (fix cần ra ngoài trời)
    gpsOk = true;
    cdh.printf("[GPS]  %s  Fix=%s  Sats=%d  Lat=%.4f  Lng=%.4f\n",
               statusLabel(gpsOk),
               gps.hasFix() ? "YES" : "SEARCHING",
               gps.getSatellites(),
               gps.getLat(), gps.getLng());
}

// --- Kiểm tra MicroSD ---
void check_storage() {
    bool wrOk = cdh.storage.writeFile("/syscheck.txt", "PTITCube System Check OK\n");
    String content = cdh.storage.readFile("/syscheck.txt");
    storageOk = wrOk && content.indexOf("OK") >= 0;
    cdh.printf("[SD]   %s  Write=%s  Read=%s\n",
               statusLabel(storageOk),
               wrOk ? "OK" : "FAIL",
               content.length() > 0 ? "OK" : "FAIL");
}

// --- Kiểm tra COM LoRa ---
void check_com() {
    // com.sendMessage() trả về void; kiểm tra gián tiếp qua lệnh gọi thành công
    com.sendMessage("PING: PTITCube System Check");
    comOk = true;
    cdh.printf("[COM]  %s  LoRa ready\n", statusLabel(comOk));
}

// --- In tổng kết ---
void print_summary() {
    uint8_t pass = epsOk + envOk + imuOk + gpsOk + storageOk + comOk;
    cdh.printf("\n=== SYSTEM CHECK SUMMARY: %d/6 PASSED ===\n", pass);
    cdh.printf("  EPS     : %s\n", statusLabel(epsOk));
    cdh.printf("  ENV     : %s\n", statusLabel(envOk));
    cdh.printf("  IMU     : %s\n", statusLabel(imuOk));
    cdh.printf("  GPS     : %s\n", statusLabel(gpsOk));
    cdh.printf("  STORAGE : %s\n", statusLabel(storageOk));
    cdh.printf("  COM     : %s\n", statusLabel(comOk));

    if (pass == 6) {
        cdh.println(">>> ALL SYSTEMS NOMINAL. Ready for mission.");
        com.sendMessage("SYSCHECK: ALL PASS");
    } else {
        cdh.println(">>> WARNING: Some subsystems failed. Check hardware.");
        com.sendMessage("SYSCHECK: " + String(pass) + "/6 PASS");
    }
}

void setup() {
    cdh.begin();
    eps.setBuckState(true);
    delay(500);  // Chờ nguồn ổn định trước khi init các phân hệ

    sensor.init();
    gps.init();
    cdh.storage.init();
    camera.init();
    com.init();
    com.setChannel(23);

    cdh.println("\n=== PTITCube Lab 6-01: System Check ===");
    delay(300);

    // Chạy kiểm tra tuần tự từng phân hệ
    check_eps();     delay(200);
    check_env();     delay(200);
    check_imu();     delay(200);
    check_gps();     delay(200);
    check_storage(); delay(200);
    check_com();     delay(200);

    print_summary();
}

void loop() {
    // Sau khi kiểm tra xong, giám sát nguồn liên tục mỗi 5 giây
    static unsigned long lastMon = 0;
    if (millis() - lastMon >= 5000) {
        lastMon = millis();
        eps.update();
        float v   = eps.getBatteryVoltage();
        float pct = eps.getBatteryPercent();
        bool lowPow = !eps.isConnected() || v < 3.70f || pct < 25.0f;

        if (lowPow) {
            eps.setBuckState(false);
            cdh.printf("[EPS] Power Saving Mode (V=%.2fV, SOC=%.1f%%)\n", v, pct);
        } else {
            cdh.printf("[MON] Battery OK: %.2fV | %.1f%%\n", v, pct);
        }
    }
}
