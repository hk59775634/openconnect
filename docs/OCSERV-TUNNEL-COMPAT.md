# 与 ocserv-tunnel 兼容性

目标服务端：https://github.com/hk59775634/ocserv （基于 ocserv 1.5.0）

## SPEC-01 Route B / TunnelGroupName

| 步骤 | 客户端行为 |
|------|------------|
| 用户连接 | `openconnect https://pop.example.com/{tunnel_group}` |
| Init XML | `<group-access>https://pop.example.com/{tunnel_group}</group-access>` |
| 服务端 | 解析路径 / `<group-access>`，RADIUS 发送 TunnelGroupName=146 |

OpenConnect 上游 `internal_get_url()` 已把 `urlpath` 拼进 group-access；本 fork 保持该行为并在文档中明确 Route B 用法。

## SPEC-02 Dynamic Split Tunneling

| 步骤 | 客户端行为 |
|------|------------|
| CONNECT | 读取 `X-CSTP-Post-Auth-XML` |
| 解析 | `dynamic-split-include-domains` / `dynamic-split-exclude-domains`（CDATA） |
| 导出 | `CISCO_DYNAMIC_SPLIT_INCLUDE_DOMAINS` / `CISCO_DYNAMIC_SPLIT_EXCLUDE_DOMAINS` |
| 路由 | `openconnect-dst-helper` 轮询 DNS 并安装主机路由 |

**注意：** 原生 AnyConnect 通过 DNS 钩子即时改表；本 helper 采用解析轮询（默认 5s），语义对齐但非驱动级钩子。主验证目标仍是 Win/macOS AnyConnect；本客户端提供 Linux 可用实现。

## 联调检查清单

1. Route B：短用户名 + URL 组路径 → RADIUS Access-Request 含 TunnelGroupName  
2. DST include：Post-Auth XML 含域名 → 环境变量非空 → helper 为解析 IP 添加 `dev $TUNDEV` 路由  
3. DST exclude：tunnel-all 下 exclude 域名走原默认网关  
4. SPEC-01 + DST 同时启用互不干扰  
