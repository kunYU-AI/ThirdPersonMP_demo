# ThirdPersonMP_demo

基于 Unreal Engine **5.4 官方 Third Person C++ 模板**改造的**多人第三人称射击 Demo**。

目标是用纯 C++ 把官方单机模板扩展成「两个玩家能在同一局里互相看见、各持一把枪、开枪、掉血、击杀」的可联机原型。

> 仓库只跟踪 `Source/` 下的 C++ 代码与少量工程配置；`Content/`（蓝图、美术资源）、`Binaries/`、`Intermediate/`、`Saved/` 等由编辑器生成的内容不入库。

---

## 已实现的功能

| 模块 | 说明 |
| --- | --- |
| 移动与视角 | Enhanced Input 驱动，第三人称弹簧臂相机（沿用官方模板） |
| 多人同步 | 角色、武器、血量、死亡状态全部走网络复制，多人 PIE / 打包联机均可见 |
| 武器系统 | 武器是独立 Actor，生成后挂到角色骨骼 Socket 上；射速、伤害、是否连发均可配置 |
| 射击 | 准星射线做服务器端 hitscan 判定，同时生成子弹 Actor 从枪口飞向准星命中点 |
| 伤害与死亡 | 服务器权威扣血；血量归零 → 禁用输入 + 布娃娃(Ragdoll)瘫倒 |
| 表现 | 开火蒙太奇、子弹拖尾 Niagara、命中特效与音效、屏幕血量提示 |

---

## 环境与依赖

- **Unreal Engine 5.4**（C++ 项目，`EngineAssociation = 5.4`）
- 模块依赖见 `Source/ThirdPersonMP_demo/ThirdPersonMP_demo.Build.cs`：
  `Core / CoreUObject / Engine / InputCore / EnhancedInput / Niagara`

需要自行在编辑器里准备以下资源（C++ 只暴露属性，美术与配置都在蓝图里）：

| 资源 | 用途 |
| --- | --- |
| `BP_ThirdPersonCharacter` | 角色蓝图，作为 GameMode 的默认 Pawn，上面配输入 IMC/IA 与角色动画 |
| `BP_Rifle` | 继承 `ATPMPRifle`，配置枪口 Socket、开火蒙太奇、子弹类 |
| `BP_Projectile` | 子弹蓝图，配置拖尾/命中 Niagara 特效与命中音效 |

---

## 代码结构

| 文件 | 类型 | 职责 |
| --- | --- | --- |
| `ThirdPersonMP_demoCharacter.h/.cpp` | `AThirdPersonMP_demoCharacter` | 玩家角色：移动/视角/开火输入、血量与死亡处理、持枪朝向控制、武器生成与持有 |
| `TPMPWeapon.h/.cpp` | `ATPMPWeapon`（Abstract）、`FTPMPAimData` | 武器基类：挂点装备、开火节奏与连发、hitscan 判定与伤害结算、开火蒙太奇广播 |
| `TPMPRifle.h/.cpp` | `ATPMPRifle` | 步枪，重写枪口/挂点 Socket 名与默认射速伤害 |
| `ThirdPersonMPProjectile.h/.cpp` | `AThirdPersonMPProjectile` | 子弹 Actor：匀速飞行、拖尾 Niagara、命中特效与音效、超时自动销毁 |
| `ThirdPersonMP_demoGameMode.h/.cpp` | `AThirdPersonMP_demoGameMode` | 指定默认 Pawn 为 `BP_ThirdPersonCharacter` |
| `ThirdPersonMP_demo.h/.cpp` | 模块入口 | 主模块声明 |

---

## 网络设计

### 权威模型

**伤害结算、子弹生成、命中判定、死亡处理全部只在服务器执行**，客户端只负责「上报输入」和「播放表现」。
客户端上报的不是「打中了谁」，而是「准星指向哪里」——判定权始终留在服务器。

### RPC 一览

| RPC | 定义在 | 方向 | 可靠性 | 用途 |
| --- | --- | --- | --- | --- |
| `Server_StartFire(AimData)` | Weapon | 客户端 → 服务器 | Reliable | 扣扳机，携带瞄准数据 |
| `Server_StopFire()` | Weapon | 客户端 → 服务器 | Reliable | 松扳机，停止连发 |
| `Server_UpdateAimPoint(AimData)` | Weapon | 客户端 → 服务器 | Unreliable | 连射期间每帧刷新瞄准点 |
| `Server_SpawnAndEquipDefaultWeapon()` | Character | 服务器本地调用 | Reliable | 服务器生成默认武器并装备 |
| `Multicast_PlayFireFX()` | Weapon | 服务器 → 所有客户端 | Unreliable | 播开火蒙太奇 |
| `Multicast_PlayImpactFX(Loc, Normal)` | Projectile | 服务器 → 所有客户端 | Reliable | 命中点播 Niagara 特效 + 音效 |
| `Multicast_PlayDeath()` | Character | 服务器 → 所有客户端 | Reliable | 死亡表现（布娃娃等） |

### 复制属性

| 属性 | 所属 | 说明 |
| --- | --- | --- |
| `CurrentHealth` / `bIsDead` | Character | 血量与死亡标记（`ReplicatedUsing` 回调） |
| `CurrentWeapon` | Character | 当前武器，复制后客户端即可自动跟随挂点 |
| `bIsTriggerHeld` / `OwnerCharacter` | Weapon | 是否按住扳机、武器归属角色 |

角色开启 `bReplicates + SetReplicateMovement`，并使用 `Exponential` 网络平滑；子弹开启 `bAlwaysRelevant`，保证远处的玩家也能看到弹道与命中特效。

### 开火流程

```text
客户端(本地)                        服务器                              其他客户端
按住开火键
  │ ComputeAimData()：摄像机射线 → ImpactPoint
  └─ Server_StartFire(AimData) ─────►
                                     立即射一发，并开启 Tick 连发
                                     HandleFire_Server()：
                                       ① 用 AimData 再做一次 hitscan 校验
                                       ② 命中者 ApplyPointDamage 造成伤害
                                       ③ 从枪口生成子弹，方向指向命中点
                                     ├─ Multicast_PlayFireFX() ─────────► 播开火蒙太奇
                                     └─ SpawnActor<Projectile> ──复制──► 看到子弹飞行 + 拖尾
  连射期间每帧 Server_UpdateAimPoint(AimData) ─► 刷新瞄准点(Unreliable)
```

服务器 Tick 里按 `FireRate` 控制节奏重复执行 `HandleFire_Server()`，实现连发。

### 死亡流程

```text
服务器: ApplyPointDamage → TakeDamage() → SetCurrentHealth(≤0)
        → bIsDead = true → HandleDeath()
        └─ Multicast_PlayDeath() ─► 所有端：禁用输入 / 开启布娃娃 / 关闭胶囊碰撞 / 停止移动
```

---

## 关键实现点

- **准星指哪打哪**：客户端每帧从摄像机沿视线做一次射线，把命中点作为 `FTPMPAimData` 上报；服务器用同一条射线二次校验，命中点既是伤害目标也是子弹飞行方向 → 视觉弹道与判定结果一致。
- **子弹不结算伤害**：子弹 Actor 只负责表现（飞行、拖尾、命中特效），伤害完全由服务器 hitscan 结算，避免同一发子弹出两次伤害。
- **子弹不参与准星射线**：子弹的 `SphereComponent` 对 `ECC_Visibility` 设为 `Ignore`，否则玩家自己的射线会被飞出去的子弹挡住。
- **忽略过近命中**：射线命中距离小于 `150`（俯视时可能落在脚边）会被跳过，避免子弹扎地。
- **高频数据走 Unreliable**：瞄准点每帧上报，用 `Unreliable` 避免可靠通道拥塞；扣扳机/停火这种状态切换才用 `Reliable`。
- **一次性事件用 Multicast 而非 OnRep**：死亡、开火、命中都属于「一次性事件」，用 `OnRep` 可能因错过中间状态而不触发，`Multicast` 更可靠。
- **武器 Tick 按需开启**：武器 Actor 默认 `bStartWithTickEnabled = false`，只在扣扳机期间开 Tick，减少无谓开销。
- **持枪时的身体朝向**：持枪时关闭 `bOrientRotationToMovement`，按「开火中 20 / 大角度偏差 5 / 移动中 8」三档插值速率让身体对齐摄像机 Yaw，纯移动状态则恢复朝向移动方向。
- **物理与人物的碰撞分层**：角色胶囊和骨骼网格对 `ECC_Visibility` 设为 `Block`，这样准星射线才能命中玩家。

---

## 本地测试

1. 用 UE 5.4 打开工程（或右键 `.uproject` → Generate Visual Studio project files），编译 `Development Editor` 配置；
2. 编辑器 Play 下拉菜单：`Number of Players = 2`、`Net Mode = Play As Listen Server`；
3. 两个窗口各控制一个角色，按住左键即可对射——双方都能看到子弹、命中特效和对方的掉血/倒地。

**打包联机提示**：独立进程的包不像 PIE 那样复用编辑器的网络栈，需要在 `Config/DefaultEngine.ini` 里显式声明 `NetDriverDefinitions`（`GameNetDriver` → `OnlineSubsystemUtils.IpNetDriver`）以及 `[OnlineSubsystem] DefaultPlatformService=Null`，否则会出现「各打各的、看不到彼此」。另外关卡里要放够 `PlayerStart`（每个玩家一个），否则多个玩家会重叠在同一个出生点。

---

## 可继续扩展的方向

- 击杀记分板 / 玩家列表 UI
- 死亡后复活与出生点逻辑
- 武器切换、拾取、多武器类型
- 命中反馈（准星跳字、受击方向提示）
- 观战 / 分离的 GameMode 与 PlayerState 玩法规则
