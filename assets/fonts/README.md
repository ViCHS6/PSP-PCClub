# Шрифты PCCLUB

Исходники предоставлены в репозитории из Ubuntu `console-setup`
(`/usr/share/consolefonts/`, пакет `console-setup-linux`):

| Исходник | Формат | Ячейка | Стиль |
|---|---|---|---|
| Uni2-Terminus12x6.psf.gz | PSF2 + Unicode | 6×12 | обычный |
| Uni2-Terminus14.psf.gz | PSF1, 512 глифов + Unicode | 8×14 | обычный |
| Uni2-TerminusBold14.psf.gz | PSF1, 512 глифов + Unicode | 8×14 | жирный |

Автор: **Dimitar Toshkov Zhekov**, Copyright (c) Dimitar Toshkov Zhekov.
Проект: [Terminus Font](https://terminus-font.sourceforge.net/).
Лицензия исходников и производных шрифтов: **SIL Open Font License 1.1**,
[официальный сайт](https://openfontlicense.org), полный текст в [OFL.txt](OFL.txt).
Reserved Font Name исходника: «Terminus Font». Имена производных — PCCLUB
(`font_ui12`, `font_mono12`, `font_ui14`, `font_ui14b`). При распространении
шрифтов сохранять copyright и OFL.txt. Версия Ubuntu-пакета в предоставленных
PSF не записана; точную версию пакета по этим файлам не заявляем.

`extra_glyphs.txt` — дополнения PCCLUB, распространяются по той же OFL 1.1.
Три варианта ₽ нарисованы по форме исходной «Р» с дополнительной перекладиной;
значки кнопок ○ △ □ — контурные, по три варианта (6×12, 8×14, 8×14 bold).
Все остальные символы набора, включая кавычки и ×, уже есть в исходных шрифтах.
Формат дополнений, метрики и контракт рисования: [font.md](../../docs/formats/font.md).

`fonts.json` задаёт четыре результата. Сгенерированные файлы:

* `font_ui12.png`, `font_ui12.fnt` — 6×12, пропорциональный;
* `font_mono12.png`, `font_mono12.fnt` — 6×12, моноширинный;
* `font_ui14.png`, `font_ui14.fnt` — 8×14, пропорциональный;
* `font_ui14b.png`, `font_ui14b.fnt` — 8×14, жирный пропорциональный.

В каждом 185 символов; цифры пропорциональных шрифтов сохраняют моноширинный
шаг. Генерация из корня проекта (Python 3.11+ и Pillow):

```powershell
py -3.12 tools/make_font.py --all
```

Генерация с превью всех четырёх шрифтов (увеличение ×3, каталог не коммитится):

```powershell
py -3.12 tools/make_font.py --all --preview build/fonts_preview
```

Тесты без сети, синтетические фикстуры создаются во временной папке:

```powershell
py -3.12 -m unittest discover -s tools/tests -p "test_make_font.py" -v
```
