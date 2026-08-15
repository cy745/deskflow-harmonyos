/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { hilog } from '@kit.PerformanceAnalysisKit';
import { display } from '@kit.ArkUI';
import { preferences } from '@kit.ArkData';
import { common } from '@kit.AbilityKit';
import { pasteboard } from '@kit.BasicServicesKit';
import deskflow from 'libentry.so';
import { ConnState, StatusBarManager } from '../statusbar/StatusBarManager';
import { desktopManager } from '../statusbar/DeskflowStatusStore';

const DOMAIN = 0x0000;
const PREFS_NAME = 'deskflow_config';
const PREFS_HOST = 'host';
const PREFS_PORT = 'port';
const PREFS_NAME_FIELD = 'client_name';
const PREFS_INVERT = 'invert_scroll';
const PREFS_AUTORECONNECT = 'auto_reconnect';
const PREFS_CLIPBOARD = 'clipboard_sync';

function stateColor(state: number): string {
  switch (state) {
    case ConnState.CONNECTING: return '#E6A23C';
    case ConnState.CONNECTED: return '#4CB050';
    case ConnState.ERROR: return '#F44336';
    default: return '#8A8A8A';
  }
}

function stateLabel(state: number): string {
  switch (state) {
    case ConnState.CONNECTING: return '连接中';
    case ConnState.CONNECTED: return '已连接';
    case ConnState.ERROR: return '异常断开';
    default: return '未连接';
  }
}

@Entry
@Component
struct Index {
  @State connState: number = ConnState.DISCONNECTED;
  @State statusText: string = '尚未连接 Deskflow 服务端';
  @State hostInput: string = '';
  @State portInput: string = '24800';
  @State screenName: string = 'HarmonyPC';
  @State invertScroll: boolean = false;
  @State autoReconnect: boolean = true; // 默认开启自动重连/启动自动连接
  @State clipboardSync: boolean = false; // 默认关闭剪贴板同步（涉及系统剪贴板权限）
  // 剪贴板桥接：避免把刚写入的本机内容又当成本机新复制推回去（回环）
  private lastRemoteClipboard: string = '';

  // 根据服务端状态字符串推断连接状态
  private mapStatus(s: string): number {
    if (s === undefined || s === null) {
      return ConnState.DISCONNECTED;
    }
    const t = s.trim().toLowerCase();
    if (t.indexOf('error') >= 0 || t.indexOf('closed') >= 0 || t.indexOf('incompatible') >= 0 ||
      t.indexOf('unknown') >= 0 || t.indexOf('violation') >= 0) {
      return ConnState.ERROR;
    }
    if (t.indexOf('active') >= 0 || t.indexOf('control acquired') >= 0) {
      return ConnState.CONNECTED;
    }
    if (t.indexOf('handshake ok') >= 0 || t.indexOf('reconnect') >= 0 ||
      t.indexOf('waiting for enter') >= 0) {
      return ConnState.CONNECTED; // 已握手并进入运行期
    }
    if (t.indexOf('connecting') >= 0 || t.indexOf('connect failed') >= 0) {
      return ConnState.CONNECTING;
    }
    if (t.indexOf('disconnected') >= 0) {
      return ConnState.DISCONNECTED;
    }
    // 默认保持当前状态
    return this.connState;
  }

  aboutToAppear(): void {
    // 托盘菜单回调
    const sb = StatusBarManager.getInstance();
    sb.onConnectClick = () => this.onConnect();
    sb.onDisconnectClick = () => this.onDisconnect();
    // 监听连接状态（worker 线程通过 NAPI 线程安全函数回投）
    // 顺带监听剪贴板同步触发：进入本机屏幕(active)时 C++ 已发 CCLP 拉取；
    // 离开本机屏幕(inactive)时把本机剪贴板推给对端。
    let prevActive = false;
    deskflow.onDeskflowStatus((event: deskflow.ValueChangeEvent) => {
      this.statusText = event.value;
      this.connState = this.mapStatus(event.value);
      const s = event.value.toLowerCase();
      const isActive = s.indexOf('active') >= 0 || s.indexOf('control acquired') >= 0;
      if (prevActive && !isActive && this.clipboardSync) {
        // 刚离开本机屏幕：把本机剪贴板推给服务端
        this.pushLocalClipboard();
      }
      prevActive = isActive;
      this.syncTray();
    });
    // 监听远程剪贴板文本到达：写入系统剪贴板
    deskflow.onClipboardData((event: deskflow.ValueChangeEvent) => {
      this.lastRemoteClipboard = event.value;
      this.setSystemClipboard(event.value);
    });
    // 回填上次保存的连接配置
    this.loadPrefs();
  }

  /** 读取上次保存的连接配置（host/port/name/滚轮反转/自动重连） */
  private loadPrefs(): void {
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      preferences.getPreferences(context, PREFS_NAME).then((pref: preferences.Preferences) => {
        this.hostInput = pref.getSync(PREFS_HOST, '') as string;
        this.portInput = pref.getSync(PREFS_PORT, '24800') as string;
        this.screenName = pref.getSync(PREFS_NAME_FIELD, 'HarmonyPC') as string;
        this.invertScroll = pref.getSync(PREFS_INVERT, false) as boolean;
        this.autoReconnect = pref.getSync(PREFS_AUTORECONNECT, true) as boolean;
        this.clipboardSync = pref.getSync(PREFS_CLIPBOARD, false) as boolean;
        // 启动自动连接：回填完配置后，若开启且地址可用，自动发起连接
        if (this.autoReconnect && this.hostInput.trim().length > 0 && this.connState === ConnState.DISCONNECTED) {
          hilog.info(DOMAIN, 'DeskflowPrefs', 'auto reconnect on launch, connecting...');
          this.onConnect();
        }
      }).catch((e: Error) => {
        hilog.error(DOMAIN, 'DeskflowPrefs', 'load prefs fail: %{public}s', e.message);
      });
    } catch (err) {
      hilog.error(DOMAIN, 'DeskflowPrefs', 'load pref error: %{public}s', JSON.stringify(err));
    }
  }

  /** 保存连接配置（下次启动自动回填） */
  private savePrefs(): void {
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      preferences.getPreferences(context, PREFS_NAME).then((pref: preferences.Preferences) => {
        pref.putSync(PREFS_HOST, this.hostInput);
        pref.putSync(PREFS_PORT, this.portInput);
        pref.putSync(PREFS_NAME_FIELD, this.screenName);
        pref.putSync(PREFS_INVERT, this.invertScroll);
        pref.putSync(PREFS_AUTORECONNECT, this.autoReconnect);
        pref.putSync(PREFS_CLIPBOARD, this.clipboardSync);
        pref.flush();
      });
    } catch (err) {
      hilog.error(DOMAIN, 'DeskflowPrefs', 'save pref error: %{public}s', JSON.stringify(err));
    }
  }

  private syncTray(): void {
    // 更新托盘图标/菜单 + 共享状态（供工具栏快速操作弹窗显示）
    desktopManager.setState(this.connState, this.statusText);
    StatusBarManager.getInstance().updateState(this.connState);
  }

  // ===== 剪贴板同步 =====

  /** 读取系统剪贴板纯文本（供推送用；失败返回空串） */
  private readSystemClipboard(): string {
    try {
      const data = pasteboard.getSystemPasteboard().getDataSync();
      const text = data.getPrimaryText();
      return text ? text : '';
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowClip', 'read clipboard fail: %{public}s', JSON.stringify(e));
    }
    return '';
  }

  /** 写入系统剪贴板（远程剪贴板文本到达时调用） */
  private setSystemClipboard(value: string): void {
    try {
      if (!this.clipboardSync || !value || value.length === 0) {
        return;
      }
      const data = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, value);
      pasteboard.getSystemPasteboard().setDataSync(data);
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowClip', 'set clipboard fail: %{public}s', JSON.stringify(e));
    }
  }

  /** 离开本机屏幕时：把本机剪贴板推给服务端（忽略回环，不推刚写入的远程内容） */
  private pushLocalClipboard(): void {
    if (!this.clipboardSync) {
      return;
    }
    const text = this.readSystemClipboard();
    if (text && text.length > 0 && text !== this.lastRemoteClipboard) {
      try {
        deskflow.pushClipboard(text);
      } catch (e) {
        hilog.warn(DOMAIN, 'DeskflowClip', 'push clipboard fail: %{public}s', JSON.stringify(e));
      }
    }
  }

  @Builder
  StatusCard() {
    Column() {
      Row() {
        // 状态圆点
        Circle({ width: 12, height: 12 })
          .fill(stateColor(this.connState))
        Text(stateLabel(this.connState))
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .fontColor(stateColor(this.connState))
          .margin({ left: 8 })
        Blank()
        Button('连接')
          .fontSize(13)
          .height(30)
          .enabled(this.connState !== ConnState.CONNECTING)
          .onClick(() => this.onConnect())
        Button('断开')
          .fontSize(13)
          .height(30)
          .margin({ left: 8 })
          .enabled(this.connState === ConnState.CONNECTED || this.connState === ConnState.CONNECTING)
          .onClick(() => this.onDisconnect())
      }
      .width('100%')

      Text(this.statusText)
        .fontSize(13)
        .fontColor('#666666')
        .width('100%')
        .margin({ top: 10 })
        .textAlign(TextAlign.Start)
        .wordBreak(WordBreak.BREAK_ALL)
    }
    .width('100%')
    .padding(16)
    .backgroundColor('#F7F9FC')
    .borderRadius(12)
  }

  @Builder
  ConnConfig() {
    Column() {
      Text('服务端连接')
        .fontSize(14)
        .fontWeight(FontWeight.Bold)
        .width('100%')
      TextInput({ placeholder: 'Server IP (例如 192.168.1.10)', text: this.hostInput })
        .type(InputType.Normal)
        .width('100%')
        .height(42)
        .margin({ top: 10 })
        .onChange((v: string) => { this.hostInput = v })
      TextInput({ placeholder: '端口 (默认 24800)', text: this.portInput })
        .type(InputType.NUMBER_DECIMAL)
        .width('100%')
        .height(42)
        .margin({ top: 8 })
        .onChange((v: string) => { this.portInput = v })
      TextInput({ placeholder: '屏幕名 (在服务端显示的名称)', text: this.screenName })
        .type(InputType.Normal)
        .width('100%')
        .height(42)
        .margin({ top: 8 })
        .onChange((v: string) => { this.screenName = v })
    }
    .width('100%')
    .alignItems(HorizontalAlign.Start)
    .padding(16)
    .backgroundColor('#FFFFFF')
    .borderRadius(12)
  }

  @Builder
  Settings() {
    Column() {
      Text('设置')
        .fontSize(14)
        .fontWeight(FontWeight.Bold)
        .width('100%')
      Row() {
        Text('反转滚轮方向')
          .fontSize(14)
        Blank()
        Toggle({ type: ToggleType.Switch, isOn: this.invertScroll })
          .selectedColor('#4CB050')
          .onChange((isOn: boolean) => {
            this.invertScroll = isOn;
            try {
              this.statusText = deskflow.setInvertScroll(isOn);
            } catch (e) {
              this.statusText = `设置滚轮反转失败: ${JSON.stringify(e)}`;
            }
            this.savePrefs();
          })
      }
      .width('100%')
      .margin({ top: 12 })

      Row() {
        Text('自动重连/启动自动连接')
          .fontSize(14)
        Blank()
        Toggle({ type: ToggleType.Switch, isOn: this.autoReconnect })
          .selectedColor('#4CB050')
          .onChange((isOn: boolean) => {
            this.autoReconnect = isOn;
            try {
              this.statusText = deskflow.setAutoReconnect(isOn);
            } catch (e) {
              this.statusText = `设置自动重连失败: ${JSON.stringify(e)}`;
            }
            this.savePrefs();
          })
      }
      .width('100%')
      .margin({ top: 12 })
      Text('开启后：应用启动时用上次配置自动连接；连接意外断开后自动重连。')
        .fontSize(12)
        .fontColor('#999999')
        .width('100%')
        .margin({ top: 4 })

      Row() {
        Text('剪贴板同步')
          .fontSize(14)
        Blank()
        Toggle({ type: ToggleType.Switch, isOn: this.clipboardSync })
          .selectedColor('#4CB050')
          .onChange((isOn: boolean) => {
            this.clipboardSync = isOn;
            try {
              this.statusText = deskflow.setClipboardSync(isOn);
            } catch (e) {
              this.statusText = `设置剪贴板同步失败: ${JSON.stringify(e)}`;
            }
            this.savePrefs();
          })
      }
      .width('100%')
      .margin({ top: 12 })
      Text('本机与服务端之间同步剪贴板文本（进入/离开本机屏幕时同步）。')
        .fontSize(12)
        .fontColor('#999999')
        .width('100%')
        .margin({ top: 4 })
    }
    .width('100%')
    .alignItems(HorizontalAlign.Start)
    .padding(16)
    .backgroundColor('#FFFFFF')
    .borderRadius(12)
  }

  onConnect(): void {
    try {
      if (this.hostInput.trim().length === 0) {
        this.statusText = '请先填写服务端 IP/地址';
        return;
      }
      const d = display.getDefaultDisplaySync();
      const w = Math.round(d.width);
      const h = Math.round(d.height);
      // 同步滚轮开关与自动重连开关、剪贴板同步开关
      deskflow.setInvertScroll(this.invertScroll);
      deskflow.setAutoReconnect(this.autoReconnect);
      deskflow.setClipboardSync(this.clipboardSync);
      this.statusText = deskflow.connectDeskflow(this.hostInput, Number(this.portInput), this.screenName, w, h);
      this.connState = ConnState.CONNECTING;
      this.savePrefs();
    } catch (e) {
      this.statusText = `连接失败: ${JSON.stringify(e)}`;
      this.connState = ConnState.ERROR;
    }
    this.syncTray();
  }

  onDisconnect(): void {
    try {
      // 手动断开视为用户主动停止，关闭自动重连避免立刻重连
      deskflow.setAutoReconnect(false);
      this.statusText = deskflow.disconnectDeskflow();
      this.connState = ConnState.DISCONNECTED;
    } catch (e) {
      this.statusText = `断开失败: ${JSON.stringify(e)}`;
    }
    this.syncTray();
  }

  build() {
    Scroll() {
      Column({ space: 12 }) {
        Column({ space: 4 }) {
          Text('Deskflow')
            .fontSize(24)
            .fontWeight(FontWeight.Bold)
          Text('鸿蒙电脑键鼠共享客户端')
            .fontSize(13)
            .fontColor('#888888')
        }
        .width('100%')
        .alignItems(HorizontalAlign.Center)
        .padding({ top: 16, bottom: 8 })

        this.StatusCard()
        this.ConnConfig()
        this.Settings()

        Text('将鼠标移到服务端屏幕右缘进入本机；本机屏幕左缘回到服务端。')
          .fontSize(12)
          .fontColor('#999999')
          .width('100%')
          .textAlign(TextAlign.Center)
          .margin({ top: 8 })
      }
      .width('94%')
      .margin({ left: '3%', right: '3%' })
      .padding({ bottom: 40 })
    }
    .width('100%')
    .height('100%')
    .scrollable(ScrollDirection.Vertical)
    .scrollBar(BarState.Auto)
  }
}
