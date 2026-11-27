# Oyunların bellek kullanımı

`python3 tools/sizes.py --md docs/boyutlar.md` ile üretilir (`make -C sw` sonrası). Bayt cinsinden.
`$9000`'dan yüklenen oyunlar için yer 20 KB (20480), `$0400`'dan yüklenenler için 30 KB (30720).

| oyun | yükleme | ikili | boş yer | kod | veri | BSS (RAM) | zero page |
|---|---|---|---|---|---|---|---|
| blok | $9000 | 15465 | 5015 | 13098 | 2327 | 714 | 26 |
| fitil | $9000 | 15911 | 4569 | 11349 | 4522 | 723 | 26 |
| koridor | $0400 | 26290 | 4430 | 13905 | 12345 | 3346 | 68 |
| kristal | $9000 | 9155 | 11325 | 6223 | 2892 | 456 | 26 |
| launcher | $9000 | 16475 | 4005 | 9416 | 7019 | 347 | 26 |
| olimpiyat | $9000 | 8155 | 12325 | 6409 | 1706 | 42 | 26 |
| tilsim | $0400 | 29051 | 1669 | 21038 | 7973 | 238 | 26 |
| yildiz | $9000 | 8518 | 11962 | 6553 | 1925 | 158 | 26 |
