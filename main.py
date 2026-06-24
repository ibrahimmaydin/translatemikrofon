import socket
import os
import speech_recognition as sr
from deep_translator import GoogleTranslator
from gtts import gTTS
from pydub import AudioSegment  
import sys

# Windows terminalinde Türkçe karakterlerin düzgün görünmesi için
sys.stdout.reconfigure(encoding='utf-8')

HOST = '0.0.0.0'
PORT = 5000

STT_DIL_HARITASI = {'tr': 'tr-TR', 'en': 'en-US', 'de': 'de-DE', 'fr': 'fr-FR', 'es': 'es-ES'}

def sesi_isle_ve_cevir(dosya_yolu, kaynak_dil, hedef_dil):
    print(f"\n--- SES İŞLEME BAŞLADI ({kaynak_dil.upper()} -> {hedef_dil.upper()}) ---")
    r = sr.Recognizer()
    stt_kodu = STT_DIL_HARITASI.get(kaynak_dil, 'tr-TR')
    
    # 1. ADIM: Gelen ham WAV dosyasını yazıya dök (STT)
    try:
        with sr.AudioFile(dosya_yolu) as source:
            audio_data = r.record(source)
            orijinal_metin = r.recognize_google(audio_data, language=stt_kodu)
            print(f"-> Söylenen ({kaynak_dil}): {orijinal_metin}")
    except sr.UnknownValueError:
        print("[HATA] Google sesinizi anlayamadı. Mikrofon boş veya çok gürültülü veri göndermiş olabilir.")
        return False
    except Exception as e:
        print(f"[HATA] STT Hatası Oluştu: {e}")
        return False

    # 2. ADIM: Yazıyı hedef dile çevir
    try:
        cevirmen = GoogleTranslator(source=kaynak_dil, target=hedef_dil)
        cevrilen_metin = cevirmen.translate(orijinal_metin)
        print(f"-> Çeviri ({hedef_dil}): {cevrilen_metin}")
    except Exception as e:
        print(f"[HATA] Çeviri Hatası: {e}")
        return False

    # 3. ADIM: Çevrilen metni yapay sese (TTS) dönüştür ve amfi için WAV formatına çek
    try:
        tts = gTTS(text=cevrilen_metin, lang=hedef_dil)
        tts.save("temp.mp3")
        
        # MP3 dosyasını amfinin çalabileceği 16kHz, Mono, 16-bit WAV formatına convert ediyoruz
        sound = AudioSegment.from_mp3("temp.mp3")
        # GÜNCELLEME: Amfi ile frekans uyuşmazlığını çözmek için 16000'e çekildi
        sound = sound.set_frame_rate(16000).set_channels(1).set_sample_width(2) 
        sound.export("cevrilen_ses.wav", format="wav")
        
        print("[BAŞARILI] Çeviri sesi 16kHz 16-bit Mono WAV formatına mühürlendi.")
        return True
    except Exception as e:
        print(f"[HATA] Ses Çevrim / FFmpeg Hatası: {e}")
        return False

# --- ÇİFT YÖNLÜ GÜVENLİ SOKET SUNUCUSU ---
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen()
    print(f"Sunucu {PORT} portunda aktif. ESP32'den gerçek ses verisi bekleniyor...")
    
    while True: 
        conn, addr = s.accept()
        with conn:
            print(f"\nBağlantı sağlandı! Cihaz IP: {addr}")
            
            # Başlığı satır sonuna (\n) kadar güvenle oku
            buffer = b""
            while b'\n' not in buffer:
                chunk = conn.recv(1)
                if not chunk:
                    break
                buffer += chunk
            
            if b'\n' in buffer:
                baslik_str = buffer.decode('utf-8').strip()
                try:
                    kaynak, hedef, dosya_boyutu = baslik_str.split('|')
                    dosya_boyutu = int(dosya_boyutu)
                    print(f"Ayarlar Doğrulandı -> {kaynak}->{hedef} | Boyut: {dosya_boyutu} Byte")
                except ValueError:
                    print("[HATA] Başlık formatı geçersiz:", baslik_str)
                    continue
                
                alinan_dosya = 'gelen_ses.wav'
                print("ESP32'den gelen ses dalgaları toplanıyor...")
                
                # Belirtilen dosya boyutu kadar veriyi ağdan eksiksiz topla
                kalan_byte = dosya_boyutu
                with open(alinan_dosya, 'wb') as f:
                    while kalan_byte > 0:
                        oku_boyut = min(4096, kalan_byte)
                        data = conn.recv(oku_boyut)
                        if not data:
                            break
                        f.write(data)
                        kalan_byte -= len(data)
                
                print("Ses aktarımı tamamlandı. Yapay zeka modülleri tetikleniyor...")
                if sesi_isle_ve_cevir(alinan_dosya, kaynak, hedef):
                    if os.path.exists("cevrilen_ses.wav"):
                        print("Hazırlanan WAV cevabı ESP32'ye geri gönderiliyor...")
                        with open("cevrilen_ses.wav", "rb") as f:
                            conn.sendall(f.read())
                        print("Cevap başarıyla iletildi!")
                else:
                    print("[İPTAL] Ses işlenemediği için ESP32'ye cevap basılmadı.")