# Retro Konsol — Tuş Rehberi

Konsol, kartın **USB HOST** portuna takılan sıradan bir USB klavyeyle oynanır. Oyunların içinde artık "A", "B"
yerine tuşların kendi adları yazar. Bu sayfa hepsini bir arada gösterir.

## Kısa kural

| Ne yapar | 1. oyuncu (klavyenin solu) | 2. oyuncu (klavyenin sağı) |
| --- | --- | --- |
| Yürü / seç | **W A S D** | **Ok tuşları** (sayısal tuş takımında 8 4 2 6 de olur) |
| Ana tuş (ateş, bomba, onay) | **Sol Shift** | **Sağ Shift** |
| İkinci tuş (geri, sola döndür…) | **Sol Ctrl** | **Sağ Ctrl** |
| Başlat / duraklat | **Space** | **Enter** |
| Select (tut, at…) | **Tab** | **Backspace** |
| Seçme ekranına dön | **Esc** | **Esc** |

- **Shift her zaman "A", Ctrl her zaman "B"** demektir; hangisini kullanacağın oyuncuna göre değişir (sol / sağ).
- **Tek oyunculu oyunlar** iki tarafı da dinler: ister W A S D + Sol Shift, ister ok tuşları + Sağ Shift ile oyna.
- Kartın butonları da 1. oyuncudur: **BTNC** (orta) = Shift, **BTNU/BTND/BTNL/BTNR** = yönler.
- 3. ve 4. oyuncu NES/SNES kolu kullanır (Pmod JA, README 7. bölüm); kolda A, B, Select, Start aynen geçerlidir.
- Kolda menüye dönmek için **Select + Start** birlikte.

## Oyun oyun

### Seçme ekranı (launcher)
| Tuş | İş |
| --- | --- |
| Ok tuşları / A D | Oyunlar arasında gez |
| Enter / Space | Seçili oyunu başlat |

### Blok Yağmuru
| Tuş | İş |
| --- | --- |
| Ok sol / sağ (A / D) | Parçayı kaydır (basılı tutunca hızlanır) |
| Ok aşağı (S) | Hızlı düşür |
| Ok yukarı (W) | Anında bırak |
| **X** ya da Shift | Sağa döndür |
| **Z** ya da Ctrl | Sola döndür |
| **C** ya da Tab | Parçayı TUT kutusuna koy / oradakini al |
| Space / Enter | Başlat, duraklat |
| Esc | Menüye dön (duraklatılmışken de) |

### Fitil Arenası (2–4 oyuncu)
| | Yürü | Bomba |
| --- | --- | --- |
| 1. oyuncu — kırmızı | W A S D | Sol Shift (ya da Sol Ctrl) |
| 2. oyuncu — mavi | Ok tuşları | Sağ Shift (ya da Sağ Ctrl) |
| 3. oyuncu — yeşil | 3. NES kolu | A (ya da B) |
| 4. oyuncu — sarı | 4. NES kolu | A (ya da B) |

Kurulum ekranında: yukarı / aşağı oyuncu sayısı, **Space / Enter** başlat, **Esc** menü.

### Tılsım
| Tuş | İş |
| --- | --- |
| Ok tuşları | Kartlar / satırlar arasında gez |
| Shift | Kartı seç, satın al, kapıya gir |
| Ctrl | Elini sırala, kapıyı atla (etiketi al) |
| Space | Seçili eli oyna, pazardan devam et |
| Tab | Seçili kartları at, pazarda yenile |
| Ok yukarı | Tılsımlar / pazarda sat |
| Esc | Menüye dön |

### Kristal Ocak
| Tuş | İş |
| --- | --- |
| Ok tuşları / W A S D | Kaz, yürü (basılı tutunca yürümeye devam eder) |
| Ctrl + yön | Yerinden kıpırdamadan kaz ya da topla; boş hücreye dinamit koy |
| Shift (basılı tut) | Bölümü baştan başlat (bir can gider) |
| Space / Enter | Başlat, seç; oyunda duraklat menüsü |
| Ok tuşları (menüde) | Ocak ve bölüm seç, menüde gezin |
| Ctrl (ocak seç ekranında) | Başlığa dön |

### Yıldız Muhafızı
| Tuş | İş |
| --- | --- |
| Ok tuşları / W A S D | Gemiyi yönet |
| Shift | Ateş |
| Space / Enter | Başlat |

### Koridor 64 (birinci şahıs nişancı)
| Tuş | İş |
| --- | --- |
| Ok yukarı / aşağı (W / S) | İleri / geri yürü |
| Ok sol / sağ (A / D) | Dön |
| Ctrl + ok sol / sağ | Yana kay |
| Shift | Ateş et (basılı tut: seri) |
| Kapıya doğru yürü | Kapı açılır |
| Space | Duraklat |
| Esc | Menüye dön |

### Mahalle Olimpiyatı (sırayla 1–4 oyuncu)
| Yarışma | Tuş |
| --- | --- |
| Hazır / devam | Shift |
| 100 m koşu | **Shift ve Ctrl'e sırayla** hızlı hızlı bas |
| Uzun atlama | Shift–Ctrl sırayla koş, çizgide **yukarı** ile atla |
| Refleks | İşaret çıkınca **Shift** |

Sıra kimdeyse o oyuncunun tuşları geçerli: 1. oyuncu Sol Shift / Sol Ctrl, 2. oyuncu Sağ Shift / Sağ Ctrl.

## Klavyeyle ilgili notlar

- Ucuz klavyeler aynı anda basılan bazı tuş kombinasyonlarını iletmez (ghosting); kartın USB denetleyicisi de en
  fazla 6 normal tuş + Shift/Ctrl geçirir. İki kişi tek klavyede oynarken bazı tuşlar kaybolabilir.
- Kartın USB denetleyicisi (PIC24) sol/yukarı/aşağı okları standart dışı gönderir; konsol bunu tanır. Ok tuşları
  çalışmıyorsa karttaki bitstream 26 Kasım 2026'dan eskidir: yeniden üret.
- Bir karakter kendi kendine yürüyorsa (tuş takılı kalmış gibi), klavyeyi çıkarıp takmak tüm tuşları bırakır.
