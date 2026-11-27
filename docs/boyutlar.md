# Oyunların bellek kullanımı

`python3 tools/sizes.py --md docs/boyutlar.md` ile üretilir (`make -C sw` sonrası). Bayt cinsinden.
`$9000`'dan yüklenen oyunlar için yer 20 KB (20480), `$0400`'dan yüklenenler için 31 KB (31744).

| oyun | yükleme | ikili | boş yer | kod | veri | BSS (RAM) | zero page |
|---|---|---|---|---|---|---|---|
| blok | $9000 | 18283 | 2197 | 15000 | 3243 | 1181 | 32 |
| fitil | $9000 | 18729 | 1751 | 13251 | 5438 | 1190 | 32 |
| koridor | $0400 | 29108 | 2636 | 15807 | 13261 | 3813 | 74 |
| kristal | $9000 | 11973 | 8507 | 8125 | 3808 | 923 | 32 |
| launcher | $9000 | 16280 | 4200 | 9221 | 7019 | 350 | 26 |
| olimpiyat | $9000 | 10973 | 9507 | 8311 | 2622 | 509 | 32 |
| tilsim | $0400 | 31378 | 366 | 23161 | 8177 | 705 | 32 |
| yildiz | $9000 | 11336 | 9144 | 8455 | 2841 | 625 | 32 |
