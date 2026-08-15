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
import deskflow from 'libentry.so';

const DOMAIN = 0x0000;

// 连接状态
enum ConnState {
  DISCONNECTED = 0, // 未连接
  CONNECTING = 1,   // 连接中 / 重连中
  CONNECTED = 2,    // 已连接（含进入屏幕控制）
  ERROR = 3         // 异常断开 / 出错
}

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
  @State hostInput: string = '10.0.0.10';
  @State portInput: string = '24800';
  @State screenName: string = 'HarmonyPC';
  @State invertScroll: boolean = false;

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
    // 监听连接状态（worker 线程通过 NAPI 线程安全函数回投）
    deskflow.onDeskflowStatus((event: deskflow.ValueChangeEvent) => {
      this.statusText = event.value;
      this.connState = this.mapStatus(event.value);
    });
    // 滚轮反转开关默认值（可从偏好读取，先默认关）
    this.invertScroll = false;
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
          })
      }
      .width('100%')
      .margin({ top: 12 })
    }
    .width('100%')
    .alignItems(HorizontalAlign.Start)
    .padding(16)
    .backgroundColor('#FFFFFF')
    .borderRadius(12)
  }

  onConnect(): void {
    try {
      const d = display.getDefaultDisplaySync();
      const w = Math.round(d.width);
      const h = Math.round(d.height);
      // 同步滚轮开关当前值
      deskflow.setInvertScroll(this.invertScroll);
      this.statusText = deskflow.connectDeskflow(this.hostInput, Number(this.portInput), this.screenName, w, h);
      this.connState = ConnState.CONNECTING;
    } catch (e) {
      this.statusText = `连接失败: ${JSON.stringify(e)}`;
      this.connState = ConnState.ERROR;
    }
  }

  onDisconnect(): void {
    try {
      this.statusText = deskflow.disconnectDeskflow();
      this.connState = ConnState.DISCONNECTED;
    } catch (e) {
      this.statusText = `断开失败: ${JSON.stringify(e)}`;
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
