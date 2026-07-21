# openconnect-tunnel

基于上游 [OpenConnect](https://gitlab.com/openconnect/openconnect) **v9.12** 的客户端二次开发分支，对齐服务端 [ocserv-tunnel](https://github.com/hk59775634/ocserv) 新增能力：

| 能力 | 说明 |
|------|------|
| **SPEC-01 Route B** | 连接 `https://host/{tunnel_group}` 时，`<group-access>` 携带完整 URL（含路径），供服务端 RADIUS TunnelGroupName |
| **SPEC-02 DST** | 解析 `X-CSTP-Post-Auth-XML` 中的 `dynamic-split-*-domains`，导出环境变量并由可选 helper 动态改路由 |

## 构建

```bash
./autogen.sh
./configure --with-gnutls
make -j"$(nproc)"
sudo make install
```

安装 DST helper（可选）：

```bash
sudo install -m 0755 scripts/openconnect-dst-helper /usr/local/libexec/
sudo install -m 0755 scripts/vpnc-script-tunnel /usr/local/etc/
```

## 使用（对接 ocserv-tunnel）

```bash
# Route B：URL 路径即 tunnel group
sudo openconnect \
  --script=/usr/local/etc/vpnc-script-tunnel \
  --user=shortname \
  https://vpn.example.com/demoagent1-sg
```

DST 域名由服务端 Post-Auth XML 下发。使用 `vpnc-script-tunnel` 时会启动 `openconnect-dst-helper`：周期性解析域名并：

- **include**：经 `TUNDEV` 添加 `/32` 主机路由  
- **exclude**：经原默认网关添加 `/32` 主机路由（tunnel-all 场景）

环境变量：

| 变量 | 含义 |
|------|------|
| `CISCO_DYNAMIC_SPLIT_INCLUDE_DOMAINS` | 逗号分隔 include 域名 |
| `CISCO_DYNAMIC_SPLIT_EXCLUDE_DOMAINS` | 逗号分隔 exclude 域名 |

## 与上游差异

- `dst.c`：解析 Post-Auth XML DST 标签  
- `cstp.c` / `script.c`：接入解析并导出环境变量  
- `scripts/openconnect-dst-helper`、`vpnc-script-tunnel`：Linux 动态路由辅助  

上游 OpenConnect 本身已在 init 阶段发送带路径的 `<group-access>`（`internal_get_url`）。

## 文档

- [`docs/OCSERV-TUNNEL-COMPAT.md`](docs/OCSERV-TUNNEL-COMPAT.md)  
- [`docs/DST-SDK-API.md`](docs/DST-SDK-API.md) — 稳定 C API 与各平台路由  
- 服务端：[hk59775634/ocserv](https://github.com/hk59775634/ocserv)

## 许可

与上游相同，见 [`COPYING.LGPL`](COPYING.LGPL)。
