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
import { common, abilityAccessCtrl, Permissions, Want, StartOptions } from '@kit.AbilityKit';
import { pasteboard } from '@kit.BasicServicesKit';
import { commonEventManager } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import deskflow from 'libentry.so';
import { ConnState, StatusBarManager } from '../statusbar/StatusBarManager';
import { desktopManager } from '../statusbar/DeskflowStatusStore';
import { BGEVT_STATUS, BGEVT_CLIP, BGEVT_STOP } from '../background/BackGroundAbility';

const DOMAIN = 0x0000;
const READ_PASTEBOARD_PERM: Permissions = 'ohos.permission.READ_PASTEBOARD';
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
  @State clipboardSync: boolean = true; // 默认开启剪贴板同步
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
    // 连接由后台承载 Ability 持有；状态/剪贴板经 commonEvent 广播回本页
    this.subscribeBgEvents();
    // 监听本机剪贴板变化：检测到真实的本地复制时立即推给服务端。
    // 用 pasteboard 'update' 监听比等 COUT 离开屏幕更可靠（服务端离开时不一定发 COUT 给我们）。
    try {
      pasteboard.getSystemPasteboard().on('update', () => {
        if (this.clipboardSync) {
          this.pushLocalClipboard();
        }
      });
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowClip', 'register pasteboard update listener fail: %{public}s', JSON.stringify(e));
    }
    // 回填上次保存的连接配置
    this.loadPrefs();
  }

  /** 订阅后台承载 Ability 广播的连接状态 / 远程剪贴板 */
  private subscribeBgEvents(): void {
    try {
      const info: commonEventManager.CommonEventSubscribeInfo =
        { events: [BGEVT_STATUS, BGEVT_CLIP] };
      const sub = commonEventManager.createSubscriberSync(info);
      commonEventManager.subscribe(sub, (err, data) => {
        if (err) {
          hilog.warn(DOMAIN, 'DeskflowBg', 'subscribe fail: %{public}s', JSON.stringify(err));
          return;
        }
        if (data?.event === BGEVT_STATUS) {
          const value = `${data.data ?? ''}`;
          this.statusText = value;
          this.connState = this.mapStatus(value);
          this.syncTray();
        } else if (data?.event === BGEVT_CLIP) {
          const value = `${data.data ?? ''}`;
          hilog.info(DOMAIN, 'DeskflowClip', 'recv remote clip len=%{public}d', value.length);
          this.lastRemoteClipboard = value;
          this.setSystemClipboard(value);
        }
      });
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowBg', 'subscribeBgEvents error: %{public}s', JSON.stringify(e));
    }
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
        this.clipboardSync = pref.getSync(PREFS_CLIPBOARD, true) as boolean;
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

  /** 开启剪贴板同步时向用户请求读取剪贴板权限（鸿蒙要求 ohos.permission.READ_PASTEBOARD） */
  private requestClipboardPermission(): void {
    try {
      const atManager = abilityAccessCtrl.createAtManager();
      atManager.requestPermissionsFromUser(this.getUIContext().getHostContext() as common.UIAbilityContext,
        [READ_PASTEBOARD_PERM]).then((data) => {
          const granted = data.authResults && data.authResults[0] === 0;
          if (!granted) {
            this.clipboardSync = false;
            try {
              this.statusText = '未授权读取剪贴板，剪贴板同步已关闭';
            } catch (e) {
            }
            this.savePrefs();
          } else {
            this.statusText = '剪贴板读取权限已授权';
          }
          hilog.info(DOMAIN, 'DeskflowClip', 'request READ_PASTEBOARD granted=%{public}d', granted ? 1 : 0);
        }).catch((e: Error) => {
          hilog.warn(DOMAIN, 'DeskflowClip', 'request perm fail: %{public}s', e.message);
        });
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowClip', 'requestClipboardPermission error: %{public}s', JSON.stringify(e));
    }
  }

  /** 读取系统剪贴板纯文本（供推送用；失败返回空串） */
  private readSystemClipboard(): string {
    try {
      const sys = pasteboard.getSystemPasteboard();
      const data = sys.getDataSync();
      const text = data.getPrimaryText ? data.getPrimaryText() : '';
      if (!text) {
        // 可能：内容非纯文本（getPrimaryText 仅返回 MIMETYPE_TEXT_PLAIN）、空，或未经
        // READ_PASTEBOARD 授权时读取受限。hasData 无需权限即可判断是否有数据。
        hilog.info(DOMAIN, 'DeskflowClip', 'readSys empty (has=%{public}d)',
          sys.hasDataSync() ? 1 : 0);
      }
      return text ? text : '';
    } catch (e) {
      const err = e as BusinessError;
      // 201 = 缺少 READ_PASTEBOARD 权限；非 201 为其它异常
      if (err.code === 201) {
        hilog.warn(DOMAIN, 'DeskflowClip', 'read clipboard denied: need READ_PASTEBOARD (code 201)');
      } else {
        hilog.warn(DOMAIN, 'DeskflowClip', 'read clipboard fail code=%{public}d msg=%{public}s',
          err.code, err.message);
      }
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

  /** 本机剪贴板变化（或离开屏幕）时：把本机剪贴板推给服务端。
   *  注：鸿蒙 6.1 读取系统剪贴板需要 READ_PASTEBOARD（声明会导致安装失败 9568289），
   *  故 readSystemClipboard 可能拿不到明文，此推送在那种情况下静默跳过。 */
  private pushLocalClipboard(): void {
    if (!this.clipboardSync) {
      return;
    }
    const text = this.readSystemClipboard();
    if (text && text.length > 0 && text !== this.lastRemoteClipboard) {
      try {
        // 上传方向把文本交给后台承载 Ability 推送（连接在它进程里）
        const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
        const want: Want = {
          bundleName: 'com.deskflow.ohospoc',
          abilityName: 'BackGroundAbility',
          parameters: { 'clipPush': text }
        };
        context.startAbility(want).catch((e: BusinessError) => {
          hilog.warn(DOMAIN, 'DeskflowClip', 'push via BG fail: %{public}s', JSON.stringify(e));
        });
        hilog.info(DOMAIN, 'DeskflowClip', 'push local clipboard len=%{public}d (via BG)', text.length);
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
            // 开启时向用户请求读取剪贴板权限（鸿蒙要求 READ_PASTEBOARD）
            if (isOn) {
              this.requestClipboardPermission();
            }
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
      // 连接由后台承载 Ability（状态栏项钉定进程）建立，隐藏到托盘时不被冻结。
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const want: Want = {
        bundleName: 'com.deskflow.ohospoc',
        abilityName: 'BackGroundAbility',
        parameters: {
          'host': this.hostInput.trim(),
          'port': Number(this.portInput),
          'screen': this.screenName,
          'invert': this.invertScroll,
          'autoreconnect': this.autoReconnect,
          'clipboard': this.clipboardSync
        }
      };
      this.startBg(want);
      this.connState = ConnState.CONNECTING;
      this.statusText = `正在连接 ${this.hostInput.trim()}:${this.portInput}...`;
      this.savePrefs();
    } catch (e) {
      this.statusText = `连接失败: ${JSON.stringify(e)}`;
      this.connState = ConnState.ERROR;
    }
    this.syncTray();
  }

  onDisconnect(): void {
    try {
      // 通过 commonEvent 通知后台承载 Ability 断开并自行终止
      this.requestBgStop();
      this.statusText = '正在断开连接...';
      this.connState = ConnState.DISCONNECTED;
      this.syncTray();
    } catch (e) {
      this.statusText = `断开失败: ${JSON.stringify(e)}`;
      this.connState = ConnState.ERROR;
      this.syncTray();
    }
  }

  /** 以独立进程方式启动后台承载 Ability（隐藏启动，独立于主窗口进程，避免隐藏时被冻结）。 */
  private startBg(want: Want): void {
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    try {
      // 先停掉可能残留的旧承载进程，避免二次连接命中 16000068（已被占用）而连到旧目标
      this.requestBgStop();
      const options: StartOptions = {
        processMode: 1, // contextConstant.ProcessMode.NEW_PROCESS_ATTACH_TO_PARENT
        startupVisibility: 0 // contextConstant.StartupVisibility.STARTUP_HIDE
      };
      context.startAbility(want, options).catch((e: BusinessError) => {
        if (e.code === 16000068) {
          // 承载已运行：旧实例正被请求停止，下一轮重新连接即可。
          hilog.info(DOMAIN, 'DeskflowBg', 'carrier already running (16000068), will restart');
          return;
        }
        hilog.warn(DOMAIN, 'DeskflowBg', 'startAbility fail: %{public}s', JSON.stringify(e));
      });
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowBg', 'startAbility error: %{public}s', JSON.stringify(e));
    }
  }

  /** 通知后台承载 Ability 停止（终止旧连接进程） */
  private requestBgStop(): void {
    try {
      const opts: commonEventManager.CommonEventPublishData = { data: 'stop' };
      commonEventManager.publish(BGEVT_STOP, opts, (err) => {
        if (err) {
          hilog.warn(DOMAIN, 'DeskflowBg', 'stop publish fail: %{public}s', JSON.stringify(err));
        }
      });
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowBg', 'requestBgStop error: %{public}s', JSON.stringify(e));
    }
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
