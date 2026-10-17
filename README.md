# KickDuck1

Плагин ducking для кика и баса на Linux: **VST3 + Standalone**, C++17 / JUCE 8.0.10.
Основной вход принимает бас, отдельный Kick Sidechain AUX — управляющий кик.
Кик не подмешивается в выход.

Подготовлен пакет **1.0.0**. Пользователь проверил в REAPER пресеты,
восстановление проекта с активным B, автоматизацию, несколько экземпляров
и offline render. Условия распространения проекта ещё не выбраны;
GitHub Release/тег не публиковались. См. [статус выпуска](docs/DISTRIBUTION.md).

## Возможности

- AUTO / AUDIO / HOST SYNC и деления от 1/16 до 1 BAR (четыре четверти).
- Скруглённая редактируемая кривая до 32 точек и шесть форм.
- Стартовый Classic с Amount 70% и подсветкой; Attack/Length/Recover/Tail.
- Input/Output gain, сглаживание и полный Bypass за 20 мс.
- A/B с копированием и отдельной историей Undo/Redo до 64 шагов.
- Полные файловые пресеты `.kickduckpreset`, загрузка отменяется одним Undo.
- Кадр Kick/Bass Out, двухсекундный живой OUT/GR scope, stereo Peak/RMS/GR.
- Стартовое окно 1050×700, диапазон ресайза 1050×620…1800×1100.

## Установка и работа

[Установка и маршрутизация в REAPER](docs/INSTALL.md) ·
[Руководство пользователя](docs/USER_GUIDE.md) · [Changelog](CHANGELOG.md).

Для VST3 копируйте всю папку `KickDuck1.vst3` в `~/.vst3/`, затем обновите
список плагинов DAW. На басовой дорожке основной сигнал идёт в 1/2,
а send кика — в 3/4. Для работы без кика выбирайте HOST SYNC.
AUTO видит включение AUX-шины, а не наличие send внутри DAW.

Текущий бинарный пакет — Linux x86-64 с glibc 2.43+ и libstdc++ с
GLIBCXX_3.4.32 и CXXABI_1.3.15. Совместимость с другими системами не подтверждена;
точные зависимости записаны в BUILD_INFO.json пакета. Исходники можно
собрать под свою систему. Это Sample Peak/RMS, не True Peak/LUFS.

## Сборка и проверки

[Исходный репозиторий](https://github.com/mabobylev/KickDuck1).

Нужны CMake 3.22+, C++17, Git и заголовки ALSA, X11, FreeType/fontconfig,
OpenGL для JUCE. При первой конфигурации JUCE 8.0.10 загружается FetchContent:

```sh
cmake --preset dev
cmake --build --preset tests
ctest --preset dev
cmake --build --preset plugin
```

Если есть `build/_deps/juce-src`, вместо `dev` используйте
`cmake --preset offline`. Presets ограничивают сборку тремя заданиями.
`plugin` собирает VST3 и Standalone и автоматически заменяет
`~/.vst3/KickDuck1.vst3`. Отключение установки при конфигурации:
`-DKICKDUCK_COPY_PLUGIN_AFTER_BUILD=OFF`. Тесты: `-DBUILD_TESTING=OFF`.

Результаты находятся в `build-dev/KickDuck1_artefacts/Release/VST3/`
и `build-dev/KickDuck1_artefacts/Release/Standalone/`.

```sh
python3 scripts/package_release.py
```

Пакет и контрольная сумма создаются в `build-dev/dist/`, в Git не добавляются.
Сценарий не устанавливает и не публикует релиз. Подробности —
[комплект выпуска](docs/DISTRIBUTION.md).

## Разработка и совместимость

`Source/PluginProcessor.*` — DSP, параметры, состояние, A/B и история;
`Source/PluginEditor.*` — GUI; `DuckingCurve.h` — общая геометрия;
`TriggeredWaveform.h` и `WaveformHistory.h` — обмен аудио/GUI;
`Tests/` — DSP, состояние, пресеты, GUI, аллокации и конкурентные проверки.

Состояние проекта v9, файловый пресет v1. ID и диапазоны параметров
сохраняются, старые состояния мигрируют. Legacy Mix доступен в DAW;
старые линейные кривые не заменяются автоматически. A/B сохраняются
в проекте, история Undo сохраняется только в текущей сессии процессора.
Подробности: [семантика ручек](docs/CONTROL_SEMANTICS.md),
[аудит и его ограничения](docs/REALTIME_AUDIT.md),
[контекст](docs/CONTEXT.md), [план](docs/PLAN.md).
