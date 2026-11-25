// ============================================================================
// YXG Minecraft Launcher — 现代化启动器界面
// 运行环境：light 宿主 QuickJS + 原生 Win32 控件
// 依赖：YXG_MinecraftLauncher.dll（提供 mc.* 命名空间 API）
// ============================================================================

var THEME = {
  bg: "#12151c", card: "#1b2230", card2: "#202839",
  accent: "#5b8cff", accent2: "#7ca2ff",
  fg: "#e7ecf5", muted: "#8a93a6", line: "#2a3448",
  ok: "#3ddc84", warn: "#ffb648", err: "#ff5d5d"
};

var hasMc = (typeof mc !== "undefined");
var W = 980, H = 640;

function log(msg) {
  if (!msg) msg = "";
  var old = win32.getWindowText("logOut");
  var line = "[" + timeNow() + "] " + msg;
  win32.setWindowText("logOut", old ? (old + "\r\n" + line) : line);
}

function timeNow() {
  var d = new Date();
  function p(n) { return (n < 10 ? "0" : "") + n; }
  return p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds());
}

function parse(str) {
  try { return JSON.parse(str); } catch (e) { return null; }
}

function createUi() {
  var C = win32.createControl;

  // 标题
  C({ type: "label", text: "⛏  我的世界启动器", id: "title", x: 24, y: 16, w: 420, h: 34 });
  win32.setControlColor("title", { fg: THEME.fg });
  C({ type: "label", text: "HMCL 兼容 · 由 C++ 核心驱动", id: "subtitle", x: 24, y: 48, w: 420, h: 18 });
  win32.setControlColor("subtitle", { fg: THEME.muted });

  // 左侧卡片
  C({ type: "panel", text: "", id: "leftCard", x: 24, y: 84, w: 320, h: 520 });
  win32.setControlColor("leftCard", { bg: THEME.card, border: THEME.line });

  C({ type: "label", text: "已安装版本", id: "lblInstalled", x: 42, y: 104, w: 200, h: 20 });
  win32.setControlColor("lblInstalled", { fg: THEME.muted });
  C({ type: "combobox", text: "", id: "installedCombo", x: 42, y: 128, w: 284, h: 30 });
  win32.setControlColor("installedCombo", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });

  C({ type: "button", text: "⟳ 刷新版本列表", id: "btnRefresh", x: 42, y: 168, w: 284, h: 34 });
  win32.setControlColor("btnRefresh", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });

  C({ type: "label", text: "安装新版本（输入版本号，如 1.20.1）", id: "lblInstall", x: 42, y: 224, w: 300, h: 20 });
  win32.setControlColor("lblInstall", { fg: THEME.muted });
  C({ type: "edit", text: "", id: "verEdit", x: 42, y: 248, w: 284, h: 30 });
  win32.setControlColor("verEdit", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });
  C({ type: "button", text: "⬇ 下载安装", id: "btnInstall", x: 42, y: 286, w: 284, h: 34 });
  win32.setControlColor("btnInstall", { bg: THEME.accent, fg: "#ffffff", border: THEME.accent });

  C({ type: "label", text: "下载源", id: "lblSrc", x: 42, y: 340, w: 200, h: 20 });
  win32.setControlColor("lblSrc", { fg: THEME.muted });
  C({ type: "combobox", text: "", id: "srcCombo", x: 42, y: 364, w: 284, h: 30 });
  win32.setControlColor("srcCombo", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });
  win32.comboAddItem("srcCombo", "BMCLAPI（推荐）");
  win32.comboAddItem("srcCombo", "MCBBS 镜像");
  win32.comboAddItem("srcCombo", "Mojang 官方");
  win32.comboSetSel("srcCombo", 0);

  // 账号区
  C({ type: "label", text: "账号", id: "lblAcc", x: 42, y: 410, w: 200, h: 20 });
  win32.setControlColor("lblAcc", { fg: THEME.muted });
  C({ type: "edit", text: "Steve", id: "nameEdit", x: 42, y: 434, w: 284, h: 30 });
  win32.setControlColor("nameEdit", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });
  C({ type: "button", text: "离线登录", id: "btnOffline", x: 42, y: 472, w: 138, h: 34 });
  win32.setControlColor("btnOffline", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });
  C({ type: "button", text: "微软正版登录", id: "btnMs", x: 188, y: 472, w: 138, h: 34 });
  win32.setControlColor("btnMs", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });

  // 右侧主区
  C({ type: "panel", text: "", id: "rightCard", x: 364, y: 84, w: 592, h: 520 });
  win32.setControlColor("rightCard", { bg: THEME.card, border: THEME.line });

  C({ type: "label", text: "启动游戏", id: "lblLaunch", x: 390, y: 112, w: 300, h: 20 });
  win32.setControlColor("lblLaunch", { fg: THEME.muted });
  C({ type: "button", text: "▶  启动游戏", id: "btnLaunch", x: 390, y: 140, w: 260, h: 52 });
  win32.setControlColor("btnLaunch", { bg: THEME.ok, fg: "#0c1208", border: THEME.ok });
  C({ type: "button", text: "■ 停止游戏", id: "btnStop", x: 664, y: 140, w: 120, h: 52 });
  win32.setControlColor("btnStop", { bg: THEME.err, fg: "#ffffff", border: THEME.err });

  C({ type: "label", text: "状态：就绪", id: "progLabel", x: 390, y: 208, w: 540, h: 24 });
  win32.setControlColor("progLabel", { fg: THEME.fg });
  C({ type: "button", text: "⟳ 刷新进度", id: "btnProgress", x: 390, y: 240, w: 140, h: 32 });
  win32.setControlColor("btnProgress", { bg: THEME.card2, fg: THEME.fg, border: THEME.line });

  C({ type: "label", text: "运行日志", id: "lblLog", x: 390, y: 290, w: 200, h: 20 });
  win32.setControlColor("lblLog", { fg: THEME.muted });
  C({ type: "output", text: "", id: "logOut", x: 390, y: 314, w: 540, h: 260 });
  win32.setControlColor("logOut", { bg: "#0e121a", fg: THEME.fg });

  if (!hasMc) {
    log("⚠ mc.* API 不可用：请将 YXG_MinecraftLauncher.dll 放入 Cache\\gui\\dll 并重启宿主");
  }
}

function refreshInstalled() {
  if (!hasMc) return;
  try {
    win32.comboClear("installedCombo");
    var list = parse(mc.listInstalled());
    if (list && list.length) {
      for (var i = 0; i < list.length; i++) {
        win32.comboAddItem("installedCombo", list[i].id + "  [" + list[i].type + "]");
      }
      win32.comboSetSel("installedCombo", 0);
      log("已加载 " + list.length + " 个本地版本");
    } else {
      log("未发现本地版本，可在左侧输入版本号下载安装");
    }
  } catch (e) { log("刷新失败: " + e); }
}

function selectedInstalledId() {
  var t = win32.comboGetText("installedCombo");
  var m = /^(\S+)/.exec(t);
  return m ? m[1] : "";
}

function applySource() {
  var sel = win32.comboGetSel("srcCombo");
  if (sel === 0) mc.setSource("bmclapi");
  else if (sel === 1) mc.setSource("mcbbs");
  else mc.setSource("mojang");
}

function onControlEvent(controlId, code) {
  if (!hasMc) return;
  try {
    if (controlId === "btnRefresh") {
      log("正在拉取版本清单…");
      var r = parse(mc.refreshRemote());
      if (r && r.ok) log("版本清单已更新，共 " + r.count + " 个版本（最新正式版 " + r.latestRelease + "）");
      else log("拉取失败: " + (r && r.error ? r.error : "未知错误"));
      refreshInstalled();
    }
    else if (controlId === "btnInstall") {
      applySource();
      var id = win32.getWindowText("verEdit").trim();
      if (!id) { log("请先输入要安装的版本号"); return; }
      var r = parse(mc.installVersion(id));
      if (r && r.ok) log("开始安装 " + id + "（点击「刷新进度」查看）");
      else log("安装启动失败: " + (r && r.error ? r.error : "未知"));
    }
    else if (controlId === "btnProgress") {
      var p = parse(mc.installProgress());
      if (p && p.finished) {
        win32.setWindowText("progLabel", p.error ? ("状态：安装失败 — " + p.error) : "状态：安装完成");
        log(p.error ? ("安装失败: " + p.error) : "安装完成");
      } else if (p) {
        var pct = p.total > 0 ? Math.floor(p.done * 100 / p.total) : 0;
        win32.setWindowText("progLabel", "状态：" + p.phase + " " + pct + "%  " + (p.current || ""));
      }
    }
    else if (controlId === "btnOffline") {
      var name = win32.getWindowText("nameEdit").trim() || "Steve";
      var a = parse(mc.authOffline(name));
      if (a) log("离线登录成功：" + a.name + " / " + a.uuid);
    }
    else if (controlId === "btnMs") {
      var dc = parse(mc.authMicrosoftStart());
      if (dc && dc.userCode) {
        log("请打开 " + dc.verificationUri + " 并输入代码：" + dc.userCode);
        log("然后点击「刷新进度」轮询登录结果");
        win32.setWindowText("progLabel", "微软登录：请输入代码 " + dc.userCode);
      } else {
        log("微软登录启动失败: " + (dc && dc.error ? dc.error : "网络错误"));
      }
    }
    else if (controlId === "btnLaunch") {
      applySource();
      var vid = selectedInstalledId();
      if (!vid) { log("请先选择或安装一个版本"); return; }
      var acc = parse(mc.getAccount());
      var name = (acc && acc.name) ? acc.name : win32.getWindowText("nameEdit").trim() || "Steve";
      var uuid = (acc && acc.uuid) ? acc.uuid : "";
      var token = (acc && acc.token) ? acc.token : "0";
      var opts = { id: vid, name: name, uuid: uuid, token: token, userType: (acc && acc.type) ? (acc.type === "msa" ? "msa" : "legacy") : "legacy" };
      var r = parse(mc.launch(JSON.stringify(opts)));
      if (r && r.ok) { win32.setWindowText("progLabel", "状态：游戏已启动"); log("正在启动 " + vid + " …"); }
      else log("启动失败: " + (r && r.error ? r.error : "未知错误"));
    }
    else if (controlId === "btnStop") {
      mc.stopGame();
      log("已请求停止游戏");
    }
  } catch (e) { log("操作异常: " + e); }
}

function onViewShow(container, w, h) {
  W = w; H = h;
  if (!hasMc) return;
  try { refreshInstalled(); } catch (e) {}
}

function onViewHide() {}
function onResize(w, h) { W = w; H = h; }
function onUnload() {}
