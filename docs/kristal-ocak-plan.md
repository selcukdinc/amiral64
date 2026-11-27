# Kristal Ocak v2 ve Ses Motoru: Faz Planı

Kapsam:
- Kristal Ocak'ın görselleri, oyun akışı (state flow) ve oynanışı Fitil Arenası seviyesine çıkar.
- Beş yeni mekanik eklenir: çatlak duvar, dinamit, grizu, karanlık + fener, maden arabası.
- Yazılım ses motoru (Aşama A) ve APU v2 donanımı (Aşama B) yapılır.
- Bütün oyunlar yeni ses motoruna geçer.

Her faz kendi başına derlenir ve testleri geçer. Her faz ayrı bir commit olur ve bir sonraki faz bir öncekine dayanır.

---

## Faz 0: Hazırlık ve ölçüm

**Yapılacaklar**
- Bütün oyunların şu anki ekran görüntülerini ve ses kayıtlarını (`apu_log`) referans olarak al.
- Her oyunun ikili dosya boyutunu ve RAM/zero page kullanımını tabloya dök. Ses motorunun her oyunda ne kadar yer kaplayabileceği buradan belli olur.
- README'deki eski Fitil Arenası görselini (`docs/05_fitil.png`) güncel sürümün görüntüsüyle değiştir.

**Bitti sayılması için**
- `tests/run_tests.sh` geçer.
- Boyut tablosu `docs/` altında durur.

---

## Faz 1: Yazılım ses motoru (Aşama A)

**Yapılacaklar**
- `sw/lib/ses.c` ve `ses.h` yazılır. İşi her karede bir kez çağrılan `ses_frame()` yapar:
  - **Müzik:** Tracker tarzı. Desenler ve sıra listesi olur; döngü noktası, tempo ve transpoz ayarlanabilir.
  - **Enstrüman:** Her karede yazılımla volume zarfı (atak, sustain, release), duty dizisi, vibrato, nota kaydırma ve arpej. Akorlar tek kanalda çalınabilir.
  - **Davul seti:** Gürültü kanalında trampet, hi-hat ve crash. Üçgen kanalda perdesi düşen kick.
  - **Efektler:** Efektler de çok adımlı makrolar olur (perde kayması, adım adım ses). Öncelik sistemi vardır: efekt bir kanalı kısa süre ödünç alır, bitince müzik o kanaldan devam eder.
  - **API:** `ses_muzik(n)`, `ses_dur()`, `ses_efekt(n)`, `ses_tempo(t)`, `ses_sessiz(maske)`.
- `k_sfx()` eski haliyle uyumlu kalır. Altta yeni motoru çağırır.
- Ses verisi Python'da yazılır ve araçla C tablolarına çevrilir. Böylece müzik elle tablo yazmadan düzenlenir.
- `tools/apu_model.py` yazılır: APU'nun Python kopyası, `apu_log`'dan `.wav` üretir. Ayrıca bir `make ses-dinle` hedefi eklenir. Karta yüklemeden bilgisayarda dinlenebilir.

**Bitti sayılması için**
- Test şarkısının WAV'ı dinlenebilir.
- Motor yaklaşık 2,5 KB ROM ve 64 bayttan az RAM kullanır.
- Birim testi: Python modeli ile `tb_apu` aynı örnekleri verir.

**Gerçekleşen (Faz 1 sonu)**
- Motor 2060 bayt kod + 338 bayt tablo, 470 bayt RAM ve 6 bayt zero page kullanıyor. Hedeflenen "64 bayttan az RAM" tutmadı: 10 iz (5 müzik + 5 efekt) × iz başına ~45 bayt durum gerekiyor. RAM bol olduğu için bu kabul edildi.
- Motor bir karede ortalama ~6.500, en çok ~13.000 işlemci döngüsü harcıyor (karenin %6'sı / %12'si). Oyun hız testleri değişmeden geçiyor.
- Motor bütün oyunlara girince 128 KB'lık kartuş taştı. Kartuş 256 KB'a (64 × 4 KB BRAM) çıkarıldı; `$0400`'dan yüklenen oyunların sınırı 30 KB'tan 31 KB'a (`$7FFF`'e kadar) genişletildi. Tılsım'ın çizimleri RLE ile sıkıştırıldı (`gfx.py --rle`, `k_vunpack`).

---

## Faz 2: APU v2 donanımı (Aşama B)

**Yapılacaklar** (`hw/apu.vhd`)
- **Geriye uyum:** `$8100–$8111` kayıtları aynen kalır. Eski oyunlar değişmeden çalışır.
- **Sürüm kaydı:** Yeni bir kayıt (`$8112`) sürüm numarası verir. Ses motoru buna bakıp v1 donanımda v2 özelliklerini kapatır.
- **Wave kanalı (5. kanal):** 32 örnek × 4 bit wavetable RAM'i, kendi periyodu ve sesi.
- **Donanım ADSR:** Her kanalda atak, decay, sustain ve release.
- **Sweep birimi:** Kare kanallarında perde kayması.
- **Karıştırıcı:** Kanal başına pan yerine seviye ayarı ve doğrusal olmayan karışım tablosu, yüksek seste bozulmayı azaltır.
- **Daha temiz çıkış:** 2. dereceden delta-sigma ve DC engelleme, hışırtı azalır.
- Ses motoru v2 algılarsa wave kanalını bas ve pad için, donanım zarfını da işlemci yükünü azaltmak için kullanır.
- `tools/apu_model.py` ve emülatör v2'ye güncellenir.

**Bitti sayılması için**
- `tb_apu` yeni kanal ve zarf testleriyle geçer.
- `tb_game` ve `tb_boot` geçer.
- Vivado sentezinde zamanlama tutar.
- Kartta dinleme testi: eski oyunlar ve yeni motor çalışır.

**Gerçekleşen (Faz 2 sonu)**
- Kayıt haritası: `$8112` sürüm (`$A2`), `$8113` çıkış filtresi, `$8114–$8117` wave kanalı, `$8118–$8119` kare kanalların sweep'i, `$811A–$811E` attack/sustain, `$8120–$812F` wave RAM. APU adres yolu 5'ten 6 bite çıktı.
- 2. dereceden delta-sigma yerine tek kutuplu bir alçak geçiren filtre (seçilebilir: kapalı / 15,5 / 7,8 / 3,9 kHz) yapıldı. Gerekçe: 25 MHz'de çalışan 1. dereceden modülatörün gürültüsü zaten duyulan bandın çok üstünde. Kare dalgaların sertliğini ise bu filtre yumuşatıyor. Şarkı `filtre=N` ile seçiyor.
- Karışım `(toplam × ana ses) × 1,125` olarak kaldı, en çok 1023'te kesiliyor. Böylece v1 oyunlarının ses düzeyi değişmiyor.
- Ses motoru donanım ADSR'sini kullanmıyor: zarfları zaten her karede yazılımla yazıyor. Donanım zarfı ve sweep, `k_tone` ile doğrudan ses çalan kodlar (launcher, boot ROM) için duruyor.
- Kartta dinleme testi bu oturumda yapılamadı; FPGA kartı gerekiyor.

---

## Faz 3: Bütün oyunların yeni ses motoruna geçişi

Her oyuna kendi müziği ve efekt seti yazılır:

| Oyun | Müzik | Efektler |
|---|---|---|
| Launcher | Giriş jingle'ı, menü loop'u | Kart kayması, seçim |
| Blok Yağmuru | Ana tema (seviyeyle hızlanır), "tehlike" sürümü | Dönme, kilit, satır silme (1–4 satır farklı) |
| Tılsım | Pazar, kapı, patron ve route temaları | Kart çekme, el değerlendirme, çip sayımı |
| Yıldız Muhafızı | Dalga temaları, patron | Ateş, patlama (gürültü + kick), güç |
| Fitil Arenası | Tur müziği, maç sonu fanfarı | Fitil cızırtısı, zincirleme patlama, power-up |
| Mahalle Olimpiyatı | Her branş için kısa tema | Start silahı, atlama, rekor |
| Koridor 64 | Gerilimli ambiyans loop'u | Ateş, düşman, kapı, hasar |

- Boot ROM jingle'ı da yeni motora alınabilir, ama ROM boyutu kontrol edilir.
- Yer açmak gerekirse bir oyun `$0400`'dan yüklenecek şekilde taşınır.

**Bitti sayılması için**
- Her oyunun testleri geçer.
- Her oyunun WAV'ı dinlenip onaylanır.
- Kartuş (`mkcart`) 128 KB'a sığar.

---

## Faz 4: Kristal Ocak'ın görsel temeli

**Yapılacaklar**
- `tools/art/kristal_art.py` ile bütün çizimler üretilir; `art.txt` elle düzenlenmez.
- **Kahraman sprite olur:** İki hücre arasında piksel piksel kayar. Dört yönde yürüme, kazma ve itme kareleri olur.
- **Zemin:** Toprak 3–4 çeşit olur, boşluğa bakan kenarlarda gölge olur. Duvarlar kabartmalı olur, arka planda mağara duvarı görünür.
- **Kristaller:** Renk döngüsüyle parlar, pırıltı sprite'ı çıkar. Büyük kristal (5 puan) ayrı bir kare olur.
- **Kaya:** Düşmeden bir adım önce sallanır. Yere çarpınca toz parçacığı çıkar ve ekran hafif sarsılır.
- **Kapı:** Açılırken animasyon oynar ve ışık yanıp söner.
- **Gösterge satırı:** Simgeler olur (kristal, kum saati, kalp, puan). Son 20 saniyede yanıp söner.
- **Renk takımları:** Toprak Ocak, Buz Mağarası ve Lav Derinliği için üç palet.

**Bitti sayılması için**
- Mevcut 3 bölüm yeni görsellerle oynanır.
- Ekran görüntüsü alınır, testler geçer.

---

## Faz 5: Oyun akışı (state flow)

```
TITLE ─► OCAK SEÇ ─► BÖLÜM KARTI ─► OYUN ◄──► DURAKLAT (Devam / Yeniden / Menü)
  ▲                                   │  │
  │                                   │  └─► BÖLÜM SONU (süre→puan, 1–3 yıldız) ─► sonraki bölüm
  └──── GAME OVER (skor, rekor) ◄── ÖLÜM ANİMASYONU ◄┘
```

**Yapılacaklar**
- `main()` açık bir durum makinesine dönüşür (`enum durum`). Her durumun kendi giriş ve kare fonksiyonu olur.
- **Başlık ekranı:** Bigtext ile logo, düşen kristaller, ve beklenirse kendi kendine oynayan bir demo (attract).
- **Ocak seç ekranı:** Açılan bölümler, en iyi yıldızlar ve rekor gösterilir. Hepsi `$02E0` kayıt alanında tutulur.
- **Bölüm kartı:** "OCAK 2-3", bölümün adı, hedef kristal sayısı ve yeni mekaniğin tek satırlık tanıtımı.
- **Ölüm animasyonu:** Toz bulutu, kısa bir duraklama, sonra bölüm yeniden başlar.
- **Bölüm sonu:** Kalan süre puana sayılarak eklenir. 1–3 yıldız verilir: kapıdan çıkmak, bütün kristaller ve ölmeden bitirmek.
- **Duraklat menüsü:** Devam, Yeniden başla, Menüye dön.
- **Takılınca yeniden başlama:** A basılı tutulunca bölüm baştan başlar (bir can gider).
- **Girdi:** Erken basılan tuş kaybolmaz, basılı tutunca sürekli yürür. B + yön ile oyuncu yerinden kıpırdamadan kazar ya da toplar.

**Bitti sayılması için**
- Bütün geçişleri dolaşan bir senaryo testi geçer (`test_sw.py`).
- Kayıt alanı yeniden açılışta okunur.

---

## Faz 6: Mekanikler, ilk dalga (çatlak duvar, dinamit)

**Yapılacaklar**
- **Çatlak duvar:** Üstüne düşen kaya duvarı kırar. Kırılma animasyonu ve sesi olur.
- **Dinamit:**
  - Yerden toplanır; göstergede kaç tane olduğu görünür.
  - B + yön ile konur, fitil 3 adım yanar.
  - 3×3 alanı patlatır: kaya, toprak ve çatlak duvar gider, çelik duvar kalır. Patlama kahramanı öldürür.
  - Kristaller patlamada yok olmaz.
- Fizik kodu yeni hücre türleri için düzenlenir. Hücre bayrakları (`MOVED`, `FALLING`) genişletilir.

**Bitti sayılması için**
- Birim senaryoları geçer: kaya duvarı kırar, dinamit zincirleme patlar, kristal korunur.

---

## Faz 7: Mekanikler, ikinci dalga (grizu, maden arabası)

**Yapılacaklar**
- **Grizu:**
  - Bazı toprakların içinde gaz cebi vardır. Hafif renk farkıyla belli olur, dikkatli bakan görür.
  - Kazılınca gaz 4 adım boyunca yayılır, sonra patlar.
  - Patlama dinamitle aynı kodu kullanır.
- **Maden arabası:**
  - Ray karelerinde ileri geri gider.
  - Önündeki kayayı iter.
  - Kahraman arabanın önünde kalırsa ölür. Arabanın üstüne binilmez.
  - Üstüne düşen kaya arabayı durdurur.

**Bitti sayılması için**
- Senaryo testleri geçer: grizu zamanlaması, arabanın kaya itmesi, ezilme.

---

## Faz 8: Mekanikler, üçüncü dalga (karanlık ve fener)

**Yapılacaklar**
- Lav Derinliği bölümleri karanlıktır.
- Fener kahramanın çevresinde yaklaşık 3 hücreyi aydınlatır. Daha uzak hücreler karanlık paletle çizilir, yalnızca kristaller hafifçe parlar.
- Fenerin yağı süre sayacına bağlıdır, azaldıkça aydınlık alan daralır. Yerden yağ şişesi toplanabilir.
- Performans: Tüm haritayı her karede yeniden çizmek yerine yalnızca ışık kenarındaki hücreler güncellenir.

**Bitti sayılması için**
- Bir adım işi bir karede (60 Hz) biter. Emülatörde ölçülür.
- Görsel test geçer.

---

## Faz 9: Bölüm tasarımı (9 bölüm)

| Ocak | Bölüm | Tanıtılan / öne çıkan öğe |
|---|---|---|
| 1 Toprak Ocak | 1-1 | Temel: kaz, topla, kayadan kaç |
| | 1-2 | Kaya itme, sallanan kaya |
| | 1-3 | Çatlak duvar |
| 2 Buz Mağarası | 2-1 | Dinamit |
| | 2-2 | Grizu |
| | 2-3 | Maden arabası |
| 3 Lav Derinliği | 3-1 | Karanlık + fener |
| | 3-2 | Karanlık + grizu + araba |
| | 3-3 | Final: hepsi bir arada |

- Bölümler `levels/*.txt` dosyalarında tutulur ve araçla C tablolarına çevrilir.
- Her bölümün çözülebilir olduğu, kaydedilmiş bir oynanış senaryosuyla test edilir.

**Bitti sayılması için**
- 9 bölümün de çözüm senaryosu geçer.
- Süreler ve kristal hedefleri ayarlanır.

---

## Faz 10: Kristal Ocak'ın sesi

**Yapılacaklar**
- **Müzikler:**
  - Başlık temasında wave kanalında melodi çalar.
  - Her ocağa ayrı bir loop yazılır.
  - Süre azalınca müzik daha hızlı ve gergin bir sürüme geçer.
  - Bölüm sonu fanfarı ve game over müziği.
- **Efektler:**
  - Kazma ("hışş"), kristal (perdesi yükselen "tiling"; arka arkaya toplanınca perde yükselir).
  - Kaya düşüşü (kick + gürültü), çatlak duvarın kırılması.
  - Dinamit fitili ve patlama, grizu tıslaması, ray gıcırtısı, kapının açılması.
- Ses motoru v1 donanımda da çalışır ama wave kanalı olmadan.

**Bitti sayılması için**
- WAV'lar dinlenip onaylanır.
- Kartta deneme yapılır.

---

## Faz 11: Cila, testler ve belgeler

**Yapılacaklar**
- Bütün testler: `tests/run_tests.sh`, `vboard` sanal kart testi, `tb_apu`, `tb_game`.
- README güncellenir: Kristal Ocak ve Fitil'in yeni ekran görüntüleri, ses motoru ve APU v2 kayıt tablosu.
- `TUSLAR.md` güncellenir: B + yön, dinamit, A basılı tutarak yeniden başlama.
- `sw/build/*.mem` yeniden üretilir. cc65 olmadan da açılabilsin diye repoda tutulur.
- Kartta son deneme yapılır.

**Bitti sayılması için**
- Taze klonda proje açılır.
- Bütün oyunlar kartta yeni seslerle çalışır.
