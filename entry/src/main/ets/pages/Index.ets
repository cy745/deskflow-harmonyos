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
import testNapi from 'libentry.so';

const DOMAIN = 0x0000;

@Component
export struct ResultDisplay {
  @Prop result: string

  build() {
    Column() {
      Text(this.result)
        .fontSize(14)
        .width('100%')
        .wordBreak(WordBreak.NORMAL)
        .fontWeight(FontWeight.Medium)
        .textAlign(TextAlign.Start)
        .padding(12)
    }
    .width('100%')
    .backgroundColor('#F5F5F5')
    .borderRadius(8)
    .margin({ top: 20, bottom: 20 })
    .padding(0)
  }
}

@Entry
@Component
struct Index {
  @State result: string = '';
  @State connStatus: string = 'not connected';
  @State hostInput: string = '10.0.0.10';
  @State portInput: string = '24800';
  @State screenName: string = 'HarmonyPC';

  @Builder
  ApiMethodItem(title: string, onClick: () => void) {
    Text(title)
      .height('auto')
      .width('100%')
      .fontSize(13)
      .fontWeight(FontWeight.Bold)
      .fontColor('#0A59F7')
      .textAlign(TextAlign.Start)
      .margin({ top: 8, bottom: 8 })
      .lineHeight(20)
      .wordBreak(WordBreak.NORMAL)
      .maxLines(2)
      .backgroundColor('#F7F9FC')
      .borderRadius(4)
      .padding(8)
      .onClick(onClick)
  }

  aboutToAppear(): void {
    if (testNapi) {
      testNapi.onChange((event: testNapi.ValueChangeEvent) => {
        this.result = event.value;
      });
      testNapi.onDeskflowStatus((event: testNapi.ValueChangeEvent) => {
        this.connStatus = event.value;
      });
    } else {
      hilog.error(DOMAIN, 'NDKInputEventInterceptor', 'testNapi is undefined');
      this.result = 'Native plugin failed to load';
    }
  }

  build() {
    Scroll() {
      Column() {
        ResultDisplay({ result: this.result })

        Column() {
          TextInput({ placeholder: 'input' })
            .type(InputType.Normal)
            .width('80%')
            .height(50)
            .margin(10)
        }

        this.ApiMethodItem(
          "Add key event interceptor",
          () => {
            try {
              this.result = testNapi.addKeyEventInterceptor()
            } catch (e) {
              this.result = `Add key event interceptor, error：${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "Remove key event interceptor",
          () => {
            try {
              this.result = testNapi.removeKeyEventInterceptor()
            } catch (e) {
              this.result = `Remove key event interceptor, error：${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "Add input event interceptor. Input event include: mouse event, touch event, and axis event",
          () => {
            try {
              this.result = `${testNapi.addEventInterceptor()}, input event interception will be automatically canceled after 30 seconds.`
              setTimeout(() => {
                try {
                  this.result = testNapi.removeEventInterceptor()
                } catch (e) {
                  this.result = `Automatic removal of input event interception failed: ${JSON.stringify(e)}`;
                }
              }, 30000);
            } catch (e) {
              this.result = `Automatic removal of input event interception failed: ${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          `Remove input event interceptor. include: mouse event, touch event, and axis event`,
          () => {
            try {
              this.result = testNapi.removeEventInterceptor()
            } catch (e) {
              this.result = `Failed to remove input event interception: ${JSON.stringify(e)}`
            }
          }
        )

        Text('--- Deskflow Server 连接 ---')
          .fontSize(13)
          .fontWeight(FontWeight.Bold)
          .fontColor('#E62020')
          .textAlign(TextAlign.Start)
          .margin({ top: 12, bottom: 4 })

        Row() {
          Text('状态: ').fontSize(13)
          Text(this.connStatus).fontSize(13).fontWeight(FontWeight.Medium)
        }
        .width('100%')
        .margin({ top: 4 })

        TextInput({ placeholder: 'Server IP (Windows 电脑)', text: this.hostInput })
          .type(InputType.Normal)
          .width('80%')
          .height(44)
          .margin({ top: 8 })
          .onChange((v: string) => { this.hostInput = v })

        TextInput({ placeholder: 'Port', text: this.portInput })
          .type(InputType.Normal)
          .width('80%')
          .height(44)
          .margin({ top: 8 })
          .onChange((v: string) => { this.portInput = v })

        TextInput({ placeholder: '屏幕名 (显示在 Server 上)', text: this.screenName })
          .type(InputType.Normal)
          .width('80%')
          .height(44)
          .margin({ top: 8 })
          .onChange((v: string) => { this.screenName = v })

        Row() {
          Button('连接 Deskflow Server')
            .margin({ top: 10, right: 8 })
            .onClick(() => {
              try {
                const d = display.getDefaultDisplaySync();
                const w = Math.round(d.width);
                const h = Math.round(d.height);
                this.result = testNapi.connectDeskflow(this.hostInput, Number(this.portInput), this.screenName, w, h)
              } catch (e) {
                this.result = `connect error: ${JSON.stringify(e)}`
              }
            })
          Button('断开')
            .margin({ top: 10 })
            .onClick(() => {
              try {
                this.result = testNapi.disconnectDeskflow()
              } catch (e) {
                this.result = `disconnect error: ${JSON.stringify(e)}`
              }
            })
        }

        Text('--- Deskflow PoC: 注入授权与全局注入 (API 20+, 仅 PC/2in1 生效) ---')
          .fontSize(13)
          .fontWeight(FontWeight.Bold)
          .fontColor('#B620E0')
          .textAlign(TextAlign.Start)
          .margin({ top: 16, bottom: 8 })

        this.ApiMethodItem(
          "1. Request injection authorization (申请注入授权, 注意观察系统弹窗)",
          () => {
            try {
              this.result = testNapi.requestInjection()
            } catch (e) {
              this.result = `Request injection, error: ${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "2. Query authorized status (查询授权状态: 0未授权/1授权中/2已授权)",
          () => {
            try {
              this.result = testNapi.queryAuthorizedStatus()
            } catch (e) {
              this.result = `Query authorized status, error: ${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "3. Inject mouse click at screen center (注入点击到屏幕中心, vp单位)",
          () => {
            try {
              const d = display.getDefaultDisplaySync();
              // 注入 API 使用 vp（逻辑像素）坐标，不要乘 densityPixels
              const cx = Math.round(d.width / 2);
              const cy = Math.round(d.height / 2);
              this.result = `screen ${d.width}x${d.height}vp density=${d.densityPixels} -> inject(${cx},${cy})vp: ` +
                testNapi.injectMouseClickGlobal(cx, cy)
            } catch (e) {
              this.result = `Inject mouse click, error: ${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "3.5 Smooth move test: 左上角 -> 屏幕中心, 300步x2ms 缓动曲线 (观察丝滑度与性能统计)",
          () => {
            try {
              const d = display.getDefaultDisplaySync();
              const cx = Math.round(d.width / 2);
              const cy = Math.round(d.height / 2);
              this.result = testNapi.injectMouseTrail(100, 100, cx, cy, 300, 2)
            } catch (e) {
              this.result = `Inject mouse trail, error: ${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "4. Inject Enter key (2015) (注入一次回车键)",
          () => {
            try {
              this.result = testNapi.injectKey(2015)
            } catch (e) {
              this.result = `Inject key, error: ${JSON.stringify(e)}`
            }
          }
        )

        this.ApiMethodItem(
          "5. Cancel injection (取消注入并撤销授权)",
          () => {
            try {
              this.result = testNapi.cancelInjection()
            } catch (e) {
              this.result = `Cancel injection, error: ${JSON.stringify(e)}`
            }
          }
        )

      }
      .height('100%')
      .width('80%')
      .alignItems(HorizontalAlign.Center)
      .margin({ left: '10%', right: '10%' })
      .margin({ bottom: 200 })
    }
    .width('100%')
    .height('100%')
    .scrollable(ScrollDirection.Vertical)
    .scrollBar(BarState.On)
    .scrollBarColor('#888888')
    .scrollBarWidth(8)
    .edgeEffect(EdgeEffect.Spring)
  }
}
