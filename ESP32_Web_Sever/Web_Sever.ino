#include <WiFi.h>
#include <ArduinoJson.h>

// Cấu hình thông tin mạng WiFi của bạn
const char* ssid = "701";
const char* password = "12345689";

WiFiServer server(80);

// Các biến lưu giá trị nhận từ STM32 qua cổng UART2
float temperature = 0.0;
float humidity = 0.0;
int gas_density = 0;

String inputString = "";
bool stringComplete = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX2 = GPIO 16
  inputString.reserve(200);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  server.begin();
}

void loop() {
  // 1. ĐỌC VÀ PHÂN TÍCH GÓI TIN JSON TỪ STM32 (UART2)
  while (Serial2.available()) {
    char inChar = (char)Serial2.read();
    inputString += inChar;
    if (inChar == '\n') {
      stringComplete = true;
    }
  }

  if (stringComplete) {
    inputString.trim();
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, inputString);
    
    if (!error) {
      temperature = doc["temp"];
      humidity = doc["humid"];
      gas_density = doc["gas"];
      Serial.printf("[UART2] Nhận -> T: %.1f, H: %.1f, G: %d\n", temperature, humidity, gas_density);
    }
    inputString = "";
    stringComplete = false;
  }

  // 2. XỬ LÝ ĐÁP ỨNG WEB SERVER
  WiFiClient client = server.available();
  if (client) {
    String header = "";
    String currentLine = "";
    unsigned long timeout = millis();
    
    while (client.connected() && millis() - timeout < 2000) {
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            // ENDPOINT 1: Trả về JSON cập nhật dữ liệu ngầm
            if (header.indexOf("GET /data") >= 0) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println("Connection: close");
              client.println();
              client.printf("{\"temp\":%.1f,\"humid\":%.1f,\"gas\":%d}", temperature, humidity, gas_density);
              break;
            } 
            
            // ENDPOINT 2: Trả về giao diện HTML
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            
            client.println("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<title>Closed-room Monitor</title>");
            client.println("<script src=\"https://code.highcharts.com/highcharts.js\"></script>");
            
            client.println("<style>");
            client.println("body { font-family: Arial, sans-serif; background-color: #f8f9fa; margin: 0; padding: 15px; color: #333; }");
            client.println(".header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }");
            client.println(".container { display: grid; grid-template-columns: 2fr 1fr; gap: 15px; margin-bottom: 15px; }");
            client.println(".left-panel { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }");
            client.println(".card { background: white; padding: 10px 15px; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }");
            client.println(".card h3 { margin: 0; font-size: 12px; color: #666; text-transform: uppercase; }");
            client.println(".card .value { font-size: 24px; font-weight: bold; margin-top: 5px; }");
            client.println(".temp-card { border-left: 4px solid #dc3545; } .humid-card { border-left: 4px solid #007bff; } .gas-card { border-left: 4px solid #6f42c1; }");
            client.println(".right-panel { display: flex; flex-direction: column; }");
            client.println(".alert-box { text-align: center; padding: 12px; background: white; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); flex-grow: 1; display: flex; flex-direction: column; justify-content: center; align-items: center;}");
            client.println(".alert-box h4 { margin: 5px 0; font-size: 14px; } .alert-box p { font-size: 11px; margin: 0; }");
            client.println("#chart-container { height: 500px; background: white; padding: 15px; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }");
            client.println("</style></head><body>");
            
            client.println("<div class=\"header\"><div><h2 style=\"margin:0; font-size:20px;\">🏠 Closed-room Monitor</h2><small style=\"color:#888; font-size:11px;\">HỆ THỐNG GIÁM SÁT PHÒNG KÍN - ESP32</small></div></div>");
            client.println("<div class=\"container\">");
            client.println("<div class=\"left-panel\">");
            client.println("<div class=\"card temp-card\"><h3>Nhiệt độ</h3><div class=\"value\" id=\"t\">-- °C</div></div>");
            client.println("<div class=\"card humid-card\"><h3>Độ ẩm</h3><div class=\"value\" id=\"h\">-- %</div></div>");
            client.println("<div class=\"card gas-card\"><h3>Nồng độ khí</h3><div class=\"value\" id=\"g\">-- ppm</div></div>");
            client.println("</div>");
            
            client.println("<div class=\"right-panel\"><div class=\"alert-box\" id=\"mbox\">");
            client.println("<h4 id=\"atitle\" style=\"color:#ff9800;\">⚠️ Đang chờ dữ liệu...</h4><p style=\"color:#888;\" id=\"adesc\">Chưa nhận được gói từ STM32.</p></div></div>");
            client.println("</div>");
            
            client.println("<div id=\"chart-container\"></div>");
            
            // --- JAVASCRIPT ĐỒ THỊ ĐÃ XÓA VẠCH DỌC ---
            client.println("<script>");
            client.println("var chart = new Highcharts.Chart({");
            client.println("chart: { renderTo: 'chart-container', type: 'line' },");
            client.println("title: { text: 'ĐỒ THỊ XU HƯỚNG THỜI GIAN THỰC' },");
            
            // Cấu hình trục X: Bỏ toàn bộ đường lưới dọc (gridLineWidth: 0)
            client.println("xAxis: {");
            client.println("  type: 'datetime',");
            client.println("  tickInterval: 60 * 1000,"); // Mốc hiển thị text vẫn cách nhau 1 phút
            client.println("  gridLineWidth: 0,");        // XÓA ĐƯỜNG LƯỚI CHÍNH
            client.println("  minorGridLineWidth: 0,");   // XÓA ĐƯỜNG LƯỚI PHỤ KHỎI XẤU
            client.println("  dateTimeLabelFormats: { minute: '%H:%M:%S' }");
            client.println("},");
            
            // Cấu hình trục Y: Giữ lại vạch ngang đo nhiệt độ (cách nhau 0.2 độ)
            client.println("yAxis: {");
            client.println("  title: { text: 'Nhiệt độ (°C)' },");
            client.println("  tickInterval: 0.2,");
            client.println("  maxRange: 4");
            client.println("},");
            
            client.println("plotOptions: { line: { animation: true, dataLabels: { enabled: false } } },");
            client.println("series: [{ name: 'Nhiệt độ', data: [], color: '#dc3545' }], credits: { enabled: false }");
            client.println("});");
            
            client.println("function updateData() {");
            client.println("  fetch('/data').then(response => response.json()).then(data => {");
            client.println("    document.getElementById('t').innerHTML = data.temp + ' <span style=\"font-size:16px;\">°C</span>';");
            client.println("    document.getElementById('h').innerHTML = data.humid + ' <span style=\"font-size:16px;\">%</span>';");
            client.println("    document.getElementById('g').innerHTML = data.gas + ' <span style=\"font-size:16px;\">ppm</span>';");
            
            client.println("    var mb = document.getElementById('mbox');");
            client.println("    if(data.temp > 38 || data.gas > 800) {");
            client.println("      mb.style.backgroundColor = '#f8d7da'; document.getElementById('atitle').innerText = 'CẢNH BÁO NGUY HIỂM!'; document.getElementById('adesc').innerText = 'Phòng vượt ngưỡng an toàn.';");
            client.println("    } else {");
            client.println("      mb.style.backgroundColor = '#d4edda'; document.getElementById('atitle').innerText = 'Hệ thống ổn định'; document.getElementById('adesc').innerText = 'Dữ liệu STM32 bình thường.';");
            client.println("    }");
            
            client.println("    var time = (new Date()).getTime() + (7 * 3600 * 1000);");
            client.println("    if(chart.series[0].data.length > 240) chart.series[0].addPoint([time, data.temp], true, true);");
            client.println("    else chart.series[0].addPoint([time, data.temp], true, false);");
            client.println("  }).catch(err => { console.log('Lỗi cập nhật AJAX:', err); });");
            client.println("}");
            
            client.println("setInterval(updateData, 500);");
            client.println("</script></body></html>");
            
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    header = "";
    client.stop();
  }
}