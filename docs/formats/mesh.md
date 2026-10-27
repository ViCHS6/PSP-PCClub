# MSH1 — статическая геометрия, версия 1 (T011)

Контракт для T012. Все целые и IEEE-754 binary32 — **little-endian**.
Смещения абсолютные, от начала файла, в байтах; указателей в файле нет.
На PSP весь файл загружается в память с базовым выравниванием **16 байт**.
На PC числа читаются как LE (на big-endian требуется перестановка байтов).
Нормалей, костей, анимации и mipmaps здесь нет. Единицы — метры.

## Раскладка

| Область | Смещение | Размер |
|---|---:|---:|
| Заголовок | 0 | 96 |
| Подсеты | 96 | `submesh_count * 48` |
| Нулевое дополнение | после подсетов | до выравнивания 16 |
| Вершины | `vertices_offset = align_up(96 + 48 * submesh_count, 16)` | `vertex_count * 16` |
| Индексы | `indices_offset = vertices_offset + vertex_count * 16` | `index_count * 2` |
| Нулевое дополнение | после индексов | до кратности размера файла 4 |

В данной версии размер таблицы подсетов уже кратен 16, но загрузчик всё равно
проверяет выравнивание. Итоговый размер файла строго
`align_up(indices_offset + 2 * index_count, 4)`. Дополнительных секций нет.

```cpp
#include <cstddef>
#include <cstdint>

struct MshHeader {
    char magic[4];                 // 0: "MSH1", без завершающего нуля
    uint16_t version;              // 4: 1
    uint16_t flags;                // 6: 0
    uint16_t vertex_count;         // 8: 1..65535
    uint16_t submesh_count;        // 10: 1..65535
    uint32_t index_count;          // 12: число индексов, кратно 3
    uint32_t vertices_offset;      // 16
    uint32_t indices_offset;       // 20
    uint32_t submeshes_offset;     // 24: 96
    uint32_t reserved;             // 28: 0
    float pos_offset[3];          // 32
    float pos_scale[3];           // 44
    float uv_offset[2];           // 56
    float uv_scale[2];            // 64
    float bbox_min[3];            // 72: исходный AABB
    float bbox_max[3];            // 84
};
struct MshSubmesh {
    uint32_t index_start;          // 0: в индексах, не в байтах
    uint32_t index_count;          // 4: кратно 3
    char texture[32];              // 8: ASCII id, не путь
    uint32_t reserved[2];          // 40: 0
};
struct MshVertex {
    uint16_t u, v;                 // 0: квантованные UV
    uint32_t color;                // 4: 0xAABBGGRR
    int16_t x, y, z;               // 8: квантованная позиция
    uint16_t padding;              // 14: 0
};
static_assert(sizeof(float) == 4);
static_assert(sizeof(MshHeader) == 96);
static_assert(sizeof(MshSubmesh) == 48);
static_assert(sizeof(MshVertex) == 16);
static_assert(offsetof(MshHeader, pos_offset) == 32);
static_assert(offsetof(MshHeader, bbox_max) == 84);
static_assert(offsetof(MshVertex, color) == 4);
static_assert(offsetof(MshVertex, x) == 8);
```

Это структуры для обычного ABI PSP/PC, без `pack(1)` и без `alignas(16)`
на структурах файла. Базу буфера выравнивает аллокатор. На произвольной платформе
безопаснее читать поля через LE-функции / `memcpy`, а не приводить сырой адрес.
Сумма полей заголовка: 32 байта целых + 16 float × 4 = **96**.

## Координаты, UV и цвет

Вход OBJ уже в осях движка: правая система, **Y вверх**.
Камера движка смотрит вдоль **−Z в пространстве камеры**, как в OpenGL /
`core::mat4_look_at`. **Лицевая сторона ассета** — экран монитора, передняя
панель ПК, направление взгляда сидящего — обращена к **+Z в пространстве объекта**.
Это соответствует Blender **−Y** при стандартной выгрузке OBJ
(`forward −Z, up Y`) и соглашению заглушек T013.
Экспорт Blender выполняет перевод из Blender Z-up; конвертер ничего не вращает,
не меняет единицы и порядок обхода. В файле обход граней — CCW в пространстве
объекта при взгляде снаружи.
Камера и модель используют column-major `core::Mat4`, `p' = M * p`.

Из OBJ берутся только вершины, используемые гранями. AABB вычисляется по их
исходным позициям, до квантования. Для каждой оси:

```text
pos_offset = (bbox_min + bbox_max) / 2
pos_scale  = (bbox_max - bbox_min) / 2; если 0, то 1.0
q_pos      = clamp(round((p - pos_offset) / pos_scale * 32768), -32768, 32767)
p          = pos_offset + (q_pos / 32768.0) * pos_scale
```

GE в `GU_TRANSFORM_3D` читает знаковую 16-битную позицию как `q / 32768`.
Положительный максимум AABB имеет ошибку до одного шага `pos_scale / 32768`,
поскольку +32768 недоступно. Для прочих значений ошибка обычно до половины шага.
Нулевой размер оси кодируется `q = 0`, `scale = 1`.

Перед вычислением UV-диапазонов применяется **`v_file = 1 - v_obj`**:
строка 0 изображения — верхняя, UV v=0 — верх. Пропущенные `vt` дают `(0, 0)`
уже в координатах файла. UV не ограничиваются диапазоном 0..1:

```text
uv_offset = min(uv)
uv_scale  = (max(uv) - min(uv)) * 32768 / 65535; если диапазон 0, то 1.0
q_uv      = clamp(round((uv - uv_offset) / uv_scale * 32768), 0, 65535)
uv        = uv_offset + (q_uv / 32768.0) * uv_scale
```

GE читает UV как unsigned16 / 32768, то есть [0, 2), а затем применяет
`sceGuTexScale` и `sceGuTexOffset`. Это позволяет отрицательные UV и тайлы >1.
На PC тоже нельзя ограничивать декодированные UV до 0..1.

В примечании PSPSDK к `sceGuDrawArray` все 16-битные атрибуты обобщённо названы
signed; для UV это неточно. Здесь сохраняется uint16 из задания: unsigned UV и
деление на 32768 подтверждаются `Step_TcU16ToFloat` / `Step_TcU16Prescale` в
[декодере GE PPSSPP](https://github.com/hrydgard/ppsspp/blob/master/GPU/Common/VertexDecoderCommon.cpp).
Читать UV как int16 нельзя: значения >=32768 испортят верхнюю половину диапазона.

`pos_offset`, `pos_scale`, `uv_offset`, `uv_scale` сначала округляются до float32;
квантование использует именно эти сохранённые коэффициенты. `bbox_min/max`
сохраняются как float32. Во всех операциях `round` — ближайшее целое,
ровно половина округляется к чётному (включая отрицательные значения).
Непредставимые, бесконечные и NaN-координаты отклоняются. Положительные масштабы
не должны округляться в ноль. Погрешность float32 добавляется к шагу квантования.

Цвет OBJ `v x y z r g b` и `Kd r g b` MTL — компоненты 0..1.
`byte = round(vertex_color * Kd * 255)`, без гамма-преобразований, в sRGB.
Без цвета OBJ берётся белый; без материала / без Kd — белый Kd. Альфа всегда 255.
Четыре байта в памяти: R,G,B,A, целое значение как у `render::Color` — 0xAABBGGRR.

Вершина имеет порядок атрибутов GE: текстура, цвет, позиция; 2 байта в конце
доводят шаг до выравнивания старшего атрибута (цвета, 4 байта).
Vtype без индексов: `GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D`.
С индексами добавляется `GU_INDEX_16BIT` (полное значение **0x111E**).

## Грани и материалы

Полигоны триангулируются веером `(0,1,2), (0,2,3), ...` без смены winding.
Для вогнутых полигонов веер может дать неверную поверхность: такие модели надо
триангулировать до экспорта. ART.md требует треугольники/квады; конвертер по
заданию T011 также принимает n-гоны, не повышая художественные бюджеты.
Нормали OBJ проверяются на корректность ссылок, но не сохраняются: свет запечён
в цветах, runtime lighting выключено.

Подсет — один на имя `usemtl`, порядок первого `usemtl`; до него есть отдельный
подсет без материала, если там встретились грани. Пустые подсеты исключаются.
Возврат к прежнему материалу дописывает индексы в его подсет. Все индексы
подсета подряд; первый `index_start=0`, каждый следующий начинается сразу после
предыдущего, сумма count равна заголовку. Индексы **глобальные**, в общий массив
вершин, тип uint16, `GU_TRIANGLES`. После квантования одинаковые 16 байт вершины
(позиция + UV + цвет; padding=0) объединяются глобально, в том числе между
материалами. Порядок вершин — первое употребление при обходе уже сгруппированных
подсетов. Лимит **65535 уникальных вершин**, индексы 0..65534; превышение — ошибка.

`texture` — basename `map_Kd` без пути и последнего расширения, ASCII 1..31 байт,
затем NUL и нули до 32. Без `map_Kd` все 32 байта нулевые. Разделители пути `/`
и `\` распознаются. Например, `../textures/tex_crt.png` → `tex_crt`; загрузчик
разрешает этот id через реестр текстур приложения, не открывает путь из MTL.

Поддерживаются `v`, `vt u v [w]` (w игнорируется), `vn`, `f` (`v`, `v/vt`,
`v//vn`, `v/vt/vn`), положительные и отрицательные индексы; 0 запрещён.
Ссылки должны существовать в момент чтения грани. `v` допускает ровно 3 или 6
чисел, homogeneous w не поддерживается. `o/g/s` игнорируются; другие команды
OBJ отклоняются. Имена с пробелами в `mtllib`/`map_Kd` можно заключать в кавычки;
MTL ищется относительно OBJ. В MTL читаются `newmtl`, `Kd`, `map_Kd`, прочие
свойства игнорируются. Неизвестный `usemtl` — ошибка; повторное определение
материала заменяет предыдущее. Опции `map_Kd -s/-o/...` отклоняются: их надо
запечь в UV, иначе контракт потеряет преобразование. Отсутствующая текстура на
диске не мешает конвертации: сохраняется только id. UTF-8 / UTF-8 BOM, комментарии `#`.

## Валидация загрузчиком

До выделения арен и передачи адресов GE проверить:

1. Magic/version, flags/reserved=0, ненулевые vertex/submesh/index count;
   index_count кратно 3. Все размеры и суммы считать как минимум в uint64,
   проверять границы файла до доступа.
2. Смещения и точный размер по раскладке; выравнивание базы и вершин 16,
   индексов 2. Padding файла и вершин нулевой.
3. Все float конечны, scale >0, bbox_min <= bbox_max поканально.
   Для отсечения учитывать погрешность float32/квантования (не сужать AABB).
4. Подсеты непусты, count кратны 3, образуют полное разбиение индексов;
   reserved=0, texture содержит NUL и корректный ASCII до него, остаток нулевой.
5. Каждый индекс < vertex_count. Неизвестный texture id обработать диагностикой
   / заглушкой по политике ресурсов, не читать произвольный файл.

## PSP: загрузка и отрисовка (псевдокод)

Загрузка выполняется один раз вне игрового цикла, файл остаётся жив до завершения
команд GE. Копировать вершины/индексы не требуется. После CPU-записи буфера
выполнить cache writeback перед чтением GE. VIEW/PROJECTION уже выставлены камерой.

Какое значение `sceGuFrontFace` соответствует файловому CCW с учётом переворота
Y во вьюпорте GU, определяет и проверяет T012 в PPSSPP. В сэмплах PSPSDK
с отсечением часто используется `GU_CW`. В рендере T001 отсечение граней сейчас
выключено. При отражении модели (`det(model)<0`) также учитывать смену обхода.

```cpp
blob = load_file_aligned_16(application_relative_path);
validate_msh(blob);
h = read_header(blob);
vertices = blob + h.vertices_offset;
indices = reinterpret_cast<const uint16_t*>(blob + h.indices_offset);
sceKernelDcacheWritebackRange(blob, file_size);

// На кадре: без выделения памяти, model из мира.
core::Mat4 packed_model = model
    * core::mat4_translate({h.pos_offset[0], h.pos_offset[1], h.pos_offset[2]})
    * core::mat4_scale({h.pos_scale[0], h.pos_scale[1], h.pos_scale[2]});
sceGumMatrixMode(GU_MODEL);
sceGumPushMatrix();
sceGumLoadMatrix(as_sce_matrix(packed_model));
sceGuDisable(GU_LIGHTING);
sceGuShadeModel(GU_SMOOTH);
// sceGuFrontFace: значение определит T012 после проверки в PPSSPP.
const int vtype = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT
                | GU_INDEX_16BIT | GU_TRANSFORM_3D;
for (s : submeshes) {
    if (s.texture[0]) {
        sceGuEnable(GU_TEXTURE_2D);
        bind_tex(texture_by_id(s.texture)); // См. texture.md; до установки UV.
        sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexWrap(GU_REPEAT, GU_REPEAT);
        sceGuTexScale(h.uv_scale[0], h.uv_scale[1]);
        sceGuTexOffset(h.uv_offset[0], h.uv_offset[1]);
    } else {
        sceGuDisable(GU_TEXTURE_2D);
    }
    // Поле count команды GE — 16 бит, заголовок допускает uint32.
    for (uint32_t done = 0; done < s.index_count; ) {
        int count = min(65535u, s.index_count - done); // Оба кратны 3.
        sceGumDrawArray(GU_TRIANGLES, vtype, count,
                       indices + s.index_start + done, vertices);
        done += count;
    }
}
sceGumPopMatrix();
sceGuTexScale(1.0f, 1.0f);
sceGuTexOffset(0.0f, 0.0f);
// Восстановить остальные состояния согласно политике рендера.
```

`as_sce_matrix` означает адаптацию layout column-major к `ScePspFMatrix4`, без
смены порядка умножений. Матрица должна иметь выравнивание 16. При следующих
draw вызовах снова выставлять модель: packed-преобразование нельзя применять дважды.

## PC: раскодирование (псевдокод)

```cpp
validate_msh(blob);
// Массив выделяется при загрузке, не на кадре.
for (uint32_t i = 0; i < h.vertex_count; ++i) {
    q = read_msh_vertex_le(blob + h.vertices_offset + 16 * i);
    render::Vertex& v = decoded[i];
    v.u = h.uv_offset[0] + (float(q.u) / 32768.0f) * h.uv_scale[0];
    v.v = h.uv_offset[1] + (float(q.v) / 32768.0f) * h.uv_scale[1];
    v.color = render::rgba(q.r, q.g, q.b, q.a);
    v.x = h.pos_offset[0] + (float(q.x) / 32768.0f) * h.pos_scale[0];
    v.y = h.pos_offset[1] + (float(q.y) / 32768.0f) * h.pos_scale[1];
    v.z = h.pos_offset[2] + (float(q.z) / 32768.0f) * h.pos_scale[2];
}
for (s : submeshes) {
    // Текущий mesh_create принимает uint16_t index_count: разбить по 65535.
    for (chunk : split_into_triangle_chunks(s, 65535)) {
        id = render::mesh_create(decoded, h.vertex_count,
                                 indices + chunk.start, uint16_t(chunk.count));
        require(id != render::kInvalidMesh);
        remember_draw(id, texture_by_id_or_none(s.texture));
    }
}
// mesh_create копирует данные; временные decoded/indices после создания освободить.
// На кадре для каждого сохранённого draw:
render::draw_mesh(id, model, texture); // Позиции уже распакованы, без T(offset)*S(scale).
```

Простой адаптер `mesh_create` копирует все вершины для каждого подсета/чанка и
расходует арену повторно; T012 может хранить общий vertex buffer во внутренней
реализации. PSP-путь с packed вершинами требует внутреннего адаптера T012:
существующий публичный `mesh_create(render::Vertex*)` не принимает MSH-вершины.
`render_api.h` данной задачей не меняется. Отсутствие нормалей, tiled UV и подсеты
не должны теряться при адаптации.

## Пример с числами

Плоский квад `(0,0,0), (2,0,0), (2,2,0), (0,2,0)`, один материал:
`vertex_count=4`, `index_count=6`, `submesh_count=1`, таблица на 96,
вершины на **144**, индексы на **208**, размер **220** байт.
Индексы: `0,1,2, 0,2,3`. `pos_offset=(1,1,0)`, `pos_scale=(1,1,1)`.
`q` первой позиции `(-32768,-32768,0)` → `(0,0,0)`;
третьей `(32767,32767,0)` → `(1.9999694824,1.9999694824,0)`.

OBJ UV углов `(-1,0), (2,0), (2,1), (-1,1)` после flip:
`(-1,1), (2,1), (2,0), (-1,0)`. `uv_offset=(-1,0)`,
`uv_scale≈(1.5000228882,0.5000076294)`; q первых двух UV —
`(0,65535)` и `(65535,65535)`. Они декодируются примерно в `(-1,1)` и `(2,1)`.
Цвет OBJ `(0.5,1,0.25)` × Kd `(0.5,0.25,1)` → байты `(64,64,64,255)`.

## CLI и источники

```text
py -3.12 tools/convert_model.py in.obj -o out.msh [--report]
```

`--report`: вершины, треугольники, подсеты и AABB. Ошибка — stderr,
ненулевой exit code; ошибки синтаксиса/ссылок содержат `OBJ:строка`, ошибки MTL
дополнительно `MTL:строка`. Файл вывода открывается после полной проверки модели.

Определения vtype и GU: [PSPSDK pspgu.h](https://pspdev.github.io/pspsdk/pspgu_8h.html).
Предел одного draw: [sceGuDrawArray](https://github.com/pspdev/pspsdk/blob/master/src/gu/sceGuDrawArray.c).
Применение GU, памяти и текстур: [PSP development report §6.5](../../PSP_DEVELOPMENT_REPORT_RU.md#65-текстуры-и-форматы).
Смежный контракт: [TEX1](texture.md).
