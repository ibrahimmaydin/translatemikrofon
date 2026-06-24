#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <WebServer.h> 
#include "ui.h"
#include "esp_timer.h"
#include "driver/i2s.h" 

// ---------------- WI-FI & SUNUCU AYARLARI ----------------
const char* ssid = "ibo_laptop";     
const char* password = "=q87L537"; 
const char* host = "192.168.137.1"; 
const uint16_t port = 5000;         

String kaynakDil = "tr"; 
String hedefDil = "en";

// ---------------- BUTON PINLERI ----------------
#define UP_PIN     27
#define DOWN_PIN   25
#define LEFT_PIN   14
#define RIGHT_PIN  33
#define OK_PIN     26

// ---------------- MİKROFON PINLERİ (SPH0645) ----------------
#define MIC_BCLK   32
#define MIC_LRCLK  19
#define MIC_DOUT   13  

// ---------------- AMFİ PINLERİ (MAX98357A) ----------------
#define AMP_BCLK   5
#define AMP_LRCLK  21
#define AMP_DIN    22

#define SAMPLE_RATE 16000 

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[240 * 15]; 

lv_group_t *g;

WebServer webServer(80);

// ---------------- DİL KODU DÜZENLEYİCİ ----------------
String dilKodunuDuzenle(String lang) {
    lang.toLowerCase();
    lang.trim();
    if (lang == "en" || lang == "english" || lang == "eng") return "en";
    if (lang == "tr" || lang == "turkce" || lang == "turkish") return "tr";
    return lang.substring(0, 2);
}

// ---------------- LVGL TICK TIMER ----------------
static void lv_tick_task(void *arg) {
    lv_tick_inc(1);
}

// ---------------- DISPLAY FLUSH ----------------
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// ---------------- I2S KURULUMLARI ----------------
void initMicrophone() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), 
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, 
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,  
        .dma_buf_len = 256,  
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = MIC_BCLK,
        .ws_io_num = MIC_LRCLK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_DOUT
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void initAmplifier() {
    i2s_config_t i2s_amp_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), 
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true // Çökmeyi engelleyen sihirli ayar
    };

    i2s_pin_config_t amp_pin_config = {
        .bck_io_num = AMP_BCLK,
        .ws_io_num = AMP_LRCLK,
        .data_out_num = AMP_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_1, &i2s_amp_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &amp_pin_config);
}

// ---------------- SES KAYIT FONKSİYONU ----------------
void writeWavHeader(File file, uint32_t totalAudioLen) {
    uint32_t totalDataLen = totalAudioLen + 36;
    uint32_t byteRate = SAMPLE_RATE * 1 * 2; 

    uint8_t header[44] = {
        'R', 'I', 'F', 'F',
        (uint8_t)(totalDataLen & 0xff), (uint8_t)((totalDataLen >> 8) & 0xff), (uint8_t)((totalDataLen >> 16) & 0xff), (uint8_t)((totalDataLen >> 24) & 0xff),
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0, 
        1, 0, 
        1, 0,        
        (uint8_t)(SAMPLE_RATE & 0xff), (uint8_t)((SAMPLE_RATE >> 8) & 0xff), (uint8_t)((SAMPLE_RATE >> 16) & 0xff), (uint8_t)((SAMPLE_RATE >> 24) & 0xff),
        (uint8_t)(byteRate & 0xff), (uint8_t)((byteRate >> 8) & 0xff), (uint8_t)((byteRate >> 16) & 0xff), (uint8_t)((byteRate >> 24) & 0xff),
        2, 0,        
        16, 0,       
        'd', 'a', 't', 'a',
        (uint8_t)(totalAudioLen & 0xff), (uint8_t)((totalAudioLen >> 8) & 0xff), (uint8_t)((totalAudioLen >> 16) & 0xff), (uint8_t)((totalAudioLen >> 24) & 0xff)
    };
    file.seek(0);
    file.write(header, 44);
}

void recordAudio(const char* path, uint32_t max_duration_sec) {
    Serial.println("\n>>> MIKROFON ACILDI! (Maks 15 Sn) <<<");
    
    File file = LittleFS.open(path, "w");
    if(!file) {
        Serial.println("Hata: Kayit dosyasi acilamadi!");
        return;
    }

    uint8_t emptyHeader[44] = {0};
    file.write(emptyHeader, 44); 

    uint32_t totalSamplesNeeded = SAMPLE_RATE * max_duration_sec;
    uint32_t samplesRecorded = 0;
    uint32_t bytesWritten = 0;

    const size_t chunkSamples = 256;
    int32_t i2s_raw_buffer[chunkSamples * 2]; 
    int16_t pcm16_buffer[chunkSamples];
    size_t bytes_read = 0;
    int32_t dc_offset = 0;

    while (samplesRecorded < totalSamplesNeeded) {
        if (digitalRead(OK_PIN) == HIGH) {
            Serial.println(">>> Buton birakildi, kayit bitti! <<<");
            break; 
        }

        esp_err_t res = i2s_read(I2S_NUM_0, i2s_raw_buffer, sizeof(i2s_raw_buffer), &bytes_read, portMAX_DELAY);
        
        if (res == ESP_OK && bytes_read > 0) {
            size_t total_samples_read = bytes_read / sizeof(int32_t);
            size_t frames_read = total_samples_read / 2; 
            
            for (size_t i = 0; i < frames_read; i++) {
                int32_t left_sample = i2s_raw_buffer[i * 2];
                int32_t right_sample = i2s_raw_buffer[i * 2 + 1];
                
                left_sample <<= 1; 
                right_sample <<= 1;
                
                int16_t left16 = left_sample >> 16;
                int16_t right16 = right_sample >> 16;
                
                int16_t mono_sample = left16 + right16;
                
                dc_offset = (dc_offset * 63 + mono_sample) / 64; 
                int32_t clean_sample = mono_sample - dc_offset;
                clean_sample *= 8; 

                if (clean_sample > 32767) clean_sample = 32767;
                else if (clean_sample < -32768) clean_sample = -32768;
                
                pcm16_buffer[i] = (int16_t)clean_sample;
            }
            
            file.write((uint8_t*)pcm16_buffer, frames_read * sizeof(int16_t));
            bytesWritten += frames_read * sizeof(int16_t);
            samplesRecorded += frames_read;
        }
        lv_timer_handler();
        webServer.handleClient(); 
    }

    writeWavHeader(file, bytesWritten);
    file.close();
}

// ---------------- SUNUCUYA YOLLA VE ÇEVİRİYİ İNDİR ----------------
void transferFile(const char* path) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Hata: Wi-Fi bagli degil!");
        return;
    }

    char buf_lang1[16];
    char buf_lang2[16];
    lv_dropdown_get_selected_str(ui_secim1, buf_lang1, sizeof(buf_lang1));
    lv_dropdown_get_selected_str(ui_secim2, buf_lang2, sizeof(buf_lang2));
    
    kaynakDil = dilKodunuDuzenle(String(buf_lang1));
    hedefDil = dilKodunuDuzenle(String(buf_lang2));

    File file = LittleFS.open(path, "r"); 
    if(!file) {
        Serial.println("Hata: Gonderilecek dosya bulunamadı!");
        return;
    }

    WiFiClient client; 
    client.setTimeout(5000); 

    if (client.connect(host, port)) { 
        Serial.println("Sunucuya baglanildi! Ses yollaniyor..."); 

        String header = kaynakDil + "|" + hedefDil + "|" + String(file.size()) + "\n"; 
        client.print(header); 

        uint8_t buffer[1024]; 
        while(file.available()) { 
            int bytesRead = file.read(buffer, sizeof(buffer)); 
            client.write(buffer, bytesRead); 
            lv_timer_handler(); 
        }
        client.flush();
        Serial.println("Dosya gonderildi. Sunucunun cevabi bekleniyor..."); 
        
        unsigned long timeout = millis();
        while(!client.available() && millis() - timeout < 15000) { 
            delay(10); 
            lv_timer_handler(); 
        }

        if (client.available()) {
            Serial.println("Sunucudan cevrilen ses indiriliyor...");
            
            File outFile = LittleFS.open("/cevrilen_ses.wav", "w");
            if (outFile) {
                // GÜNCELLEME: İndirme işlemi daha kararlı hale getirildi
                uint8_t rx_buf[512];
                unsigned long son_okuma_zamani = millis();
                
                while (client.connected() || client.available()) {
                    if (client.available()) {
                        int bytes_read = client.read(rx_buf, sizeof(rx_buf));
                        outFile.write(rx_buf, bytes_read);
                        son_okuma_zamani = millis(); // Başarılı okumada süreyi sıfırla
                    } else {
                        delay(5);
                        // Eğer 2 saniye boyunca hiç veri gelmezse indirme tamamlandı say
                        if (millis() - son_okuma_zamani > 2000) break;
                    }
                    lv_timer_handler();
                }
                outFile.close();
                Serial.println(">>> Çeviri '/cevrilen_ses.wav' olarak kaydedildi. <<<");
            }
        } else {
            Serial.println("[HATA] Sunucu ceviriyi gonderemedi (Zaman asimi).");
        }
        client.stop(); 
    } else {
        Serial.println("Hata: Sunucuya baglanilamadi!"); 
    }
    file.close(); 
}

// ---------------- İNDİRİLEN ÇEVİRİYİ HOPARLÖRDEN ÇALMA (YENİLENMİŞ MOTOR) ----------------
void playAudio(const char* path) {
    File file = LittleFS.open(path, "r"); 
    
    if (!file || file.size() <= 44) {
        Serial.println("Hata: Calinacak gecerli bir ses bulunamadi!");
        if(file) file.close();
        return;
    }

    Serial.println("\n>>> SES HOPARLÖRDEN ÇALINIYOR <<<");
    
    // 1. Python Pydub'ın oluşturduğu değişken boyutlu başlığı atlamak için gerçek "data" konumunu tarıyoruz!
    bool data_bulundu = false;
    uint8_t header_byte;
    file.seek(0);
    
    // İlk 1000 byte içinde gerçek ses dalgasının başlama noktası olan "data" kelimesini arar
    while (file.position() < 1000 && file.available()) {
        file.read(&header_byte, 1);
        if (header_byte == 'd') {
            uint8_t buf[3];
            file.read(buf, 3);
            if (buf[0] == 'a' && buf[1] == 't' && buf[2] == 'a') {
                file.seek(file.position() + 4); // "data" yazısını ve 4 bytelık boyut bilgisini atla
                data_bulundu = true;
                Serial.println("-> Ses baslangic noktasi basariyla bulundu!");
                break;
            } else {
                file.seek(file.position() - 3); // Geri dönüp aramaya devam et
            }
        }
    }
    
    if (!data_bulundu) {
        Serial.println("-> Standart 44 byte atlanarak caliniyor...");
        file.seek(44); 
    }

    // 2. Sesi Okuma ve Yazılımsal Amfi
    size_t bytes_written;
    int16_t read_buf[256];  // Bellek hizalaması (Alignment) için uint8_t yerine int16_t kullanıldı
    int16_t i2s_buf[512];   // Stereo çıkış (Sol + Sağ)
    
    while (file.available()) {
        int bytes_read = file.read((uint8_t*)read_buf, sizeof(read_buf));
        int samples = bytes_read / 2; 
        
        for (int i = 0; i < samples; i++) {
            // 🔥 YAZILIMSAL AMFİ: Google TTS sesi cılız olduğu için matematiksel olarak 5 KAT güçlendiriyoruz!
            int32_t val = read_buf[i] * 5; 
            
            // Patlamayı ve bozulmayı engelle (Clipping)
            if (val > 32767) val = 32767;      
            if (val < -32768) val = -32768;    
            
            i2s_buf[i*2]     = (int16_t)val; // Sol Kanal
            i2s_buf[i*2 + 1] = (int16_t)val; // Sağ Kanal
        }
        
        i2s_write(I2S_NUM_1, i2s_buf, samples * 4, &bytes_written, portMAX_DELAY);
        lv_timer_handler(); 
    }
    
    // GÜNCELLEME: Hoparlörün son kelimeyi bitirebilmesi için 250ms zaman tanındı
    delay(250); 
    i2s_zero_dma_buffer(I2S_NUM_1); // Amfiyi pürüzsüzce sustur
    file.close();
    Serial.println(">>> OYNATMA BİTTİ <<<");
}


// ---------------- SETUP ----------------
void setup() {
    Serial.begin(115200);
    delay(500); 
    Serial.println("\n=== ESP32 BASLIYOR ===");

    pinMode(UP_PIN, INPUT_PULLUP);
    pinMode(DOWN_PIN, INPUT_PULLUP);
    pinMode(LEFT_PIN, INPUT_PULLUP);
    pinMode(RIGHT_PIN, INPUT_PULLUP);
    pinMode(OK_PIN, INPUT_PULLUP);

    tft.begin();
    tft.setRotation(2); 
    tft.invertDisplay(1); 
    tft.fillScreen(0xFC00); 

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 15);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = my_disp_flush; 
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    ui_init(); 
    lv_obj_set_style_bg_color(ui_Screen1, lv_color_hex(0xFFB300), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Screen1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFB300), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_scr_load(ui_Screen1);

    g = lv_group_create();
    lv_group_set_default(g);

    lv_obj_add_flag(ui_secim1, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(g, ui_secim1);
    lv_obj_add_flag(ui_secim2, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(g, ui_secim2);
    lv_obj_add_flag(ui_speak, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(g, ui_speak);
    lv_obj_add_flag(ui_listen, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(g, ui_listen);
    lv_obj_add_flag(ui_Button3, LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(g, ui_Button3);

    static lv_style_t style_focus;
    lv_style_init(&style_focus);
    lv_style_set_outline_width(&style_focus, 4);
    lv_style_set_outline_color(&style_focus, lv_color_black());
    lv_style_set_outline_opa(&style_focus, LV_OPA_COVER);
    lv_style_set_outline_pad(&style_focus, 2);

    lv_obj_add_style(ui_secim1, &style_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(ui_secim2, &style_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(ui_speak, &style_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(ui_listen, &style_focus, LV_STATE_FOCUSED);
    lv_obj_add_style(ui_Button3, &style_focus, LV_STATE_FOCUSED);

    lv_group_focus_obj(ui_secim1); 

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lv_tick"
    };
    esp_timer_handle_t periodic_timer;
    esp_timer_create(&periodic_timer_args, &periodic_timer);
    esp_timer_start_periodic(periodic_timer, 1000);

    initMicrophone();
    initAmplifier();

    if(!LittleFS.begin(true)){ 
        Serial.println("LittleFS Hatasi!"); 
    }

    WiFi.begin(ssid, password); 
    Serial.println("WiFi'ye baglanma istegi gonderildi...");
    
    // --- BİLGİSAYARDAN SES İNDİRME ROTASI (WEB SERVER) ---
    webServer.on("/cevap", HTTP_GET, []() {
        File file = LittleFS.open("/cevrilen_ses.wav", "r");
        if (!file) {
            webServer.send(404, "text/plain", "Ceviri dosyasi henuz yok. Once SPEAK yapip sunucudan sesi cekin.");
            return;
        }
        webServer.sendHeader("Content-Disposition", "attachment; filename=\"cevrilen_ses.wav\"");
        webServer.streamFile(file, "audio/wav");
        file.close();
    });

    webServer.on("/kayit", HTTP_GET, []() {
        File file = LittleFS.open("/kayit.wav", "r");
        if (!file) {
            webServer.send(404, "text/plain", "Kayit dosyasi henuz yok. Once konusun.");
            return;
        }
        webServer.sendHeader("Content-Disposition", "attachment; filename=\"kayit.wav\"");
        webServer.streamFile(file, "audio/wav");
        file.close();
    });

    webServer.begin();
    
    Serial.println("=== SISTEM HAZIR ===");
}

// ---------------- LOOP ----------------
void loop() {
    lv_timer_handler(); 
    webServer.handleClient(); // Web Server'ı canlı tut

    static bool baglandiYazdi = false;
    if (WiFi.status() == WL_CONNECTED) { 
        if (!baglandiYazdi) {
            Serial.print("Wi-Fi Baglantisi Basarili! IP: ");
            Serial.println(WiFi.localIP());
            baglandiYazdi = true;
        }
    } else {
        if (baglandiYazdi) {
            Serial.println("Wi-Fi baglantisi koptu...");
            baglandiYazdi = false;
        }
    }

    int upState    = digitalRead(UP_PIN);
    int downState  = digitalRead(DOWN_PIN);
    int leftState  = digitalRead(LEFT_PIN);
    int rightState = digitalRead(RIGHT_PIN);
    int okState    = digitalRead(OK_PIN);

    lv_obj_t * focused_obj = lv_group_get_focused(g);

    if (upState == LOW) {
        if (focused_obj != NULL && lv_obj_check_type(focused_obj, &lv_dropdown_class) && lv_dropdown_is_open(focused_obj)) {
            uint16_t sel = lv_dropdown_get_selected(focused_obj);
            if (sel > 0) lv_dropdown_set_selected(focused_obj, sel - 1);
        } else {
            if (focused_obj != ui_secim1 && focused_obj != ui_secim2) {
                lv_group_focus_prev(g);
            }
        }
        delay(250); 
    }

    if (downState == LOW) {
        if (focused_obj != NULL && lv_obj_check_type(focused_obj, &lv_dropdown_class) && lv_dropdown_is_open(focused_obj)) {
            uint16_t sel = lv_dropdown_get_selected(focused_obj);
            uint16_t total = lv_dropdown_get_option_cnt(focused_obj);
            if (sel < total - 1) lv_dropdown_set_selected(focused_obj, sel + 1);
        } else {
            if (focused_obj == ui_secim1 || focused_obj == ui_secim2) {
                lv_group_focus_obj(ui_speak);
            } else {
                lv_group_focus_next(g);
            }
        }
        delay(250); 
    }

    if (leftState == LOW) {
        if (focused_obj == ui_secim2) lv_group_focus_obj(ui_secim1);
        delay(250);
    }

    if (rightState == LOW) {
        if (focused_obj == ui_secim1) lv_group_focus_obj(ui_secim2);
        delay(250);
    }

    if (okState == LOW) {
        if (focused_obj != NULL) {
            if (lv_obj_check_type(focused_obj, &lv_dropdown_class)) {
                if (lv_dropdown_is_open(focused_obj)) lv_dropdown_close(focused_obj);
                else lv_dropdown_open(focused_obj);
            } 
            // SPEAK BUTONU
            else if (focused_obj == ui_speak) {
                recordAudio("/kayit.wav", 15); 
                transferFile("/kayit.wav"); 
                while(digitalRead(OK_PIN) == LOW) { delay(10); webServer.handleClient(); }
            } 
            // LISTEN BUTONU 
            else if (focused_obj == ui_listen) {
                playAudio("/cevrilen_ses.wav"); 
                while(digitalRead(OK_PIN) == LOW) { delay(10); webServer.handleClient(); }
            }
            else {
                lv_event_send(focused_obj, LV_EVENT_CLICKED, NULL);
            }
        }
        delay(250);
    }

    delay(5);
}