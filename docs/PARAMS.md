# Параметры Dolby Audio Processing

Идентификаторы параметров движка Dolby - это **четыре ASCII-символа**, записанные как
32-битное число (little-endian). Для трёхбуквенных кодов четвёртый байт нулевой.

Примеры:

```
"deon" -> байты 64 65 6F 6E -> число 0x6E6F6564 = 1852794212
"dea"  -> байты 64 65 61 00 -> число 0x00616564 = 6382948
```

Таблица собрана дизассемблированием `libswdap.so` (функция обработки параметров) и сверена с
эталонным профилем Dolby. Названия в третьей колонке - имена внутренних функций движка, по ним
видно назначение параметра.

## Флаги включения

| Код | Значение | Внутренняя функция |
|---|---|---|
| `beon` | bass enhancer | `dap_cpdp_bass_enhancer_enable_set` |
| `ceon` | complex equalizer | `dap_cpdp_complex_equalizer_enable_set` |
| `deon` | dialog enhancer (диалоги) | `dap_cpdp_de_enable_set` |
| `geon` | graphic equalizer | `dap_cpdp_graphic_equalizer_enable_set` |
| `ieon` | intelligent equalizer | `dap_cpdp_ieq_enable_set` |
| `aoon` | audio optimizer | `dap_cpdp_audio_optimizer_enable_set` |
| `poon` | process optimizer | `dap_cpdp_process_optimizer_enable_set` |
| `hpon` | hearing protection (защита слуха) | `dap_cpdp_hearing_protection_enable_set` |
| `aron` | regulator (контроль перегрузки) | `dap_cpdp_regulator_enable_set` |
| `ngon` | surround decoder (объём) | `dap_cpdp_surround_decoder_enable_set` |
| `rvse` | reverb suppression | `dap_cpdp_rs_enable_set` |
| `lave` | virtualizer | `dap_cpdp_virtualizer_*_set` |
| `mave` | mi adaptive virtualizer steering | `dap_cpdp_mi2*` |
| `miee` | mi IEQ steering | `dap_cpdp_mi2ieq_steering_enable_set` |
| `mdle` | mi volume leveler steering | `dap_cpdp_mi2dv_leveler_steering_enable_set` |
| `mdee` | mi dialog enhancer steering | `dap_cpdp_mi2dialog_enhancer_steering_enable_set` |
| `msce` | mi surround compressor steering | `dap_cpdp_mi2surround_compressor_steering_enable_set` |
| `mvbe` | mi binaural steering | `dap_cpdp_mi2virtualizer_binaural_steering_enable_set` |
| `bmde` | bass mbdrc | `dap_cpdp_bass_mbdrc_enable_set` |
| `arde` | regulator speaker distortion | `dap_cpdp_regulator_speaker_distortion_enable_set` |
| `rsse` | regulator sibilance suppress | `dap_cpdp_regulator_sibilance_suppress_enable_set` |
| `dvme` | volume modeler | `dap_cpdp_volume_modeler_enable_set` |
| `bexe` | bass extraction | `dap_cpdp_bass_extraction_enable_set` |
| `dvlc` | volume leveler compressor | `dap_cpdp_volume_leveler_compressor_enable_set` |

## Числовые параметры

| Код | Значение | Внутренняя функция |
|---|---|---|
| `dea` | dialog enhancer amount (0..16) | `dap_cpdp_de_amount_set` |
| `ded` | dialog enhancer ducking | `dap_cpdp_de_ducking_set` |
| `iea` | intelligent eq amount | `dap_cpdp_ieq_amount_set` |
| `dvla` | volume leveler amount | `dap_cpdp_volume_leveler_amount_set` |
| `dvli` | volume leveler input target | `dap_cpdp_volume_leveler_in_target_set` |
| `dvlo` | volume leveler output target | `dap_cpdp_volume_leveler_out_target_set` |
| `dvmc` | volume modeler calibration | `dap_cpdp_volume_modeler_calibration_set` |
| `beb` | bass enhancer boost (0..192) | `dap_cpdp_bass_enhancer_boost_set` |
| `becf` | bass enhancer cutoff frequency | `dap_cpdp_bass_enhancer_cutoff_frequency_set` |
| `bexf` | bass extraction cutoff frequency | `dap_cpdp_bass_extraction_cutoff_frequency_set` |
| `plb` | calibration boost | `dap_cpdp_calibration_boost_set` |
| `vmb` | volmax boost (громкость) | `dap_cpdp_volmax_boost_set` |
| `dsb` | surround boost | `dap_cpdp_surround_boost_set` |
| `vol` | system gain | `dap_cpdp_system_gain_set` |
| `preg` | pregain | `dap_cpdp_pregain_set` |
| `pstg` | postgain | `dap_cpdp_postgain_set` |
| `vbm` | virtual bass mode (0..3) | `dap_cpdp_virtual_bass_mode_set` |
| `dom` | output mode | `dap_cpdp_output_mode_set` |
| `vssd` | headphone virtualizer steerer distance | `dap_cpdp_headphone_virtualizer_steerer_source_distance_set` |
| `svsb` | virtualizer start band | `dap_cpdp_virtualizer_start_band_set` |
| `avsb` | advanced speaker virtualizer start bin | `dap_cpdp_advanced_speaker_virtualizer_start_bin_set` |
| `dfsa` | virtualizer front speaker angle | `dap_cpdp_virtualizer_front_speaker_angle_set` |
| `dhsa` | virtualizer height speaker angle | `dap_cpdp_virtualizer_height_speaker_angle_set` |
| `dsa` | virtualizer surround speaker angle | `dap_cpdp_virtualizer_surround_speaker_angle_set` |
| `rvsa` | reverb suppression amount | `dap_cpdp_rs_amount_set` |
| `arod` | regulator overdrive | `dap_cpdp_regulator_overdrive_set` |
| `ards` | regulator distortion slope | `dap_cpdp_regulator_distortion_slope_set` |
| `hpaa` | hearing protection attack time | `dap_cpdp_hearing_protection_attenuation_attack_time_set` |
| `hpar` | hearing protection release time | `dap_cpdp_hearing_protection_attenuation_release_time_set` |
| `hpat` | hearing protection rms attenuation target | `dap_cpdp_hearing_protection_rms_attenuation_target_set` |
| `hptw` | hearing protection rms window length | `dap_cpdp_hearing_protection_rms_averaging_time_win_len_set` |

## Массивы и конфигурации

| Код | Значение | Внутренняя функция |
|---|---|---|
| `gebs` | полосы графического эквалайзера | `dap_cpdp_graphic_equalizer_bands_set` |
| `iebs` | полосы intelligent eq | `dap_cpdp_ieq_bands_set` |
| `aobs` | полосы audio optimizer | `dap_cpdp_audio_optimizer_bands_set` |
| `pobs` | полосы process optimizer | `dap_cpdp_process_optimizer_bands_set` |
| `arbs` | regulator tuning | `dap_cpdp_regulator_tuning_set` |
| `arsa` | regulator stress amount | `dap_cpdp_regulator_stress_amount_set` |
| `rssa` | regulator sibilance suppress amounts | `dap_cpdp_regulator_sibilance_suppress_amounts_set` |
| `hvrc` | конфигурация наушникового виртуализатора (8 значений) | `dap_cpdp_advanced_headphone_virtualizer_rendering_config_set` |
| `svrc` | конфигурация динамикового виртуализатора (8 значений) | `dap_cpdp_advanced_speaker_virtualizer_rendering_config_set` |
| `rvrc` | вариант конфигурации виртуализатора | `*_virtualizer_rendering_config_set` |
| `vbct` | virtual bass compressor tuning (7 значений) | `dap_cpdp_virtual_bass_compressor_tuning_set` |
| `bmdt` | bass mbdrc tuning (80 значений) | `dap_cpdp_bass_mbdrc_tuning_set` |
| `vbmf` | virtual bass mix freqs | `dap_cpdp_virtual_bass_mix_freqs_set` |
| `vbmx` | virtual bass mix frequency | `dap_cpdp_virtual_bass_mix_frequency_set` |
| `vbsf` | virtual bass source freqs | `dap_cpdp_virtual_bass_src_freqs_set` |
| `vbsg` | virtual bass subgains | `dap_cpdp_virtual_bass_subgains_set` |
| `vbhg` | virtual bass harmonic gains | `dap_cpdp_virtual_bass_harmgains_set` |
| `vbhm` | virtual bass hybrid gains | `dap_cpdp_virtual_bass_hybgains_set` |
| `vbog` | virtual bass overall gain | `dap_cpdp_virtual_bass_overall_gain_set` |
| `vbrg` | virtual bass rolloff gain | `dap_cpdp_virtual_bass_rolloff_gain_set` |
| `vbbg` | virtual bass blend linear gain | `dap_cpdp_virtual_bass_blend_linear_gain_set` |
| `pobs` | полосы process optimizer | `dap_cpdp_process_optimizer_bands_set` |

## Готовые значения из эталонного профиля Dolby

| Параметр | Динамик | Наушники |
|---|---|---|
| `ieon` / `iea` | 1 / 4 | 1 / 6 |
| `deon` / `dea` | 1 / 4 | 1 / 4 |
| `dvle` / `dvla` | 1 / 0 | 1 / 0 |
| `dvli` / `dvlo` | -256 / -256 | -256 / -256 |
| `vmb` | 56 | 32 |
| `ngon`, `geon`, `rvse` | 1 | 1 |
| `hpon` | 0 | 0 |
| `beon`, `bexe` | 0 | 0 |
| `hvrc` / `svrc` | 35 32568 11164 5090 0 3 3 3 | 35 32568 11164 5090 0 3 3 3 |

Примечание: менять на устройстве стоит в первую очередь флаги включения и числовые параметры
громкости и баса. Параметры-массивы (полосы эквалайзера, коэффициенты виртуализатора) имеют сложный
формат и требуют отдельной проверки на конкретном устройстве.
