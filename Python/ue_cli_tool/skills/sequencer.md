# Level Sequence 过场动画工作流

## 创建 Level Sequence

```
sequencer_create LS_Intro --package_path /Game/Cinematics
```

## 查看序列信息

```
sequencer_describe --sequence_path /Game/Cinematics/LS_Intro
```

返回绑定列表、轨道数量、播放范围等。

## 绑定场景中的 Actor

```
sequencer_add_possessable --sequence_path /Game/Cinematics/LS_Intro --actor_name BP_MainCharacter
```

将关卡中的 Actor 绑定到 Sequence，后续可为其添加动画轨道。

## 添加轨道

```
sequencer_add_track --sequence_path /Game/Cinematics/LS_Intro --binding_name BP_MainCharacter --track_type Transform
```

支持的轨道类型：Transform、Visibility、SkeletalAnimation 等。

## 设置播放范围

```
sequencer_set_range --sequence_path /Game/Cinematics/LS_Intro --start_frame 0 --end_frame 300
```

## 典型工作流

```
1. sequencer_create           → 创建 Sequence 资产
2. sequencer_add_possessable  → 绑定场景 Actor
3. sequencer_add_track        → 为绑定添加动画轨道
4. sequencer_set_range        → 设置播放范围
5. sequencer_describe         → 确认最终状态
```

## 提示

- 绑定 Actor 前确保 Actor 已在当前关卡中
- 帧数基于 Sequence 的帧率（通常 30fps），300 帧 = 10 秒
- 用 `sequencer_describe` 随时检查绑定和轨道状态