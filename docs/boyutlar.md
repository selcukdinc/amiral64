# Oyunların bellek kullanımı

`python3 tools/sizes.py --md docs/boyutlar.md` ile üretilir (`make -C sw` sonrası). Bayt cinsinden.
`$9000`'dan yüklenen oyunlar için yer 20 KB (20480), `$0400`'dan yüklenenler için 31 KB (31744).

| oyun | yükleme | ikili | boş yer | kod | veri | BSS (RAM) | zero page |
|---|---|---|---|---|---|---|---|
| blok | $9000 | 18912 | 1568 | 14983 | 3889 | 1196 | 32 |
| fitil | $9000 | 19350 | 1130 | 13302 | 6008 | 1206 | 32 |
| koridor | $0400 | 29822 | 1922 | 15897 | 13885 | 3829 | 74 |
| kristal | $0400 | 24804 | 6940 | 21256 | 3508 | 1698 | 32 |
| launcher | $9000 | 19229 | 1251 | 10979 | 8210 | 830 | 32 |
| olimpiyat | $9000 | 11981 | 8499 | 8418 | 3523 | 525 | 32 |
| tilsim | $0400 | 30855 | 889 | 23216 | 7599 | 721 | 32 |
| yildiz | $9000 | 12227 | 8253 | 8545 | 3642 | 641 | 32 |
