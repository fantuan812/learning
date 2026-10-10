---
type: Concept
title: "11 RenderTarget 与 SceneCapture 实战（RT & SceneCapture In-Depth）"
description: "从捕获、材质与Canvas写入到显示和读回，说明纹理格式、坐标、所有权、时序与失败边界。"
status: stable
verified: []
maturity: L2
updated: 2026-10-10
sources:
  - id: S01
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget2D"
  - id: S02
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ETextureRenderTargetFormat"
  - id: S03
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D"
  - id: S04
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D/CaptureScene"
  - id: S05
    resource: "https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D/CaptureSceneDeferred"
  - id: S06
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent"
  - id: S07
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/object-pointers-in-unreal-engine"
  - id: S08
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCanvasRenderTarget2D"
  - id: S09
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-delegates-in-unreal-engine"
  - id: S10
    resource: "https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCanvas?lang=en-US"
  - id: S11
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary/DrawMaterialToRenderTarget"
  - id: S12
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-and-render-targets-overview?application_version=4.27"
  - id: S13
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary/ReadRenderTarget"
  - id: S14
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/FRHIGPUTextureReadback"
  - id: S15
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/FRHIGPUMemoryReadback"
  - id: S16
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget/GameThread_GetRenderTargetResour-"
  - id: S17
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RenderCore/FRenderCommandFence"
  - id: S18
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine"
  - id: S19
    resource: "https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary?lang=en-US"
  - id: S20
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget2D/UpdateResourceImmediate"
  - id: S21
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ESceneCaptureSource"
  - id: S22
    resource: "https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponentCube"
---

# 11 RenderTarget 与 SceneCapture 实战（RT & SceneCapture In-Depth）

> 知识成熟度：L2（公开官方资料静态核对；完整使用层教学例与读回设计合同，均未运行）。
> 版本基准：2026-10-10 实际读取的 Epic 公开 API 页面标题标为 UE 5.8，但未锁定底层源码 revision；S12 是明确固定 UE 4.27 的历史绘制限制说明，不能当作 5.8 实现核验。这里不继承旧稿“本机 UE5.8.0 / CL55116800 全文已核对”的证据声明。
> 最后更新：2026-10-10
> 适用范围：客户端小地图、监控屏、Canvas 雷达、脚印/衰减画布及 CPU 截图接口选型；主要示例限定单个 World、单个拥有者、固定尺寸和普通 2D RT。
> 证据范围：公开 API/文档选段、已有正文与 Git 历史静态核对。未读取受限引擎实现，未运行 UE、UHT、编译、Shader、GPU、读回、截图、模型、性能、PowerShell、配置或目标网络实验。下文所有 PAPER_EXPECTED、代码和操作均是未执行方案。

## 1. 先把写入者、消费者和完成条件分开

RT 是渲染输出能继续作为纹理使用的载体；它本身不决定拍摄哪个视角。SceneCapture 从一个视角生成画面；材质绘制生成一个覆盖 RT 的面片结果；Canvas 提供线、文字等 2D 绘制入口。需要显示到 UMG 或场景材质时，让 GPU 直接采样 RT 即可，通常不需要绕 CPU 读回。[UTextureRenderTarget2D（S01）][S01] [DrawMaterialToRenderTarget（S11）][S11]

| 需求 | 生产者 → 消费者 | 首先约束什么 |
| --- | --- | --- |
| 俯视小地图 | 正交 SceneCapture2D → RT → UI 材质 | 世界覆盖范围、更新频率、屋顶遮挡、图标坐标 |
| 监控屏 | 透视 SceneCapture2D → RT → 屏幕材质 | 相机位置与 FOV、屏幕反馈、颜色链 |
| 雷达图标/网格 | Canvas → RT → UI | 持有引用、绘制回调、像素坐标、变更时刷新 |
| 脚印/衰减痕迹 | 上一状态 RT → 材质 → 下一状态 RT | 输入输出分离、清屏、参数和交换次序 |
| 历史颜色冻结/残影 | 颜色捕获 → 历史颜色 RT → 冻结显示或与当前颜色混合的下游材质 | 帧身份、曝光/颜色空间、坐标对应、历史输入与当前写入分离 |
| 深度/法线效果输入 | 已支持的深度/法线捕获 → 数据 RT → 描边或遮挡相关的二次材质处理 | 单位/编码/坐标空间、投影与 UV 对齐、实际路径能力 |
| 截图/外部图像处理 | 捕获结果 → staging/readback → CPU 副本 | 对应哪次捕获、格式、完成信号、容量与取消 |

Cube 捕获面向六个方向；Volume 和 2DArray 分别表达体积与切片数据，不能把本例的二维尺寸、行跨度和相机投影照搬过去。CanvasRenderTarget2D 派生自 TextureRenderTarget2D，不是与 2D RT 并列的另一种维度。运行时创建的对象与内容资产都要有明确的持有者；内容资产方便配置，运行时实例适合独立尺寸、独立生命周期。[S08][S08] [S19][S19] [S22][S22]

SceneCapture 增加额外渲染工作，但“每次必然完整再渲染主视图一遍”“必然帧率腰斩”都不是接口合同。捕获源、可见集合、路径、主渲染器集成和更新方式会改变实际工作；5.8 页面还公开了 bRenderInMainRenderer 等条件能力，不能从“有一次 CaptureScene 调用”推定 pass 数量。[S03][S03]

## 2. 三层资源寿命，以及四种不同的完成

把以下对象看成三个不同问题：

1. UObject 引用：`UPROPERTY` 标记的 `TObjectPtr` 可使被引用 UObject 保持可达。只把 NewObject 的 Outer 设为 Actor、保存一个局部裸指针，不能代替明确的可达引用。[Object Pointers（S07）][S07]
2. 渲染侧资源：把 `FTextureRenderTargetResource*` 捕获进 lambda，并没有延长资源寿命。`GameThread_GetRenderTargetResource` 的公开合同只允许 GT 传递和判空该指针，不能据此在 GT 任意解引用它。[S16][S16]
3. 底层 GPU 存储：即使某个 CPU 命令已结束，GPU 仍可能使用纹理或 staging 分配。自定义读回任务必须维持正确的资源引用、顺序和退场协议；UObject 可达也不保证它不会被 Resize、重建或显式释放。[Threaded Rendering（S18）][S18]

| 事件 | 能说明什么 | 不能推出什么 |
| --- | --- | --- |
| `CaptureScene()` 返回 | 捕获调用已返回；API 与 Deferred 调用的调度语义不同 | GPU 已写完、CPU 数组已有数据、图像已显示 |
| 某个渲染命令 lambda 返回 | 这个 CPU 命令结束了 | 后续 RHI/GPU 对纹理的访问结束 |
| 覆盖复制的 readback `IsReady()` 为真 | 该 readback 的数据可以开始读取 | 任意另一个捕获已完成，或所有使用该纹理的 GPU 工作都已结束 |
| Lock → 拷入独立 CPU 内存 → Unlock | 应用不再依赖映射指针 | 屏幕已经呈现这幅图像 |

`CaptureScene` 文档的 immediately 是相对 Deferred 的调用时机描述，不是 GPU 栅栏。`CaptureSceneDeferred` 等到下一次主视图渲染；没有该次主视图时不能靠等待固定毫秒数宣告成功。自动每帧捕获与手动调用应避免混用，官方明确提示这会重复渲染；DetailMode 剔除也可能使请求不产生捕获。[S04][S04] [S05][S05]

`FRenderCommandFence` 用于跟踪渲染命令，其同步深度要结合实际版本与参数核对。本次 5.8 API 显示 `BeginFence(ESyncDepth)`，没有读取实现，不能拿旧版 `BeginFence` 调用或一个完成布尔值担保全部 GPU 访问结束。[S17][S17] 线程与 GPU 退场的通用解释见[渲染管线概览](01-渲染管线概览.md)。

PAPER_EXPECTED P01：N 号请求入队后立即销毁 Actor；渲染线程稍后才取资源，GPU 更晚执行拷贝。若只捕获资源裸指针或以 GT 返回作为释放条件，生命周期没有覆盖消费者。正确的使用层例只交给引擎公开组件/纹理管理，不缓存渲染侧裸指针；自定义读回则由独立任务持有者继续保管资源，直到第 6 节的退场条件成立。两者不能混为一个“加 UPROPERTY 就都安全”的修复。

## 3. 格式、颜色、尺寸是三份合同

### 3.1 纹理格式不等于输出含义

下表的 payload 是单 mip、单采样、无压缩的名义 texel 存储量；不包含对齐、额外缓冲、mip、深度、驱动分配和读回 staging。8 位定点 RT 在采样时表达归一化范围，不应把 CPU 字节的 0–255 直接写成材质采样值。[ETextureRenderTargetFormat（S02）][S02]

| RT 格式 | 名义字节/texel | 采样表达 | 选择理由与限制 |
| --- | ---: | --- | --- |
| `RTF_R8` | 1 | 单通道归一化 0–1 | 遮罩；不适合有符号或大范围高度 |
| `RTF_RGBA8` / `RTF_RGBA8_SRGB` | 4 | 四通道归一化 0–1 | 颜色/低精度数据；两种编码用途要区分 |
| `RTF_R16f` | 2 | 单通道半精度浮点 | 较紧凑连续值，精度与范围有界 |
| `RTF_RGBA16f` | 8 | 四通道半精度浮点 | HDR 中间颜色；不自动生成 Bloom 或反射 |
| `RTF_R32f` | 4 | 单通道单精度浮点 | 需要更高精度的数值数据；仍需定义单位 |

`RenderTargetFormat` 是公开的格式选择，`InitCustomFormat` 接收 `EPixelFormat` 并有 `bInForceLinearGamma`。不要混写两套枚举，也不要认为修改一个 gamma 布尔值就完成了曝光、色调映射、采样解码与显示编码的全部配置。[S01][S01]

### 3.2 先记录捕获源，再决定怎么显示或读数

SceneCaptureSource 枚举包含 SceneColorHDR、FinalColorLDR、FinalColorHDR、深度、法线和 BaseColor 等。公开枚举页没有解释所有通道打包、alpha 含义或每条路径的支持情况，本次没有读取实现，因此不把 BaseColor 当作所有渲染路径都可用的无光照万能方案，也不将 SceneDepth/DeviceDepth 当作相同单位。[S21][S21]

为每张 RT 写清：生产者及 CaptureSource → 数值/单位与颜色编码 → RT 格式/gamma → 材质采样解释 → UI/显示或文件编码。场景线性 HDR、经过映射的颜色和显示信号不能随意互换；曝光与显示映射详见[后处理与画面特效](05-后处理与画面特效.md)。

PAPER_EXPECTED P02：某“高度”通道约定值为 -0.25、0.5、2。若写进只能表达 0–1 的 R8，三个输入不可能完整保留原数值；改成浮点 RT 只解决存储范围，单位、材质输出和读回转换仍须保持一致。反过来，用 HDR 格式保存已裁剪的 LDR 输入，不能恢复被裁掉的高光。

官方 `ReadRenderTarget` 返回 8 位 BGRA sRGB 数据，并对 LDR/HDR 输入采取不同的颜色假设；它不是逐位原始纹理读回。`ReadRenderTargetRaw` 提供另一种读取入口，但它也不是任意 GPU 格式均可按 `FColor` 强转的承诺。用于科学/玩法数值时必须确认支持格式、归一化选择和输出类型。[S13][S13] [S19][S19]

### 3.3 尺寸、UV 与世界位置

在普通独立正交捕获、无 overscan/自定义投影/平面调整的简化模型中，设水平世界覆盖为 Lx，RT 宽高为 W、H，则 Ly=Lx×H/W。定义相机平面的右向量 R、上向量 U、中心 C，世界点 P 的纸面映射是：

```text
u = 0.5 + dot(P-C, R) / Lx
v = 0.5 - dot(P-C, U) / Ly
落在 [0,1]×[0,1] 外：本例隐藏图标，不环绕
像素中心 (i,j) 对应 UV=((i+0.5)/W, (j+0.5)/H)
```

这里主动约定图像左上为原点、v 向下，不能把主摄像机屏幕坐标或 UMG DPI 坐标直接当 RT 像素。P03 取 W=512、H=256、Lx=4096，则 Ly=2048；P=C 得 (0.5,0.5)，沿 R 移动 1024 得 u=0.75。若只改 RT 高度而仍按正方形覆盖计算，图标就会拉伸或错位。

这只是正交纸面投影：实际裁剪、朝向和纹理上下方向要用四角不同颜色的标记验收；透视必须使用该次捕获的 view/projection 并处理齐次除法与相机后方点。5.8 的主视图分辨率/相机继承与正交分块有额外条件，默认教学例不启用它们。[S03][S03]

## 4. 有生命周期的小地图与 Canvas 雷达例

下面是原创的两个文件完整教学例。项目模块已有 Core、CoreUObject、Engine 依赖，模块内使用，因此类声明不放示例项目的导出宏；跨模块时按项目真实模块名添加导出宏。未做 UHT/编译/运行验证。

条件：Actor 放在目标平面上方，局部俯视；固定 512×512 LDR 捕获，独立 256×256 Canvas；每 0.1 秒最多提交一次捕获，同时刷新扫描线。这里的 0.1 秒是教学节奏，不是硬件容量或完成承诺。API依据为 S03、S04、S08、S09、S10、S19。

```cpp
// RenderTargetLessonActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "RenderTargetLessonActor.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UCanvasRenderTarget2D;
class UCanvas;

UCLASS()
class ARenderTargetLessonActor : public AActor
{
    GENERATED_BODY()
public:
    ARenderTargetLessonActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="RT Lesson")
    TObjectPtr<USceneCaptureComponent2D> Capture;

    UPROPERTY(Transient, BlueprintReadOnly, Category="RT Lesson")
    TObjectPtr<UTextureRenderTarget2D> MapRT;

    UPROPERTY(Transient, BlueprintReadOnly, Category="RT Lesson")
    TObjectPtr<UCanvasRenderTarget2D> RadarRT;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void UpdateImages();
    UFUNCTION()
    void DrawRadar(UCanvas* Canvas, int32 Width, int32 Height);

    FTimerHandle RefreshTimer;
    bool bRunning = false;
    bool bUpdating = false;
    bool bDrawing = false;
    float ScanAngle = 0.0f;
};
```

```cpp
// RenderTargetLessonActor.cpp
#include "RenderTargetLessonActor.h"
#include "Camera/CameraTypes.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/GuardValue.h"

ARenderTargetLessonActor::ARenderTargetLessonActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
    Capture->SetupAttachment(GetRootComponent());
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = 4096.0f;
    Capture->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

void ARenderTargetLessonActor::BeginPlay()
{
    Super::BeginPlay();
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;

    // 明确重新关闭自动模式，避免派生蓝图的配置与手动模式重复。
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->bMainViewResolution = false;
    Capture->bMainViewCamera = false;
    Capture->bMainViewFamily = false;
    Capture->bRenderInMainRenderer = false;
    Capture->bEnableOrthographicTiling = false;
    MapRT = UKismetRenderingLibrary::CreateRenderTarget2D(
        this, 512, 512, RTF_RGBA8, FLinearColor::Black, false, false);
    RadarRT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
        this, UCanvasRenderTarget2D::StaticClass(), 256, 256);
    if (!MapRT || !RadarRT)
    {
        UE_LOG(LogTemp, Warning, TEXT("RT lesson: render target creation failed"));
        MapRT = nullptr;
        RadarRT = nullptr;
        return;
    }

    Capture->TextureTarget = MapRT;
    RadarRT->SetShouldClearRenderTargetOnReceiveUpdate(true);
    RadarRT->OnCanvasRenderTargetUpdate.AddDynamic(
        this, &ARenderTargetLessonActor::DrawRadar);
    bRunning = true;
    UpdateImages();
    GetWorldTimerManager().SetTimer(
        RefreshTimer, this, &ARenderTargetLessonActor::UpdateImages, 0.1f, true);
}

void ARenderTargetLessonActor::UpdateImages()
{
    if (!bRunning || bUpdating || !Capture || !MapRT || !RadarRT) return;
    TGuardValue<bool> UpdateGuard(bUpdating, true);
    ScanAngle = FMath::Fmod(ScanAngle + 0.15f, 2.0f * PI);
    Capture->CaptureScene();        // 提交捕获，不报告 GPU 完成。
    RadarRT->UpdateResource();      // 触发 Canvas 更新，不是 CPU 读回。
}

void ARenderTargetLessonActor::DrawRadar(UCanvas* Canvas, int32 Width, int32 Height)
{
    if (!bRunning || bDrawing || !Canvas || Width <= 0 || Height <= 0) return;
    TGuardValue<bool> DrawGuard(bDrawing, true);
    const FVector2D Center(Width * 0.5f, Height * 0.5f);
    const float OuterRadius = FMath::Min(Width, Height) * 0.4f;
    constexpr int32 Segments = 48;
    for (int32 Ring = 1; Ring <= 2; ++Ring)
    {
        const float Radius = OuterRadius * (0.5f * Ring);
        for (int32 I = 0; I < Segments; ++I)
        {
            const float A = 2.0f * PI * I / Segments;
            const float B = 2.0f * PI * (I + 1) / Segments;
            Canvas->K2_DrawLine(
                Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius,
                Center + FVector2D(FMath::Cos(B), FMath::Sin(B)) * Radius,
                1.0f, FLinearColor::Green);
        }
    }
    Canvas->K2_DrawLine(Center,
        Center + FVector2D(FMath::Cos(ScanAngle), FMath::Sin(ScanAngle)) * OuterRadius,
        2.0f, FLinearColor::Green);
    // 此回调内不能再次 UpdateResource，也不保留 Canvas 指针供下一次使用。
}

void ARenderTargetLessonActor::EndPlay(const EEndPlayReason::Type Reason)
{
    bRunning = false;
    if (GetWorld()) GetWorldTimerManager().ClearTimer(RefreshTimer);
    if (Capture)
    {
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->TextureTarget = nullptr;
    }
    if (RadarRT)
        RadarRT->OnCanvasRenderTargetUpdate.RemoveDynamic(
            this, &ARenderTargetLessonActor::DrawRadar);
    RadarRT = nullptr;
    MapRT = nullptr;
    Super::EndPlay(Reason);
}
```

这段例子的责任边界：

- Actor 用反射属性持有两张纹理；动态多播委托使用 `UFUNCTION` 与 AddDynamic/RemoveDynamic，不能把这里的 BlueprintAssignable 委托直接当作普通 AddLambda 委托。[S08][S08] [S09][S09]
- 回调只画当前 Canvas；两圈由闭合的 48 段折线近似，最后一段回到起点。BoxItem 只能表示框，不能拿它冒充圆。
- `bUpdating`/`bDrawing` 只防本对象的同步重入，不是 GPU busy 标志；也不提供多线程互斥。所有公开 UObject 操作都限定在 GT。
- 销毁时先关入口、清计时器、解绑回调、断开目标引用，然后交给引擎正常资源生命周期；本例没有私有 RHI 资源或异步读回任务，不能把这些步骤借用成自定义 readback 的安全析构证明。
- UI 若仍持有 RT，它可能继续显示最后一幅图并延长寿命。UI 拥有者负责移除自己的材质/纹理引用；不要调用全局清理或抢先手动 ReleaseResource 以求“立刻回收”。

### 4.1 让输出进入 UI，并规定验收输入

纸面连接步骤：创建 User Interface Domain 材质，TextureSampleParameter2D 参数名为 `CaptureTexture`，RGB 接 Final Color；本例用不透明显示，Opacity 设 1，不借用捕获 alpha。Widget 自己创建并持有该材质实例，把 Actor 的 MapRT 设为参数，再交给 Image 的材质画刷。RadarRT 用另一个独立实例显示；不要让两个实例意外共用并互相覆盖同一参数。按实际纹理的线性/sRGB设置选择匹配采样方式，不能凭“更亮”判断哪条颜色链正确。

PAPER_EXPECTED P04：场景平面放红、绿、蓝、白四个方块，Actor 在其上方；UI 显示整个 RT，雷达显示两圈和一条旋转线。先固定曝光与捕获位置检查顺序和遮挡，再检查数值/色彩。平面全黑不唯一指向 CaptureSource：未绑定目标、相机朝向、屋顶遮挡、裁剪、渲染路径支持与曝光都需分开排查。不同颜色能认出来，只证明基本通路可见，不证明色彩正确。

小地图图标可在 UI 上按第 3.3 节映射叠加，没必要每次把图标也重渲染为 3D 场景。绘制静态雷达时去掉计时器刷新，只在数据变化后 UpdateResource。普通 Canvas 绘制省掉的是这次 3D 场景捕获，不是所有 GPU 开销。

监控屏可以使用固定透视相机；平面镜还要根据观察相机与镜面计算反射视点/方向，并处理投影和镜面裁剪，不能只给屏幕贴上RT就声称完成镜面反射。S03提供ClipPlaneBase/Normal等入口，但本例未实现镜面相机、传送门或相互可见镜面的递归协议。Cube环境捕获则须匹配TextureRenderTargetCube与已注册的SceneCaptureComponentCube：先持有和绑定目标、关闭自动触发、按需CaptureScene，再由匹配Cube采样的消费者读取；停止时仍按入口、消费者与引擎资源寿命顺序收尾。六个方向带来额外工作，不等于固定六倍GPU耗时，也不自动生成可直接替代所有反射系统的烘焙资产。[S03][S03] [S22][S22]

历史颜色也可作为后续材质的输入。一个有限的冻结/残影用途是：在固定相机、固定投影和尺寸下，先取得身份明确的一次颜色捕获作为历史 H；冻结时停止更新 H，仍由材质显示它；残影时另用当前颜色 C 与 H 做有意的历史混合，再写入独立输出 D。H/C 的曝光与颜色空间必须可比较，采样 UV 必须对应同一图像范围，不能一边覆写 H 一边把它当旧帧采样；更新历史还需遵守捕获生产者和消费者的先后关系。相机移动后的准确重投影、运动矢量与遮挡消歧不在此有限用途内，本段不声称已实现完整残影效果。[S03][S03] [S11][S11]

深度/法线 RT 可向下游材质提供描边或遮挡相关的处理输入，但前提是目标渲染路径确实支持所选捕获源。消费前记录深度的单位和编码、法线的坐标空间和打包方式，以及该次捕获的投影、尺寸与 UV；不能把 DeviceDepth 当作线性世界距离，也不能把法线数据按显示颜色做 gamma 转换。颜色和数据来自不同捕获时，还须确认帧与视图一致，再把处理结果写入独立输出。能力或语义未确认、结果缺失时停止该效果并报告输入不可用，不把黑图解释为合法深度或法线。这里仅保留数据供材质二次处理的用途，具体解码、描边阈值和遮挡判定未实现；S21 的枚举名单本身不足以证明这些能力。[S21][S21]

## 5. Ping-Pong：一个有边界的衰减痕迹画布

保留旧画面并读取邻域时，读写同一张 RT 会形成反馈冲突。Epic 的 4.27 绘制说明明确要求采样输入与目标分离，或在不采样目标的前提下用适当 blending；这里选择双 RT，并以现行 DrawMaterialToRenderTarget 的公开入口构造教学流程。[S12][S12] [S11][S11]

本例只演示衰减和印章合成，不声称它已是流体模拟。真正波动方程还要定义时间步长、边界条件、稳定性、更多历史状态与数值精度。

输入合同：同一个管理 Actor 的反射属性持有 A、B 和更新 MID；A/B 是两个不同的 256×256 RTF_R16f、线性数据 RT，均先清为 0，当前读者为 A。材质是 Surface/Unlit/Opaque，结果写 Emissive；不用场景光照或 WorldPosition 猜平面坐标。Texture 参数 `Previous` 采样 R，Scalar `Decay` 属于 [0,1]，Vector `Stamp` 的 xy 是 UV、z 是半径，Scalar `Strength` 属于 [0,1]。初始 `Strength=0`、`Decay=1` 是中性状态。

```text
UV = TextureCoordinate0
old = sample(Previous, UV).r                // 线性数据采样、Clamp，禁止读取本次 Dest
q = length(UV - Stamp.xy) / Stamp.z         // 要求 Stamp.z > 0
brush = 1 - saturate(q)                    // 圆形线性衰减印章
next = saturate(max(old * Decay, brush * Strength))
Emissive = (next, next, next)
```

完整的纸面一次更新步骤：

1. 只在 GT、World 有效、未停止且不在更新回调中接受。核对 A/B 非空、实例不同、尺寸与格式相同；参数含 NaN/无穷、半径≤0或越界强度直接拒绝，不提交也不交换。
2. 每帧至多处理一个更新；重复输入先在 CPU 合并或保留最新一个，队列容量固定为 1。该选择是本例业务取舍，不是通用丢帧策略。
3. 本帧确定 Source 和 Dest，将 Source 绑定 `Previous`，设置本次不可变参数，调用 `DrawMaterialToRenderTarget(this, Dest, UpdateMID)`；不用 `GWorld` 猜测当前世界。
4. 调用返回后交换逻辑 Source/Dest，并把显示材质的纹理参数切向新的 Source。这表示后续命令中的逻辑角色已交换，不意味着 GPU 已完成。后续写入必须处在引擎绘制 API 的有序使用范围内；若要跨线程、跨 renderer 或异步计算，必须另核对依赖和资源屏障。
5. 本例不在一个帧内重复修改同一个更新 MID 并提交多次绘制；需要多笔严格快照时使用各次独立参数/实例和已证明的执行次序。下一步也不得从 Canvas 回调重入这一更新。
6. 关闭时停止接受新笔触与调度、解绑自己的显示入口，保留已有提交依赖的资源至引擎正常退场；尺寸变化采用停止生产、等待相应使用者退场后重建两张并清零的路径，不在有自定义 readback 的 RT 上就地 Resize。

DrawMaterialToRenderTarget 没有成功结果参数，以上只能检查提交前条件；材质未编译、平台格式不支持或图形设备错误时，不能根据“函数返回了”递增成功计数。应用可将状态标为“已请求第 N 次更新”，显示验收或读回另行证明结果。

PAPER_EXPECTED P05：某像素 old=0.8，Decay=0.5，无新印章，下一状态=0.4；再做一次=0.2。第二次必须读第一次写出的另一张 RT。若一直绑定初始化 A、只改 Dest 指针，结果不会按这条递推持续衰减；若 Source=Dest，应在提交前拒绝。此算例没有运行材质或 GPU。

同一 RT 需要画多个独立 2D 图元时，S11 建议考虑 BeginDrawCanvasToRenderTarget/EndDrawCanvasToRenderTarget；Begin/End 必须成对，Context 只属于该次绘制，不能留到下一帧或跨 World 复用。[S19][S19] 这与 UCanvasRenderTarget2D 的更新回调是两种不同入口。

## 6. CPU 读回：先选简单接口，再设计真正的异步任务

### 6.1 低频颜色快照的同步入口

对于偶发调试，公开的 `UKismetRenderingLibrary::ReadRenderTarget` 比凭空调用 `UTextureRenderTarget2D::ReadPixels` 更容易明确输入输出。下面函数只读调用者已选定的 LDR RT 内容；不触发新捕获、不证明读到某个新请求的帧号，也不保证不卡顿。[S13][S13]

```cpp
// 完整工具函数；包含 CoreMinimal.h、Engine/TextureRenderTarget2D.h、
// Kismet/KismetRenderingLibrary.h。仅在 GT、低频诊断路径调用，未编译。
bool ReadLdrPreview(UObject* WorldContext, UTextureRenderTarget2D* Target,
                    TArray<FColor>& OutPixels)
{
    OutPixels.Reset();
    if (!IsInGameThread() || !IsValid(WorldContext) || !WorldContext->GetWorld()
        || !IsValid(Target) || Target->SizeX <= 0 || Target->SizeY <= 0)
        return false;
    const int64 Count = int64(Target->SizeX) * int64(Target->SizeY);
    constexpr int64 MaxPreviewPixels = 512LL * 512LL; // 本例的容量上限
    if (Count > MaxPreviewPixels) return false;
    if (!UKismetRenderingLibrary::ReadRenderTarget(WorldContext, Target, OutPixels, false)
        || int64(OutPixels.Num()) != Count)
    {
        OutPixels.Reset();
        return false;
    }
    return true; // 仅表示接口成功且长度符合；不代表 HDR/raw 数值无损。
}
```

调用方需保证目标是约定的 LDR 颜色纹理、期间无其他写者或 Resize；输出不作为高度/深度算法输入。接口文档明确标为慢操作，没有支持固定“10–30ms”或“所有平台必排空整条 GPU 管线”的数据。把 ReadSurfaceData 放进渲染线程 lambda 只改变阻塞发生的位置，不自动获得异步 staging 与 ready 轮询协议。

### 6.2 异步读回接入设计：生产者依赖尚未实现

纹理应研究 `FRHIGPUTextureReadback`；`FRHIGPUBufferReadback` 是缓冲的对应类型，不能只因都叫 readback 就互换。已读取的公开签名包括 `EnqueueCopy`、`IsReady`、纹理 `Lock(OutRowPitchInPixels, OutBufferHeight)` 和 Unlock；尚未取得同一目标版本的捕获生产者、RDG/RHI 复制接入及平台线程约束实现。下面只给单槽接入设计：读者可以据此审查所有权、完成条件和行拷贝，不能据此完成一条已经接通的异步截图链路。第 6.3 节的退场分支也属于这份未实现设计。[S14][S14] [S15][S15]

固定条件：单 GPU、一次只允许一个任务；源为已确认支持拷贝的固定 512×512 BGRA8 二维纹理、mip 0、单采样；若实际格式、尺寸、GPU mask 或能力不符则拒绝，不能静默套用每像素 4 字节。该源由任务独占调度，关闭其他自动捕获与外部写者，在这次捕获与复制之间不能被下一次写入覆盖。队列满返回 Busy，不创建第二个 readback。任务有 RequestId、Generation、ExpectedSize/Format、独立结果缓冲、弱接收者及“结果是否已派发”状态。

管理职责要独立于发起 Actor：Actor 退场后管理者仍能处理已入队命令及资源回收。GT 管理 UObject 强引用和接收者有效性；受控渲染执行侧管理该次 RHI texture 引用、readback、状态及 Lock/Unlock。跨侧传递不可变消息，不能让两个线程无同步读写同一个 bool 或直接访问对方所有的对象。

| 状态 | 唯一允许的下一步 | 资源与失败责任 |
| --- | --- | --- |
| Idle | 校验后分配 RequestId，进入 AwaitProducer | 源对象被明确持有；不因拿到 UObject 就保存一个永不过期的旧资源指针 |
| AwaitProducer | 在已核对的捕获生产者之后建立纹理复制依赖 | 要证明是这一代纹理、这一请求的结果；无法建立依赖则失败，不把下一 Tick 当完成 |
| CopySubmitted | 记录复制真正提交，并在允许的执行线程轮询该任务 IsReady | readback 与资源由任务继续持有；提交 lambda 返回不能销毁它们 |
| CopySubmitted 且未 ready | 稍后轮询；可报告 Pending | 不 Lock、不阻塞忙等、不重新 EnqueueCopy 覆盖同一任务 |
| Ready | Lock、检查跨度、拷入独立 CPU 数组、Unlock | 映射指针不逃逸；配对清理由同一执行侧负责 |
| CpuOwned | 向 GT 发送独立数组和 RequestId/Generation | 仅有效接收者、未取消、代号仍匹配才派发成功；业务回调最多一次 |
| Retiring | 在已确认所有 GPU 使用及 CPU 命令退场后释放任务资源 | 不把 CPU 已拿到数组推广成源纹理所有其他消费者均退场 |
| Idle 或 Stopped | 释放单槽，或关闭服务 | 只有安全退场才能复用这个槽 |

尚缺的实现正是 AwaitProducer → CopySubmitted：需要在固定引擎 revision、renderer 和 RHI 路径中，找到本次捕获实际写入的纹理及其生产位置，把该请求的复制接在生产者之后，并核对依赖、资源状态与持有期限。只有接通并验证这一步，后面的 ready 才能代表“请求 N 的副本可读”。本次没有对应源码证据，因此不提供虚构回调、RDG pass 或 `CaptureScene(); ENQUEUE_RENDER_COMMAND(...)` 捷径；下一 Tick 或固定延迟也不能补上这条依赖。保留这项实现缺口，不把状态表或行跨度算例计为异步读回实战完成。

行拷贝的纸面算法：Lock 返回行跨度以**像素**计，先核对 rowPitch≥W、bufferHeight≥H、乘法未溢出以及实际格式仍为 BGRA8。逐行把 `base + y*rowPitch*4` 开始的 `W*4` 字节拷到紧密 CPU 行 `out + y*W*4`，最后 Unlock。任何检查失败都沿已建立的锁状态清理并报告失败，不能返回半张成功图。字节通道顺序来自本例已声明格式，不从 FColor 类型名猜测所有 RT 的布局。[S14][S14]

PAPER_EXPECTED P06：W=3、H=2、rowPitch=4，每像素4字节，staging两行各16字节，输出应为24字节；分别拷偏移0与16处的12字节。一次 memcpy 24 字节会把首行填充和第二行部分内容当作图像。若 rowPitch=2 必须失败，不能继续越界读取。

### 6.3 取消、超时、销毁和重入不能省掉

- 提交前取消：标记取消并阻止后续生产/复制；若命令已排队，仍要等命令消费确认后释放其捕获数据。
- 提交后取消或接收者销毁：只取消结果投递。保留单槽直到对应 GPU/命令退场；取消布尔值不会撤回 GPU 工作。
- 超时：报告一次 Timeout，停止该请求的用户等待；资源进入 Retiring/隔离态，不能立刻 delete readback 或把槽交给新请求。若设备异常导致无法获得 ready，停止新请求并交由已核对的设备丢失/引擎关闭协议处理。本例不编造“超时后安全强制释放”。这使业务可结束，资源仍有明确的负责方和容量上限。
- Resize/重新初始化：增加 Generation 不能修复旧 GPU 访问。先停止生产并使旧任务安全退场，才替换资源；Generation 只用来拒收迟到结果。
- 回调重入：先把终态与“已派发”记录好，再在 GT 调用业务回调；回调若又发起请求，看到的是当前单槽状态。不要边遍历原任务容器边无保护地执行可重入用户代码。
- 服务关闭：先禁止新请求、取消接收者投递，再完成已提交工作的引擎允许的退场流程；等待范围、线程与设备丢失分支由实现者在目标版本验证。不得在任意析构里用全局 flush 冒充正确资源所有权。

PAPER_EXPECTED P07：请求7复制已提交，随后 Actor EndPlay，用户结果被取消；GPU 晚些 ready。管理者丢弃结果并安全退场，不访问已失效 Actor，回调次数为0。反例是取消后立即复用同一个 readback 给请求8：迟来的 ready/像素可能被错配为8。这个反例解释了为什么 RequestId、资源代号和真实退场各有职责。

## 7. 成本控制、平台边界与故障定位

只计颜色 payload 时，256² RGBA8=256 KiB，512² RGBA8=1 MiB，1024² RGBA16f=8 MiB；双缓冲乘2，Cube完整六面按面数计。完整 mip 链还增加存储；实际驻留和峰值分配不是这张乘法表能决定的。不存在仅凭分辨率即可保证的“最多常驻4–6张”，更不能用 CPU 虚拟内存文章证明 GPU 驻留机制。

降低频率减少平均捕获工作，但单次昂贵捕获仍可能形成尖峰；降低宽高各一半把名义像素数降到四分之一，不证明总 GPU 时间也变为四分之一。先按用途限制可见对象、距离、分辨率和更新条件，再在目标机测 CPU/GPU 时间线与峰值资源。[S06][S06]

| 现象 | 先做的判别 | 修复方向及停止条件 |
| --- | --- | --- |
| 小地图全黑 | 查 TextureTarget、组件注册/World、朝向、遮挡、DetailMode、曝光和路径 | 先用简单标记确认通路；BaseColor 不能穿透屋顶，也不是移动端万能补丁 |
| 重复绘制/更新过密 | 自动每帧、移动捕获、计时器是否同时打开 | 只保留明确的生产策略；Hidden/ShowOnly 列表要匹配 PrimitiveRenderMode |
| 监控屏出现自己/旧画面回响 | 被拍屏幕材质是否又采样当前 RT | 隐藏本屏或使用明确的历史缓冲/更新次序；这不是引擎必然无限递归生成新视图的证明 |
| 多相机画面被覆盖 | 是否多个写者共享同一 TextureTarget | 独立RT，或有严格先后与消费协议；不能只称为“光栅化竞争” |
| 画面偏暗/发灰 | CaptureSource、曝光、tone mapping、gamma、采样与UI路径 | 四角/灰阶基准逐段比较，不凭眼感叠加gamma修正 |
| 缩小时闪烁、模糊或像素化 | 源分辨率、UV、滤波、mip是否实际可用 | 大图缩小可研究mip；放大低分辨率纹理不能靠mip补细节 |
| 画完变空白 | 是否又清了RT、错误重建或写错目标 | `UpdateResourceImmediate` 有清除参数，不作每次绘制后的无条件“修复” [S20][S20] |
| 移动端黑屏/失败 | 路径、格式渲染与采样能力、资源尺寸、后期支持、Cook资产 | 在实际后端验收；不强制将所有浮点数据降到R8/RGBA8而破坏数值含义 |
| 读回空/旧图/错色 | 生产者顺序、请求代号、格式、跨度与完成信号 | 不用固定延迟掩盖依赖错误；失败返回明确状态 |

ShowFlags 是按视图控制特性的入口。关闭 MotionBlur、DOF、Bloom、动态阴影、GI、反射或 AO 要先判断用途与路径，逐项对照画面；不能许诺“关闭阴影省40%”“关一个GI标志等于所有Lumen代价消失”。关闭时序效果或长期不捕获还会影响历史状态，`bAlwaysPersistRenderingState` 也是有成本和版本条件的选择。[S06][S06]

截图导出与纹理资产转换也有职责边界：S19 明确将转换接口标为 EditorOnly，而 ExportRenderTarget 根据格式导出 HDR/PNG。保存路径权限、文件成功落盘、编码和色彩需单独确认，不能把“调用导出函数”当作可打开文件已经交付。

## 8. 纸面验收与后续实际验证责任

本文只完成表内“纸面判定”，没有执行右列验证。

| 场景 | PAPER_EXPECTED 的判定 | 进入项目后谁验证什么 |
| --- | --- | --- |
| P01 生命周期 | 入队/CPU返回不能授权GPU资源复用 | 渲染实现者：命令、引用、设备失效与销毁路径 |
| P02 颜色/数值 | 0–1存储无法无损容纳负数和>1 | 图形工程/技术美术：格式、曝光、UI与文件编码 |
| P03 非方形地图 | 512×256配4096宽时高覆盖2048 | UI/玩法：相机矩阵、方向、DPI、裁剪与图标对齐 |
| P04 使用层例 | 持有两个RT、一次计时器、解绑后无新回调 | 工程：UHT/编译、PIE结束、关卡切换、创建失败、反复开关 |
| P05 双缓冲 | 0.8→0.4→0.2且每次读前一结果 | 材质作者：初始清零、参数、alias拒绝、重入与显示绑定 |
| P06 行跨度 | 两行各取12字节并跳过padding | RHI实现者：目标平台格式、Lock线程、跨度及Unlock失败路径 |
| P07 取消 | 取消回调不立即复用在途槽 | 任务拥有者：销毁、超时、迟到结果、回调重入、关闭 |

实际验证还应记录平台/RHI、引擎版本、捕获源、格式/gamma、尺寸、材质/场景版本与输入。性能验证先做关闭捕获的基线，再只改变一个条件；频率、耗时和资源指标分开报告。为延迟读回增加排队不会让带宽变成零，需要测最大在途容量、数据新鲜度和丢弃策略。

## 9. 来源定位与关联阅读

2026-10-10 实际核对范围：S01/S02 的字段与格式定义；S03–S06 的捕获属性和触发说明；S07 的强/弱引用与线程提醒；S08–S10 的 Canvas、绘制签名与动态委托绑定；S11/S12 的材质绘制入口与历史限制；S13–S17 的读回输出、staging API、资源指针和栅栏声明；S18 的线程与资源寿命说明；S19 的创建、清除、绘制、同步读取、导出/EditorOnly列表；S20 的更新/清除参数；S21 的捕获枚举。

除 S12 的固定 4.27 外，引用 API 均以当日返回的动态 5.8 页面为准，不声称这些页面证明同一个 checkout 的实现。S12 只辅助解释读写分离与材质输出范围，不将其遗留限制扩展到所有现代渲染路径。S18 的一般线程所有权说明仍有参考价值，但页面含旧宏和旧栅栏表述，不作为现行接口签名的依据；S22只核对Cube的六面职责与公开目标属性。独立 K2_DrawLine、CreateRenderTarget2D、ReadRenderTargetRaw、ExportRenderTarget 页面有读取失败，改读所属类的对应条目；Render Targets 概念入口只返回空壳，不能作已读论据。Async readback 的具体生产者集成、线程与资源屏障仍未核对，因此第 6.2 节按设计合同标示。

- [01-渲染管线概览](01-渲染管线概览.md)：线程、RDG、GPU完成与资源退场
- [02-材质系统详解](../材质地形与世界表现/02-材质系统详解.md)：MID、参数与采样
- [05-后处理与画面特效](05-后处理与画面特效.md)：曝光、HDR/SDR与材质输入位置
- [01-UMG框架与控件系统](../../05-Gameplay与交互系统/界面设置与无障碍/01-UMG框架与控件系统.md)：UI显示入口与控件生命周期
- [10-渲染线程与RHI源码](10-渲染线程与RHI源码.md)：进一步研究的历史专题；该篇源码声明须按其自身范围复核，不作为本次读实现的证明
- [10-移动端渲染专项](10-移动端渲染专项.md)：平台约束的专题入口，实际能力仍需查目标路径
- [07-相机系统与视口](../../05-Gameplay与交互系统/输入移动与交互/07-相机系统与视口.md)：捕获视图、相机与坐标职责
- [11-多线程与任务系统](../../03-引擎架构与资源系统/运行架构与任务调度/11-多线程与任务系统.md)：异步消息与线程所有权
- [03-性能分析工具与Profiling](../../08-工程实践与质量/调试与性能分析/03-性能分析工具与Profiling.md)：测量捕获耗时与尖峰
- [02-虚拟内存、PageFault与mmap](../../01-编程与计算机基础/操作系统与系统I-O/02-虚拟内存、PageFault与mmap.md)：CPU虚拟内存背景；与GPU纹理驻留区分

[S01]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget2D
[S02]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ETextureRenderTargetFormat
[S03]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D
[S04]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D/CaptureScene
[S05]: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D/CaptureSceneDeferred
[S06]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent
[S07]: https://dev.epicgames.com/documentation/en-us/unreal-engine/object-pointers-in-unreal-engine
[S08]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCanvasRenderTarget2D
[S09]: https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-delegates-in-unreal-engine
[S10]: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCanvas?lang=en-US
[S11]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary/DrawMaterialToRenderTarget
[S12]: https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-and-render-targets-overview?application_version=4.27
[S13]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary/ReadRenderTarget
[S14]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/FRHIGPUTextureReadback
[S15]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/FRHIGPUMemoryReadback
[S16]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget/GameThread_GetRenderTargetResour-
[S17]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RenderCore/FRenderCommandFence
[S18]: https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine
[S19]: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UKismetRenderingLibrary?lang=en-US
[S20]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget2D/UpdateResourceImmediate
[S21]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ESceneCaptureSource
[S22]: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponentCube
