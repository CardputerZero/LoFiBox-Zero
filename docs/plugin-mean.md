我建议 **plugin 不要做成“可以改一切”的扩展系统**，而要做成 LoFiBox 的“四类扩展点”：

1. UI Skin / Theme：替换界面风格
2. DSP Effect：自定义音效
3. Remote Source Provider：更多远程音乐仓库
4. Metadata Provider：自定义 ID3 / Artwork / Lyric / 指纹匹配来源

这样正好贴合 LoFiBox 现有边界。README 里已经明确产品边界是：

```text
Source -> Decode/Playback Services -> DSP/Visualization -> App State -> Canvas -> Linux Presenter
```

也强调 app 层不应该知道自己运行在容器、Cardputer Zero 还是其他 Linux 设备上，这说明 plugin 也不能越过这个边界去碰 presenter / framebuffer / X11 细节。

---

## 一、先明确 plugin 的定位

现在仓库里已经有 `src/plugins/plugin_manifest.cpp` 和 `plugin_manifest.h`，但确实还只是注册 manifest：`PluginRegistry::registerPlugin()` 只做去重并保存 manifest，`findById()` 只是查找 ID。

当前 manifest 也很轻：只有 `id`、`name`、`kind`、`capabilities`、`runtime_dependencies`。

我建议不要急着上动态 `.so` 插件。第一阶段应该做成：

```text
manifest + 资源包 + 外部 helper 进程 + 稳定接口
```

也就是说：

```text
UI 插件        = theme/layout/assets 配置包
DSP 插件       = 内置 C++ effect 注册 + 可选外部 LADSPA/LV2/pipe helper
远程仓库插件   = Python/helper provider，通过 JSON-RPC 或 stdio 协议接入
Metadata 插件  = Python/helper provider，通过统一 metadata request/response 接入
```

不要一开始允许插件直接链接进主程序、直接拿 AppState 指针、直接画 Canvas、直接改播放队列。那样短期很快，长期会把分层打穿。

---

# 二、插件能力应该分成 4 个 capability family

建议 manifest 里的 `capabilities` 不要只是字符串随便写，而是固定成命名空间。

例如：

```json
{
  "id": "io.github.vicliu624.lofibox.theme.classic-dark",
  "name": "Classic Dark",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "asset_pack",
  "capabilities": [
    "ui.theme",
    "ui.icons",
    "ui.layout.now_playing",
    "ui.layout.library"
  ]
}
```

核心分类：

```text
ui.theme
ui.icons
ui.layout.now_playing
ui.layout.library
ui.layout.eq
ui.layout.lyrics

audio.effect
audio.effect.realtime
audio.effect.offline
audio.effect.visualizer

remote.source
remote.browse
remote.search
remote.stream
remote.auth.profile

metadata.reader
metadata.enricher
metadata.lyrics
metadata.artwork
metadata.fingerprint
metadata.tag_writer
```

这样做的好处是：plugin manager 不需要理解所有插件细节，只需要根据 capability 把插件挂到对应扩展点。

---

# 三、UI 插件：我建议叫 Skin，而不是完整 UI Plugin

这里最危险：如果允许插件直接写页面代码，很快就会破坏 LoFiBox 的交互一致性、Now Playing 联动、WebUI/GUI 状态同步。

所以我建议 UI plugin 第一阶段不要叫 `UI Plugin`，而叫：

```text
Skin Plugin / Theme Pack / Layout Pack
```

它只能替换：

```text
颜色
字体
图标
背景
边框
页面布局模板
组件间距
Now Playing 卡片样式
EQ 滑块样式
歌词页样式
列表项样式
```

但不能替换：

```text
播放状态模型
页面路由逻辑
输入行为
播放命令
远程源语义
队列语义
```

你的项目已经有 `src/ui/pages/now_playing_page.cpp`、`equalizer_page.cpp`、`lyrics_page.cpp`、`list_page.cpp` 等页面，也有 `app_projection_builder`、`app_page_model`、`app_renderer` 这类分层文件在 CMake 中。
所以 UI plugin 应该挂在 **Projection -> Render Template** 之间，而不是挂在 AppState 或 Presenter 上。

推荐结构：

```text
~/.local/share/lofibox/plugins/
  classic-dark/
    plugin.json
    skin.json
    icons/
      play.png
      pause.png
      source.png
    fonts/
      tiny.ttf
```

`skin.json` 示例：

```json
{
  "screen": {
    "target_width": 320,
    "target_height": 170
  },
  "palette": {
    "background": "#08090D",
    "panel": "#11131A",
    "primary": "#F3C56B",
    "secondary": "#9B8F73",
    "accent": "#DFA24A",
    "danger": "#D66A4A"
  },
  "pages": {
    "now_playing": {
      "layout": "compact-cover-left",
      "show_album_art": true,
      "show_spectrum": true,
      "show_source_badge": true,
      "title_max_lines": 1,
      "artist_max_lines": 1
    },
    "eq": {
      "layout": "ten-band-thin-slider",
      "band_label_mode": "minimal"
    },
    "lyrics": {
      "layout": "center-active-line",
      "inactive_lines": 2
    }
  }
}
```

UI 插件的核心原则：

```text
插件提供“怎么画”
核心决定“画什么”
```

也就是说，Now Playing 的数据仍然来自统一 runtime snapshot。WebUI 操作后，GUI Now Playing 联动的问题不能由 UI 插件解决，而应该由统一 runtime event / snapshot 解决。UI 插件只是消费这个 projection。

---

# 四、自定义音效：分两级做，不要一开始就开放任意 DSP

LoFiBox 现在已经有 DSP 相关结构，比如 `dsp_chain.cpp`、`realtime_dsp_engine.cpp`、`eq_runtime.cpp`，测试里也已经有 equalizer、DSP chain、realtime DSP engine、DSP profile manager 等。

所以音效插件应该接在：

```text
Decode PCM -> DSP Chain -> Audio Output
```

而不是接在播放器控制层。

我建议分成两种：

## 1. Built-in DSP Plugin

这是最稳的第一阶段。

插件不是外部动态库，而是编译进 LoFiBox 的 effect provider：

```cpp
class AudioEffectProvider {
public:
    virtual std::string id() const = 0;
    virtual AudioEffectDescriptor descriptor() const = 0;
    virtual std::unique_ptr<AudioEffectInstance> create(const AudioEffectConfig&) = 0;
};
```

适合：

```text
LoFi
Night
Radio(像从电台里传来)
Loop / Sampler
Tape(变旧、变暖、像磁带)
Vinyl(模拟黑胶)
Soft Clip
Stereo Width
Compressor
Limiter
```

这些是你自己可控的，性能也可测。

## 2. External DSP Plugin

第二阶段再考虑外部 DSP。

但是我不建议一开始就支持 VST。VST 生态重，授权和 UI 模型也复杂，不适合 LoFiBox 这种小设备播放器。

更合理的是：

```text
LADSPA：最简单，适合 Linux 音频效果
LV2：比 LADSPA 强，但复杂一些
SoX effect bridge：适合非实时 / 低复杂度效果
FFmpeg filter bridge：适合部分滤镜
外部 helper PCM pipe：最高自由度，但延迟和稳定性要管
```

第一阶段可以定义一个 `audio.effect` 插件 manifest，但只允许参数化，不允许外部代码进程内运行。

例如：

```json
{
  "id": "io.github.vicliu624.lofibox.effect.lofi",
  "name": "LoFi",
  "kind": "internal_provider",
  "capabilities": ["audio.effect", "audio.effect.realtime"],
  "effect": {
    "type": "dsp_chain",
    "nodes": [
      { "type": "lowpass", "cutoff_hz": 7200 },
      { "type": "bitcrush", "bits": 12, "mix": 0.35 },
      { "type": "wow_flutter", "depth": 0.12 },
      { "type": "soft_clip", "drive": 1.4 }
    ]
  }
}
```

关键限制：

```text
音效插件不能控制播放队列
不能访问远程账号
不能写 metadata
不能阻塞 audio callback
不能在实时线程里做内存分配 / IO / 网络请求
```

这条边界非常重要。

---

# 五、远程仓库插件：这是最适合先落地的 plugin 类型

你的项目已经有 remote 结构，例如：

```text
src/remote/common/remote_provider_contract.cpp
src/remote/common/remote_source_registry.cpp
src/remote/common/stream_source_model.cpp
src/platform/host/remote_media_runtime.cpp
src/platform/host/runtime_remote_media_tool.cpp
scripts/remote_media_tool.py
```

这些已经在 CMake 里出现。

这说明远程源非常适合做成外部 provider 插件。

不要让远程插件直接链接主程序。建议用：

```text
host runtime -> remote provider adapter -> plugin helper process -> JSON response
```

也就是每个远程仓库插件是一个独立 helper：

```text
plugins/
  navidrome-provider/
    plugin.json
    provider.py
  webdav-provider/
    plugin.json
    provider.py
  jellyfin-provider/
    plugin.json
    provider.py
```

统一协议：

```json
{
  "method": "browse",
  "params": {
    "profile_id": "home-navidrome",
    "path": "/albums"
  }
}
```

返回：

```json
{
  "items": [
    {
      "type": "album",
      "id": "album:123",
      "title": "Kind of Blue",
      "subtitle": "Miles Davis",
      "artwork_url": "plugin://navidrome/artwork/album/123"
    }
  ]
}
```

播放时返回统一 stream model：

```json
{
  "stream": {
    "url": "https://example.com/rest/stream.view?id=123",
    "content_type": "audio/flac",
    "headers": {
      "Authorization": "Bearer ***"
    },
    "cache_policy": "streaming"
  }
}
```

远程源插件需要重点处理：

```text
browse
search
resolve_stream
resolve_artwork
test_connection
profile_schema
```

其中 `profile_schema` 很重要。它让 UI/WebUI 都能知道这个插件需要哪些配置字段，但不需要知道具体插件逻辑。

例如：

```json
{
  "method": "profile_schema",
  "result": {
    "fields": [
      { "name": "base_url", "type": "url", "required": true },
      { "name": "username", "type": "string", "required": true },
      { "name": "password", "type": "secret", "required": true }
    ]
  }
}
```

这样以后增加 Jellyfin、Navidrome、Emby、OpenSubsonic、WebDAV、SFTP，都不会污染核心代码。

---

# 六、自定义音频文件信息获取：应该做成 Metadata Pipeline 插件

你列的 “id3/lyric 获取方式” 我建议拆成 5 类：

```text
embedded_tag_reader     本地文件内嵌标签读取
metadata_enricher       MusicBrainz / AcoustID / 自定义服务
lyrics_provider         LRC / synced lyrics / plain lyrics
artwork_provider        cover art / fanart / folder image
identity_provider       指纹 / 文件 hash / track identity
```

你的项目里已经有这些倾向：`metadata_provider.cpp`、`musicbrainz_client.cpp`、`acoustid_client.cpp`、`lyrics_provider.cpp`、`lyrics_pipeline_components.cpp`、`cover_art_archive_client.cpp`、`track_identity_provider.cpp` 等。

所以 Metadata plugin 不是新体系，而是把已有 host provider 抽象化。

推荐 pipeline：

```text
Local File
  -> embedded tag reader
  -> filename/path heuristic
  -> fingerprint identity
  -> metadata enrichers
  -> artwork providers
  -> lyrics providers
  -> merge policy
  -> governed metadata result
```

注意，metadata 插件必须经过 `metadata_merge_policy` / `metadata_governance`。仓库里已经有相关文件：`metadata_governance.cpp`、`metadata_merge_policy.cpp`、`match_confidence_guard.cpp`、`enrichment_authority_policy.cpp`。

这非常关键：
插件可以提供候选结果，但不能直接覆盖曲库事实。

例如：

```json
{
  "track_id": "local:/Music/a.flac",
  "candidates": [
    {
      "title": "Blue in Green",
      "artist": "Miles Davis",
      "album": "Kind of Blue",
      "confidence": 0.93,
      "source": "musicbrainz"
    }
  ]
}
```

核心再根据规则决定是否采用：

```text
本地 embedded tag 优先？
高置信度在线结果是否覆盖？
歌词是否允许多来源？
封面是否缓存？
用户手动修改是否永远最高优先级？
```

这符合你的“不要功能残缺”的要求：插件扩展来源，但最终一致性仍由核心治理。

---

# 七、Plugin Runtime 不建议直接用 C++ ABI，至少第一阶段不要

C++ 动态插件听起来很正统，但对 LoFiBox 不一定合适：

```text
ABI 不稳定
编译器版本敏感
Debian 打包复杂
崩溃会拖死主进程
权限/路径/依赖治理麻烦
以后官方仓库审核也更难解释
```

更适合你的路线是三层：

## Level 0：Manifest / Asset Plugin

用于 UI skin、图标、布局、DSP preset。

```text
安全
容易打包
不会崩主程序
适合用户分享
```

## Level 1：External Helper Plugin

用于远程源、歌词、metadata。

```text
Python / shell / 任意语言都可以
通过 stdio JSON-RPC 通信
崩了也只是插件失败
主程序可超时、重启、禁用
```

## Level 2：Native Internal Provider

用于高性能 DSP、核心内置 provider。

```text
只给官方/内置模块使用
通过 C++ 接口注册
不作为普通用户插件机制
```

暂时不要 Level 3：

```text
任意 .so 插进主进程
任意脚本访问 AppState
任意插件控制 UI 路由
```

---

# 八、建议的目录结构

系统级：

```text
/usr/share/lofibox/plugins/
  builtin-classic-dark/
  builtin-navidrome/
  builtin-jellyfin/
```

用户级：

```text
~/.local/share/lofibox/plugins/
  my-theme/
  my-lyrics-provider/
  my-webdav-provider/
```

缓存：

```text
~/.cache/lofibox/plugins/
  plugin-id/
    artwork/
    metadata/
    lyrics/
```

配置：

```text
~/.config/lofibox/plugins.json
```

插件状态：

```json
{
  "enabled": [
    "io.github.vicliu624.lofibox.theme.classic-dark",
    "community.lyrics.my-provider"
  ],
  "disabled": [
    "community.remote.experimental-sftp"
  ],
  "selected_skin": "io.github.vicliu624.lofibox.theme.classic-dark",
  "metadata_order": [
    "embedded",
    "musicbrainz",
    "acoustid",
    "community.lyrics.my-provider"
  ]
}
```

---

# 九、Plugin Manifest 建议升级成这样

当前 manifest 太轻，可以升级为：

```json
{
  "schema_version": 1,
  "id": "community.remote.navidrome",
  "name": "Navidrome Provider",
  "version": "1.0.0",
  "api_version": "1",
  "kind": "external_helper",
  "entry": {
    "command": "python3",
    "args": ["provider.py"],
    "protocol": "lofibox-jsonrpc-1"
  },
  "capabilities": [
    "remote.source",
    "remote.browse",
    "remote.search",
    "remote.stream",
    "remote.auth.profile"
  ],
  "runtime_dependencies": [
    "python3",
    "python3-requests"
  ],
  "permissions": [
    "network.client",
    "credential.read.profile"
  ],
  "resources": {
    "icons": "icons/",
    "schema": "profile.schema.json"
  }
}
```

虽然你之前 WebUI 不想要任何权限相关功能，但 plugin manifest 内部仍然应该有 permission 描述。
这不是用户角色和权限系统，而是 **运行时能力声明**，用于保护核心和打包审查。

UI 上可以不暴露复杂权限，只显示：

```text
这个插件需要网络访问
这个插件需要读取对应远程源的凭据
这个插件会写入缓存
```

---

# 十、Plugin Manager 应该承担什么责任

建议新增：

```text
src/plugins/plugin_manifest.h
src/plugins/plugin_registry.h
src/plugins/plugin_discovery.h
src/plugins/plugin_loader.h
src/plugins/plugin_runtime.h
src/plugins/plugin_capability_index.h
```

职责：

```text
PluginDiscovery
  扫描系统目录和用户目录

PluginManifestParser
  读取 plugin.json
  校验 schema_version / api_version / id / capabilities

PluginRegistry
  保存所有插件 manifest

PluginCapabilityIndex
  按 capability 查询插件

PluginRuntime
  管理 external helper 生命周期
  超时
  stderr 日志
  崩溃隔离
  request/response 编码

PluginSettingsStore
  保存启用/禁用、优先级、选中 skin
```

不要让 PluginManager 知道 remote、metadata、DSP、UI 的具体业务。
它只管理“插件作为扩展包”的生命周期。

具体业务适配器应该是：

```text
RemotePluginAdapter
MetadataPluginAdapter
SkinPluginAdapter
DspPluginAdapter
```

---

# 十一、GUI / WebUI / CLI 的联动方式

插件系统不要直接解决 GUI/WebUI 联动。
联动应该统一走 runtime：

```text
WebUI command
  -> RuntimeCommandBus
  -> PlaybackRuntime / SettingsRuntime / QueueRuntime
  -> RuntimeEvent
  -> RuntimeSnapshot
  -> GUI projection
  -> Now Playing redraw
```

你仓库里已经有 `runtime_command_bus.cpp`、`runtime_event.cpp`、`runtime_snapshot_assembler.cpp`、`runtime_session_facade.cpp` 等运行时结构。

所以 plugin 对 GUI/WebUI 的影响应该是：

```text
换 skin -> settings runtime event -> GUI 和 WebUI 同步使用新 skin
切 DSP preset -> DSP runtime event -> Now Playing 显示当前音效
换远程源 -> source profile event -> GUI/WebUI source list 同步
metadata 更新 -> library/runtime event -> Now Playing 重新显示标题/歌词/封面
```

这样 plugin 不需要知道 GUI/WebUI 存在。

---

# 十二、四类插件的优先级建议

我建议实现顺序如下：

## P0：Plugin Manifest + Discovery + Registry

目标：

```text
能扫描插件目录
能读取 plugin.json
能按 capability 查询
能启用/禁用
能在 diagnostics 页面显示
```

这一步不实现任何真实插件能力，但把骨架打稳。

## P1：UI Skin Plugin

原因：收益最大、风险最低。

先支持：

```text
颜色
图标
字体
Now Playing 布局模板
EQ 布局模板
Lyrics 布局模板
```

不要支持任意页面逻辑。

## P2：Remote Source Plugin

原因：你的播放器网络媒体库是核心卖点，而且远程源天然适合插件化。

先支持：

```text
profile_schema
test_connection
browse
search
resolve_stream
```

## P3：Metadata / Lyrics Plugin

原因：这类来源非常多，适合社区扩展。

先支持：

```text
lyrics lookup
artwork lookup
metadata enrichment
```

写 tag 要晚一点，因为它涉及破坏性写入。

## P4：DSP Plugin

原因：实时音频稳定性最难，应该最后开放。

先做：

```text
DSP preset plugin
```

后做：

```text
external effect plugin
```

不要一开始支持任意 native DSP。

---

# 十三、最终我建议的架构图

```text
                         ┌─────────────────────────┐
                         │       plugin.json        │
                         └───────────┬─────────────┘
                                     │
                         ┌───────────▼─────────────┐
                         │   Plugin Discovery       │
                         └───────────┬─────────────┘
                                     │
                         ┌───────────▼─────────────┐
                         │   Plugin Registry        │
                         └───────────┬─────────────┘
                                     │
        ┌────────────────────────────┼────────────────────────────┐
        │                            │                            │
┌───────▼────────┐          ┌────────▼─────────┐        ┌─────────▼────────┐
│ Skin Adapter   │          │ Remote Adapter   │        │ Metadata Adapter │
└───────┬────────┘          └────────┬─────────┘        └─────────┬────────┘
        │                            │                            │
┌───────▼────────┐          ┌────────▼─────────┐        ┌─────────▼────────┐
│ App Projection │          │ Remote Registry  │        │ Metadata Pipeline│
└───────┬────────┘          └────────┬─────────┘        └─────────┬────────┘
        │                            │                            │
┌───────▼────────┐          ┌────────▼─────────┐        ┌─────────▼────────┐
│ Canvas Renderer│          │ Playback Source  │        │ Merge Governance │
└────────────────┘          └──────────────────┘        └──────────────────┘


┌────────────────┐
│ DSP Adapter    │
└───────┬────────┘
        │
┌───────▼────────┐
│ DSP Chain      │
└───────┬────────┘
        │
┌───────▼────────┐
│ Audio Output   │
└────────────────┘
```
