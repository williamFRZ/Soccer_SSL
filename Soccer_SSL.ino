#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include "esp_camera.h"

// Set to 1 only for Romeo ESP32-S3 hardware revision V1.0.
// That revision needs the DFRobot_AXP313A library to power the camera.
#define ENABLE_CAMERA_POWER_V1_0 0
#if ENABLE_CAMERA_POWER_V1_0
#include "DFRobot_AXP313A.h"
DFRobot_AXP313A axp;
#endif

// DFRobot Romeo ESP32-S3 (DFR0994) camera pinout.
#define CAM_PWDN_GPIO_NUM  -1
#define CAM_RESET_GPIO_NUM -1
#define CAM_XCLK_GPIO_NUM  45
#define CAM_SIOD_GPIO_NUM  1
#define CAM_SIOC_GPIO_NUM  2
#define CAM_Y9_GPIO_NUM    48
#define CAM_Y8_GPIO_NUM    46
#define CAM_Y7_GPIO_NUM    8
#define CAM_Y6_GPIO_NUM    7
#define CAM_Y5_GPIO_NUM    4
#define CAM_Y4_GPIO_NUM    41
#define CAM_Y3_GPIO_NUM    40
#define CAM_Y2_GPIO_NUM    39
#define CAM_VSYNC_GPIO_NUM 6
#define CAM_HREF_GPIO_NUM  42
#define CAM_PCLK_GPIO_NUM  5

const char *AP_SSID = "Romeo-Camera";
const char *AP_PASSWORD = "SoccerCam1";
constexpr int IMAGE_WIDTH = 320;
constexpr int IMAGE_HEIGHT = 240;
constexpr int SAMPLE_STEP = 2;
constexpr uint8_t MIN_SATURATION_PERCENT = 35;
constexpr uint8_t MIN_VALUE = 30;
constexpr uint32_t MIN_ORANGE_SAMPLES = 8;
constexpr uint32_t MIN_RED_SAMPLES = 8;
constexpr int GRID_WIDTH = (IMAGE_WIDTH + SAMPLE_STEP - 1) / SAMPLE_STEP;
constexpr int GRID_HEIGHT = (IMAGE_HEIGHT + SAMPLE_STEP - 1) / SAMPLE_STEP;
constexpr int GRID_SIZE = GRID_WIDTH * GRID_HEIGHT;

WiFiServer server(80);

enum ColorId : uint8_t {
  C_BLACK,
  C_GRAY,
  C_WHITE,
  C_RED,
  C_ORANGE,
  C_YELLOW,
  C_GREEN,
  C_CYAN,
  C_BLUE,
  C_PURPLE,
  C_PINK,
  COLOR_COUNT
};

const char *COLOR_NAMES[COLOR_COUNT] = {
  "black", "gray", "white", "red", "orange", "yellow",
  "green", "cyan", "blue", "purple", "pink"
};

struct HSV {
  float h;
  uint8_t s;
  uint8_t v;
};

struct ColorRegion {
  uint32_t samples = 0;
  uint32_t sumX = 0;
  uint32_t sumY = 0;
  int minX = 10000;
  int minY = 10000;
  int maxX = -1;
  int maxY = -1;
};

struct FrameAnalysis {
  uint32_t colorSamples[COLOR_COUNT] = {};
  ColorRegion orange;
  ColorRegion red;
  ColorId dominant = C_BLACK;
  float dominantPercent = 0.0f;
};

// Static buffers keep connected-component work off the small loop-task stack.
uint8_t orangeMask[GRID_SIZE];
uint8_t redMask[GRID_SIZE];
uint16_t orangeQueue[GRID_SIZE];

static const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Visao do robo</title>
  <style>
    :root { color-scheme: dark; }
    body { font-family: system-ui, sans-serif; margin: 1.25rem auto; max-width: 820px; padding: 0 1rem; background: #111827; color: #f9fafb; }
    h1 { margin-bottom: .25rem; }
    p { color: #cbd5e1; }
    canvas { display: block; width: 100%; height: auto; background: #000; border: 1px solid #475569; border-radius: 10px; image-rendering: auto; }
    .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: .8rem; margin-top: 1rem; }
    .card { background: #1f2937; border-radius: 10px; padding: .9rem; }
    .label { color: #94a3b8; font-size: .9rem; }
    .value { font-size: 1.25rem; font-weight: 700; margin-top: .25rem; }
    #status { font-size: .9rem; }
  </style>
</head>
<body>
  <h1>Visao do Robo Soccer</h1>
  <p id="status">Conectando a camera...</p>
  <canvas id="view" width="320" height="240"></canvas>
  <section class="cards">
    <div class="card"><div class="label">Cor predominante na imagem</div><div class="value" id="dominant">--</div></div>
    <div class="card"><div class="label">Laranja detectado</div><div class="value" id="orange">--</div></div>
    <div class="card"><div class="label">Vermelho detectado</div><div class="value" id="red">--</div></div>
    <div class="card"><div class="label">Centro do laranja</div><div class="value" id="position">--</div></div>
  </section>
  <p>As caixas mostram as maiores regioes vermelha e laranja. A cor predominante e calculada entre preto, cinza, branco, vermelho, laranja, amarelo, verde, ciano, azul, roxo e rosa.</p>
<script>
const W = 320, H = 240;
const canvas = document.getElementById('view');
const ctx = canvas.getContext('2d', { willReadFrequently: false });
const names = {black:'Preto', gray:'Cinza', white:'Branco', red:'Vermelho', orange:'Laranja', yellow:'Amarelo', green:'Verde', cyan:'Ciano', blue:'Azul', purple:'Roxo', pink:'Rosa'};
const image = ctx.createImageData(W, H);
function drawRgb565(bytes) {
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const p = (y * W + x) * 2;
    // The ESP32 camera framebuffer stores RGB565 most-significant byte first.
    const value = bytes.getUint16(p, false);
    const r5 = (value >> 11) & 31;
    const g6 = (value >> 5) & 63;
    const b5 = value & 31;
    // Mirror horizontally so motion on the left of the camera appears on the left of the page.
    const out = (y * W + (W - 1 - x)) * 4;
    image.data[out] = (r5 << 3) | (r5 >> 2);
    image.data[out + 1] = (g6 << 2) | (g6 >> 4);
    image.data[out + 2] = (b5 << 3) | (b5 >> 2);
    image.data[out + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
}

async function update() {
  try {
    const response = await fetch('/frame?t=' + Date.now(), { cache: 'no-store' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const bytes = await response.arrayBuffer();
    if (bytes.byteLength !== W * H * 2) throw new Error('Quadro RGB565 inesperado: ' + bytes.byteLength + ' bytes');
    drawRgb565(new DataView(bytes));

    const dominant = response.headers.get('X-Dominant') || 'unknown';
    const dominantPct = response.headers.get('X-Dominant-Pct') || '0';
    document.getElementById('dominant').textContent = (names[dominant] || dominant) + ' (' + dominantPct + '%)';

    const visible = response.headers.get('X-Orange-Visible') === '1';
    const count = Number(response.headers.get('X-Orange-Samples') || 0);
    document.getElementById('orange').textContent = visible ? 'Sim (' + count + ' amostras)' : 'Nao detectado';
    const redVisible = response.headers.get('X-Red-Visible') === '1';
    const redCount = Number(response.headers.get('X-Red-Samples') || 0);
    document.getElementById('red').textContent = redVisible ? 'Sim (' + redCount + ' amostras)' : 'Nao detectado';
    if (visible) {
      const cx = Number(response.headers.get('X-Orange-CX'));
      const cy = Number(response.headers.get('X-Orange-CY'));
      const minX = Number(response.headers.get('X-Orange-MINX'));
      const minY = Number(response.headers.get('X-Orange-MINY'));
      const maxX = Number(response.headers.get('X-Orange-MAXX'));
      const maxY = Number(response.headers.get('X-Orange-MAXY'));
      document.getElementById('position').textContent = 'x=' + cx + ', y=' + cy + ' px';
      ctx.strokeStyle = '#ff5a1f';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(minX, minY, Math.max(1, maxX - minX), Math.max(1, maxY - minY));
      ctx.beginPath();
      ctx.moveTo(cx - 5, cy); ctx.lineTo(cx + 5, cy);
      ctx.moveTo(cx, cy - 5); ctx.lineTo(cx, cy + 5);
      ctx.stroke();
      ctx.fillStyle = '#ff5a1f';
      ctx.font = '8px sans-serif';
      ctx.fillText('LARANJA', minX, Math.max(9, minY - 2));
    }
    if (redVisible) {
      const minX = Number(response.headers.get('X-Red-MINX'));
      const minY = Number(response.headers.get('X-Red-MINY'));
      const maxX = Number(response.headers.get('X-Red-MAXX'));
      const maxY = Number(response.headers.get('X-Red-MAXY'));
      ctx.strokeStyle = '#ff3030';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(minX, minY, Math.max(1, maxX - minX), Math.max(1, maxY - minY));
      ctx.fillStyle = '#ff3030';
      ctx.font = '8px sans-serif';
      ctx.fillText('VERMELHO', minX, Math.max(9, minY - 2));
    }
    if (!visible) document.getElementById('position').textContent = '--';
    document.getElementById('status').textContent = 'Camera ativa | ' + W + ' x ' + H;
  } catch (error) {
    document.getElementById('status').textContent = 'Erro de camera/rede: ' + error.message;
  }
  setTimeout(update, 80);
}
update();
</script>
</body>
</html>
)rawliteral";

HSV rgb565ToHSV(uint16_t pixel) {
  const uint8_t r5 = (pixel >> 11) & 0x1F;
  const uint8_t g6 = (pixel >> 5) & 0x3F;
  const uint8_t b5 = pixel & 0x1F;
  const float r = (r5 * 255.0f) / 31.0f;
  const float g = (g6 * 255.0f) / 63.0f;
  const float b = (b5 * 255.0f) / 31.0f;

  float maxChannel = r;
  if (g > maxChannel) maxChannel = g;
  if (b > maxChannel) maxChannel = b;
  float minChannel = r;
  if (g < minChannel) minChannel = g;
  if (b < minChannel) minChannel = b;
  const float delta = maxChannel - minChannel;

  HSV hsv;
  hsv.v = static_cast<uint8_t>(maxChannel);
  hsv.s = maxChannel <= 0.0f ? 0 : static_cast<uint8_t>((delta * 100.0f) / maxChannel);
  if (delta <= 0.0f) {
    hsv.h = 0.0f;
  } else if (maxChannel == r) {
    hsv.h = 60.0f * fmodf((g - b) / delta, 6.0f);
  } else if (maxChannel == g) {
    hsv.h = 60.0f * (((b - r) / delta) + 2.0f);
  } else {
    hsv.h = 60.0f * (((r - g) / delta) + 4.0f);
  }
  if (hsv.h < 0.0f) hsv.h += 360.0f;
  return hsv;
}

ColorId classifyColor(const HSV &hsv) {
  if (hsv.v < MIN_VALUE) return C_BLACK;
  if (hsv.s < MIN_SATURATION_PERCENT) return hsv.v >= 170 ? C_WHITE : C_GRAY;
  if (hsv.h < 14.0f || hsv.h >= 345.0f) return C_RED;
  if (hsv.h < 48.0f) return C_ORANGE;
  if (hsv.h < 70.0f) return C_YELLOW;
  if (hsv.h < 170.0f) return C_GREEN;
  if (hsv.h < 195.0f) return C_CYAN;
  if (hsv.h < 250.0f) return C_BLUE;
  if (hsv.h < 290.0f) return C_PURPLE;
  return C_PINK;
}

ColorRegion findLargestRegion(uint8_t *mask) {
  ColorRegion largest;
  for (int start = 0; start < GRID_SIZE; start++) {
    if (!mask[start]) continue;

    uint32_t head = 0;
    uint32_t tail = 0;
    orangeQueue[tail++] = static_cast<uint16_t>(start);
    mask[start] = 0;
    ColorRegion region;

    while (head < tail) {
      const int index = orangeQueue[head++];
      const int gx = index % GRID_WIDTH;
      const int gy = index / GRID_WIDTH;
      const int x = gx * SAMPLE_STEP;
      const int y = gy * SAMPLE_STEP;
      region.samples++;
      region.sumX += x;
      region.sumY += y;
      if (x < region.minX) region.minX = x;
      if (y < region.minY) region.minY = y;
      if (x > region.maxX) region.maxX = x;
      if (y > region.maxY) region.maxY = y;

      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0) continue;
          const int nx = gx + dx;
          const int ny = gy + dy;
          if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_HEIGHT) continue;
          const int neighbor = ny * GRID_WIDTH + nx;
          if (mask[neighbor]) {
            mask[neighbor] = 0;
            orangeQueue[tail++] = static_cast<uint16_t>(neighbor);
          }
        }
      }
    }

    if (region.samples > largest.samples) largest = region;
  }
  return largest;
}

void analyzeFrame(const camera_fb_t *fb, FrameAnalysis &analysis) {
  for (int i = 0; i < GRID_SIZE; i++) {
    orangeMask[i] = 0;
    redMask[i] = 0;
  }
  for (int y = 0; y < fb->height; y += SAMPLE_STEP) {
    for (int x = 0; x < fb->width; x += SAMPLE_STEP) {
      const size_t offset = (static_cast<size_t>(y) * fb->width + x) * 2;
      // RGB565 bytes in the camera framebuffer are big-endian.
      const uint16_t pixel = (static_cast<uint16_t>(fb->buf[offset]) << 8) | fb->buf[offset + 1];
      const ColorId color = classifyColor(rgb565ToHSV(pixel));
      analysis.colorSamples[color]++;
      if (color == C_ORANGE) {
        const int gx = x / SAMPLE_STEP;
        const int gy = y / SAMPLE_STEP;
        orangeMask[gy * GRID_WIDTH + gx] = 1;
      } else if (color == C_RED) {
        const int gx = x / SAMPLE_STEP;
        const int gy = y / SAMPLE_STEP;
        redMask[gy * GRID_WIDTH + gx] = 1;
      }
    }
  }

  analysis.orange = findLargestRegion(orangeMask);
  analysis.red = findLargestRegion(redMask);

  uint32_t total = 0;
  uint32_t maxSamples = 0;
  for (uint8_t i = 0; i < COLOR_COUNT; i++) {
    total += analysis.colorSamples[i];
    if (analysis.colorSamples[i] > maxSamples) {
      maxSamples = analysis.colorSamples[i];
      analysis.dominant = static_cast<ColorId>(i);
    }
  }
  analysis.dominantPercent = total ? (100.0f * maxSamples / total) : 0.0f;
}

bool writeAll(WiFiClient &client, const uint8_t *data, size_t length) {
  size_t sent = 0;
  while (sent < length && client.connected()) {
    const size_t written = client.write(data + sent, length - sent);
    if (written == 0) {
      delay(1);
      continue;
    }
    sent += written;
  }
  return sent == length;
}

bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2_GPIO_NUM;
  config.pin_d1 = CAM_Y3_GPIO_NUM;
  config.pin_d2 = CAM_Y4_GPIO_NUM;
  config.pin_d3 = CAM_Y5_GPIO_NUM;
  config.pin_d4 = CAM_Y6_GPIO_NUM;
  config.pin_d5 = CAM_Y7_GPIO_NUM;
  config.pin_d6 = CAM_Y8_GPIO_NUM;
  config.pin_d7 = CAM_Y9_GPIO_NUM;
  config.pin_xclk = CAM_XCLK_GPIO_NUM;
  config.pin_pclk = CAM_PCLK_GPIO_NUM;
  config.pin_vsync = CAM_VSYNC_GPIO_NUM;
  config.pin_href = CAM_HREF_GPIO_NUM;
  config.pin_sscb_sda = CAM_SIOD_GPIO_NUM;
  config.pin_sscb_scl = CAM_SIOC_GPIO_NUM;
  config.pin_pwdn = CAM_PWDN_GPIO_NUM;
  config.pin_reset = CAM_RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;   // 320 x 240
  config.jpeg_quality = 12;             // Unused in RGB565 mode.
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Falha ao iniciar a camera: 0x%x\n", err);
    return false;
  }
  return true;
}

void sendPage(WiFiClient &client) {
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ");
  client.print(sizeof(PAGE) - 1);
  client.print("\r\n\r\n");
  writeAll(client, reinterpret_cast<const uint8_t *>(PAGE), sizeof(PAGE) - 1);
}

void sendFrame(WiFiClient &client) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    client.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    return;
  }

  FrameAnalysis analysis{};
  analyzeFrame(fb, analysis);
  const bool orangeVisible = analysis.orange.samples >= MIN_ORANGE_SAMPLES;
  const bool redVisible = analysis.red.samples >= MIN_RED_SAMPLES;
  const ColorRegion &o = analysis.orange;
  const ColorRegion &r = analysis.red;
  // Report coordinates in the mirrored canvas coordinate system.
  const int frameWidth = fb->width;
  const float cx = o.samples ? (frameWidth - 1) - o.sumX / static_cast<float>(o.samples) : 0.0f;
  const float cy = o.samples ? o.sumY / static_cast<float>(o.samples) : 0.0f;
  const int orangeMinX = o.samples ? (frameWidth - 1) - o.maxX : 0;
  const int orangeMaxX = o.samples ? (frameWidth - 1) - o.minX : 0;
  const int redMinX = r.samples ? (frameWidth - 1) - r.maxX : 0;
  const int redMaxX = r.samples ? (frameWidth - 1) - r.minX : 0;

  client.print("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: ");
  client.print(fb->len);
  client.print("\r\nCache-Control: no-store\r\nConnection: close\r\nX-Dominant: ");
  client.print(COLOR_NAMES[analysis.dominant]);
  client.print("\r\nX-Dominant-Pct: ");
  client.print(analysis.dominantPercent, 1);
  client.print("\r\nX-Orange-Visible: ");
  client.print(orangeVisible ? "1" : "0");
  client.print("\r\nX-Orange-Samples: ");
  client.print(o.samples);
  client.print("\r\nX-Red-Visible: ");
  client.print(redVisible ? "1" : "0");
  client.print("\r\nX-Red-Samples: ");
  client.print(r.samples);
  client.print("\r\nX-Orange-CX: ");
  client.print(cx, 0);
  client.print("\r\nX-Orange-CY: ");
  client.print(cy, 0);
  client.print("\r\nX-Orange-MINX: ");
  client.print(orangeVisible ? orangeMinX : 0);
  client.print("\r\nX-Orange-MINY: ");
  client.print(orangeVisible ? o.minY : 0);
  client.print("\r\nX-Orange-MAXX: ");
  client.print(orangeVisible ? orangeMaxX : 0);
  client.print("\r\nX-Orange-MAXY: ");
  client.print(orangeVisible ? o.maxY : 0);
  client.print("\r\nX-Red-MINX: ");
  client.print(redVisible ? redMinX : 0);
  client.print("\r\nX-Red-MINY: ");
  client.print(redVisible ? r.minY : 0);
  client.print("\r\nX-Red-MAXX: ");
  client.print(redVisible ? redMaxX : 0);
  client.print("\r\nX-Red-MAXY: ");
  client.print(redVisible ? r.maxY : 0);
  client.print("\r\n\r\n");

  writeAll(client, fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleOneClient() {
  WiFiClient client = server.available();
  if (!client) return;
  client.setTimeout(2000);

  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
  while (client.connected()) {
    String headerLine = client.readStringUntil('\n');
    if (headerLine == "\r" || headerLine.length() == 0) break;
  }

  if (requestLine.startsWith("GET /frame")) {
    sendFrame(client);
  } else if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /?")) {
    sendPage(client);
  } else {
    client.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
  }
  client.stop();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nVisao com deteccao de cor laranja e cor predominante");

#if ENABLE_CAMERA_POWER_V1_0
  while (axp.begin() != 0) {
    Serial.println("Nao foi possivel iniciar AXP313A; tentando novamente...");
    delay(1000);
  }
  axp.enableCameraPower(axp.eOV2640);
#endif

  Serial.printf("PSRAM: %s\n", psramFound() ? "detectada" : "nao detectada");
  if (!psramFound()) {
    Serial.println("A camera RGB565 precisa de PSRAM para este teste.");
    while (true) delay(1000);
  }
  if (!startCamera()) {
    Serial.println("Confira a revisao da placa e a alimentacao da camera.");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  server.begin();
  Serial.printf("Wi-Fi: %s\nSenha: %s\nAcesse: http://%s/\n",
                AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

void loop() {
  handleOneClient();
  delay(1);
}
