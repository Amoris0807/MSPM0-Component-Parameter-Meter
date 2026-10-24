# 接线说明

## AD9833

| AD9833 | 天猛星 MSPM0G3507 |
|---|---|
| SYNC / FSYNC | PA12 |
| CLK / SCLK | PA13 |
| DATA / SDATA | PA14 |
| V+ | 3.3 V |
| GND | GND |

AD9833 模拟输出通过 SMA 接入模拟前端板 J1。AD9833、开发板和模拟前端必须共地。

## 模拟前端 J3

| J3 | 网络 | MSPM0G3507 |
|---:|---|---|
| 1 | GND | GND |
| 2 | ADC_EXC_IN | PA27 / ADC0 |
| 3 | GND | GND |
| 4 | ADC_SENSE_IN | PA15 / ADC1 |
| 5 | 3V3_IN | 3.3 V |
| 6 | +5V_RELAY | 5 V |
| 7 | RANGE_4R7_CTRL | PA8 |
| 8 | RANGE_47R_CTRL | PA9 |
| 9 | RANGE_470R_CTRL | PB4 |
| 10 | RANGE_4K7_CTRL | PB5 |
| 11 | NC | 不接 |
| 12 | NC | 不接 |

两根 ADC 杜邦线应短且尽量等长，并与继电器控制线和 AD9833 数字线分开。

## 被测元件

待测电容或电感接在 J2-1（EXC_OUT）与 J2-2（SENSE_NODE）之间。J2 两端都不是固定 GND。

## 上电顺序

1. 断电完成全部接线，确认 3.3 V、5 V 与 GND 无短路。
2. 不接被测元件，上电检查 3.3 V、5 V 和约 1.65 V 的 VCM。
3. 确认所有继电器默认释放。
4. 接入 AD9833，检查 EXC_OUT 是以约 1.65 V 为中心的正弦波。
5. 断电后接入被测元件，再重新上电测量。
