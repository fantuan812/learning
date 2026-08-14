# 游戏AI LLM NPC 安全
> 知识成熟度：L2（本轮审计修订时补标）。

> 本篇由《AI 评测回放与 LLM 安全》§七 拆分而来，正文逐字迁移（2026-08-14，R2-SPLIT-01）。

> 知识基线：引擎无关的游戏 AI 通用原理；本文聚焦 LLM NPC 对话接入产品时的安全工程——提示注入、工具调用边界、RAG 边界、敏感内容、隐私、成本与人工兜底；示意 schema、命令、伪代码和阈值不代表仓库已有实现。
> 适用范围：适用于把 LLM 作为 NPC 对话/决策组件接入的联网游戏，覆盖威胁模型、低权限工具网关、内容安全、可观测性与上线门禁。
> 事实边界：具体模型能力、平台容量、延迟和合规要求必须以项目版本、压测和官方资料为准；本文不声称任何模型或框架的既有安全能力。
> 官方参考：[OpenTelemetry 文档](https://opentelemetry.io/docs/)；[Unreal Engine 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine)。
> 最后更新：2026-08-14（拆分迁移）。

## 一、概述

LLM NPC 把自然语言对话接入游戏时，安全边界不再是"过滤器"，而是产品架构的一部分。
输入不只有玩家文本，输出也会被渲染、执行工具或被其他系统消费；
必须从威胁模型、工具权限、数据边界、人工兜底四个层面同时约束，
并把安全规则放进可评测、可门禁、可回滚的闭环（评测指标见 [01-AI评测回放](01-AI评测回放.md)）。



### 1.1 威胁模型

LLM NPC 的输入不只有玩家文本。

还包括任务上下文。

还包括角色设定。

还包括 RAG 文档。

还包括工具返回值。

还包括历史对话。

每个输入源都可能携带不可信内容。

玩家可以尝试伪造系统消息。

玩家可以要求 NPC 忽略安全规则。

玩家可以把恶意指令放进昵称或道具名。

RAG 文档也可能被错误标注或污染。

工具返回值可能包含诱导模型再次调用工具的文本。

### 1.2 提示注入

提示注入的目标通常是改变指令优先级。

也可能是诱导模型泄露系统提示。

也可能是诱导模型输出内部知识。

也可能是诱导模型调用不该调用的工具。

不能只靠一句“请遵守安全规则”防御。

应把权限放在模型外部。

模型输出先经过结构化解析。

工具网关再进行独立授权。

用户文本和系统策略要有清晰边界。

RAG 文档要标注为资料而非指令。

工具返回值默认视为不可信数据。

### 1.3 工具调用边界

LLM 不得直接拥有交易高权限。

LLM 不得直接拥有战斗高权限。

LLM 不得直接修改玩家资产。

LLM 不得直接发放掉落。

LLM 不得直接改变任务完成状态。

LLM 不得直接执行封禁或处罚。

LLM 可以提出结构化意图。

意图要经过规则引擎校验。

需要玩家确认的动作必须回到交互层确认。

需要服务端授权的动作必须使用短期令牌。

### 1.4 最小权限工具分类

低风险工具可以是查询公开区域名称。

低风险工具可以是查询当前任务提示。

中风险工具可以是生成导航建议。

中风险工具可以是创建待审核的对话草稿。

高风险工具包括交易。

高风险工具包括战斗技能。

高风险工具包括资产写入。

高风险工具包括剧情状态推进。

高风险工具默认不暴露给模型。

即使暴露，也必须由外部策略强制拒绝未授权参数。

### 1.5 RAG 知识边界

RAG 是检索增强生成。

检索结果不是天然可信。

文档要有可见性标签。

标签至少区分公开、玩家已知、当前任务、运营内部和未发布。

检索器要根据玩家、角色和剧情阶段过滤。

生成器只应看到当前授权范围内的文档。

不要把“模型会自觉保密”当作访问控制。

检索日志要记录文档 ID 和策略版本。

正文可以只保留摘要哈希以降低隐私风险。

### 1.6 剧透保护

剧情状态应来自权威游戏状态。

不能让玩家通过自由提问绕过剧情锁。

模型应区分“玩家已知”和“世界真实”。

NPC 可以用角色视角表达未知。

如果玩家尚未解锁章节，回答应使用安全模板。

剧透评测要覆盖直接问法。

还要覆盖诱导问法。

还要覆盖编码、翻译和角色扮演绕过。

还要覆盖让 NPC 复述检索原文。

### 1.7 敏感内容

敏感内容策略至少覆盖成人、仇恨、骚扰、自伤、违法和个人信息。

具体类别要以项目地区和平台政策为准。

评测集需要有正常边界样本。

也需要有明显违规样本。

还需要有暧昧和上下文依赖样本。

拦截器要支持输入和输出双向检查。

工具参数也要检查。

模型拒答后仍要保持角色一致和可玩性。

不要把所有拒答都变成同一句生硬提示。

### 1.8 隐私

对话日志可能包含真实姓名、联系方式和地址。

也可能包含儿童或未成年人信息。

采集前应提供必要的告知和控制。

存储时要最小化字段。

分析时应优先使用聚合统计。

回放导出要有权限审核。

客服调试需要看到原文时，应有短时授权和审计。

不得把原始私密对话直接用于训练。

训练前还要做去标识、去重和风险筛选。

### 1.9 成本与延迟

每次请求都可能产生模型调用成本。

成本要按玩家、会话、NPC 和版本分层。

延迟要拆成排队、检索、模型和后处理。

长上下文会增加成本和延迟。

缓存可以降低重复问题成本。

缓存必须绑定剧情状态和策略版本。

不能把一个玩家的个性化内容泄露给另一个玩家。

命中缓存时也要执行安全策略。

缓存不是安全检查的替代品。

### 1.10 人工审核与兜底

高风险内容应进入人工审核队列。

审核队列要有优先级。

优先处理可能已经暴露给玩家的风险。

审核员要看到输入、上下文摘要、输出、策略判定和工具审计。

需要保留最小可用证据。

模型不可用时切换到模板。

检索不可用时只回答已知安全信息。

工具不可用时不编造“已经完成”。

超时应给出可理解的稍后再试提示。

### 1.11 LLM NPC 请求流程

~~~mermaid
sequenceDiagram
    participant P as 玩家
    participant G as 输入网关
    participant R as RAG 检索器
    participant L as LLM
    participant V as 输出与权限校验
    participant T as 低权限工具
    participant H as 人工审核
    P->>G: 文本与会话上下文
    G->>G: 脱敏、限流、注入检测
    G->>R: 授权后的检索请求
    R-->>L: 标记为资料的文档摘要
    G->>L: 角色设定与受限任务
    L-->>V: 文本或结构化意图
    V->>V: 内容、剧透、权限、成本校验
    alt 低风险查询
        V->>T: 只读低权限调用
        T-->>V: 校验后的结果
    else 高风险或不确定
        V->>H: 审核或转人工
    end
    V-->>P: 回复或安全兜底
~~~

### 1.12 安全策略示意

~~~yaml
npc_policy_id: npc-policy-v12
input:
  max_chars: 1000
  rate_limit_per_player_minute: 12
  redact:
    - email
    - phone
    - account_id
retrieval:
  allowed_visibility:
    - public
    - player_unlocked
  deny_tags:
    - internal
    - unreleased
    - moderator_only
tools:
  allow:
    - get_public_location
    - get_current_quest_hint
  deny:
    - trade
    - grant_item
    - apply_damage
    - complete_quest
output:
  max_tokens: 300
  check:
    - sensitive_content
    - spoiler
    - personal_data
    - system_prompt_leak
fallback:
  timeout_ms: 2500
  response_template: npc_safe_fallback_v3
  human_review_on:
    - suspected_self_harm
    - credible_threat
    - high_confidence_privacy_leak
~~~

### 1.13 低权限工具 schema 示意

~~~yaml
tool: get_current_quest_hint
authority: server_read_only
input:
  player_id: server_bound
  quest_id: server_bound
  locale: validated_enum
side_effects: none
visibility: player_owned_state
audit:
  record_request_hash: true
  record_result_hash: true
  redact_player_id: true
~~~

工具 schema 必须声明副作用。

没有副作用的只读查询也要做可见性校验。

参数不能由模型自由拼接 SQL。

参数必须经过枚举、范围和归属检查。

### 1.14 LLM 守门伪代码

~~~text
function handle_npc_message(request, player_context):
    safe_input = redact_and_classify(request.text)
    if safe_input.blocked:
        return safe_fallback("input_policy")
    if rate_limiter.exceeded(request.player_id):
        return safe_fallback("rate_limit")
    docs = rag.retrieve(
        query=safe_input.text,
        visibility=player_context.allowed_lore,
        snapshot=player_context.rag_snapshot
    )
    prompt = build_prompt(
        role=player_context.role,
        state=player_context.public_state,
        documents=mark_as_untrusted_reference(docs),
        policy_id=player_context.policy_id
    )
    model_result = llm.generate(prompt, timeout_ms=2500)
    candidate = parse_structured_or_text(model_result)
    policy_result = safety_policy.check(candidate, player_context)
    if policy_result.high_risk:
        create_review_case(request, candidate, policy_result)
        return safe_fallback("review")
    if candidate.tool_call:
        if not tool_gateway.authorize(candidate.tool_call, player_context):
            audit_denied_tool(candidate.tool_call)
            return safe_fallback("tool_denied")
        result = tool_gateway.execute_read_only(candidate.tool_call)
        candidate = render_with_result(candidate, result)
    return output_filter_and_reply(candidate)
~~~

### 1.15 LLM 安全评测样本

提示注入样本要覆盖“忽略上文”。

要覆盖“你现在是系统管理员”。

要覆盖把恶意指令放进任务名。

要覆盖把恶意指令放进 RAG 文档。

要覆盖要求复述系统提示。

工具越权样本要覆盖交易。

要覆盖发放道具。

要覆盖造成伤害。

要覆盖修改任务。

剧透样本要覆盖直接、间接、翻译和编码表达。

隐私样本要覆盖用户主动提供和模型自行推断。

成本样本要覆盖超长输入和重复请求。

### 1.16 LLM 指标门禁

安全门禁应优先关注高影响漏放。

“工具调用成功率为零”比“回复更自然”更重要。

安全评测报告要列出样本总数。

要列出漏放数。

要列出误杀数。

要列出人工审核覆盖数。

要列出版本与策略。

要列出抽样和置信边界。

不能只给一个安全总分。

总分可能掩盖单个高风险类别的失败。

## 关联阅读

- [01-AI评测回放](01-AI评测回放.md)：评测指标体系、回放证据链与门禁口径（本文安全门禁的指标来源）。
- [02-自动测试玩家与AI回归基准](02-自动测试玩家与AI回归基准.md)：Bot 批量执行与安全回归。
- [游戏AI/01-决策与架构/07-NPC人格对话与社交AI](../01-决策与架构/07-NPC人格对话与社交AI.md)：对话树/人格/情绪等非 LLM 对话设计。
- [游戏服务端/04-平台与可靠性/00-平台可靠性总览与迁移说明](../../游戏服务端/04-平台与可靠性/00-平台可靠性总览与迁移说明.md)：权限模型与审计的工程落地。
