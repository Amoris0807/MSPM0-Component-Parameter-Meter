# 硬件说明

## 信号链

```text
AD9833
  -> 交流耦合与 1.65 V 偏置
  -> TLV9064 激励缓冲
  -> EXC_OUT
  -> 被测元件
  -> SENSE_NODE
  -> 继电器选择参考电阻
  -> VCM
```

TLV9064 的另外两路分别缓冲 EXC_OUT 和 SENSE_NODE，经匹配的 68 Ω / 1 nF 网络送入 ADC0 与 ADC1。

## 量程

| 量程 | 参考电阻 | 控制引脚 | 当前状态 |
|---|---:|---|---|
| R1 | 4.7 Ω | PA8 | 可用 |
| R2 | 47 Ω | PA9 | 链路行为异常 |
| R3 | 470 Ω | PB4 | 可用 |
| R4 | 4.7 kΩ | PB5 | 链路异常 |

固件采用先断后合：先释放全部继电器，等待 10 ms，再吸合目标继电器并等待 20 ms 后采样。

## 测量模型

参考电阻连接在 SENSE_NODE 与 VCM 之间。固件对两路 ADC 数据进行正弦/余弦拟合，得到相量 `Vexc` 和 `Vsense`，再计算：

```text
Zx = Rref * (Vexc / Vsense - 1)
```

当虚部呈容性时：

```text
C = 1 / (2 * pi * f * -Im(Zx))
D = Re(Zx) / -Im(Zx)
```

这些结果尚未进行双通道增益、相位、继电器触点和寄生参数校准，因此只能作为实验数据。

## 设计文件

- `hardware/schematic.png`：当前原理图截图
- `hardware/easyeda/component-meter.epru`：嘉立创 EDA Pro 工程，包含原理图和 PCB
