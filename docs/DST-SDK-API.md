# DST C API 与平台路由

面向 App / SDK 集成（对接 [ocserv-tunnel](https://github.com/hk59775634/ocserv)）。

## 稳定 C API（`OPENCONNECT_API_VERSION` 5.10）

```c
#include <openconnect.h>

/* 1) 读取域名（指针归库管理，重连前有效） */
const char **inc = NULL, **exc = NULL;
int n_inc = 0, n_exc = 0;
openconnect_get_dynamic_split_domains(vpninfo, &inc, &n_inc, &exc, &n_exc);

/* 2) 回调：CONNECT / prepare_script_env 后通知 App */
void on_dst(void *priv, const char **inc, int ni, const char **exc, int ne) {
    /* Android: VpnService.addRoute / iOS: NEPacketTunnelNetworkSettings */
}
openconnect_set_dst_domains_handler(vpninfo, on_dst);

/* 3) 桌面内置路由引擎（可选） */
openconnect_set_dst_routing(vpninfo, 1);
openconnect_set_dst_poll_interval(vpninfo, 5); /* seconds */
/* mainloop 内自动 openconnect_dst_poll()；也可手动： */
openconnect_dst_sync_routes(vpninfo);
/* 断开时： */
openconnect_dst_clear_routes(vpninfo);
```

| API | 用途 |
|-----|------|
| `openconnect_get_dynamic_split_domains` | 查询 include/exclude 域名数组 |
| `openconnect_set_dst_domains_handler` | 域名就绪回调（移动端首选） |
| `openconnect_set_dst_routing` | 启用库内主机路由同步 |
| `openconnect_set_dst_poll_interval` | DNS 轮询间隔 |
| `openconnect_dst_sync_routes` | 立即解析并更新路由 |
| `openconnect_dst_poll` | mainloop 定时入口 |
| `openconnect_dst_clear_routes` | 清理已装路由 |

域名仍来自服务端 `X-CSTP-Post-Auth-XML`；亦通过 `CISCO_DYNAMIC_SPLIT_*_DOMAINS` 导出给 vpnc-script。

## 平台行为

| 平台 | 库内路由 | 推荐 App 集成 |
|------|----------|----------------|
| **Linux** | `ip route replace …/32 dev $TUN`（include）；exclude 经原默认网关 | root/`CAP_NET_ADMIN`；或只用回调 |
| **macOS / BSD** | `route -n add/change -host` | 同上；生产可用 Network Extension |
| **Windows** | `route.exe` 尽力而为 | 推荐 Wintun + 回调自行装路由 |
| **Android** | 不直接改系统表 | `VpnService.Builder.addRoute` / 按解析 IP 更新 |
| **iOS** | 不直接改系统表 | `NEPacketTunnelNetworkSettings` / `includedRoutes` |

移动端请 **关闭** `openconnect_set_dst_routing`，仅用 handler + 平台 VPN API。

## 与脚本 helper 的关系

`scripts/openconnect-dst-helper` 仍可用于纯 CLI；嵌入式 SDK 应优先本 C API，避免再 fork 脚本。
