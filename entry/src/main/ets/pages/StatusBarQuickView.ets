/*
 * Deskflow -- HarmonyOS Deskflow client status-bar quick-operation page
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import { desktopManager } from '../statusbar/DeskflowStatusStore';

@Entry
@Component
struct StatusBarQuickView {
  @State state: number = 0;
  @State statusText: string = '未连接';

  aboutToAppear(): void {
    desktopManager.registerStateListener((s: number, t: string) => {
      this.state = s;
      this.statusText = t;
    });
    this.state = desktopManager.getState();
    this.statusText = desktopManager.getStatusText();
  }

  build() {
    Column({ space: 10 }) {
      Text('Deskflow')
        .fontSize(16)
        .fontWeight(FontWeight.Bold)
      Text(this.statusText)
        .fontSize(13)
        .fontColor(
          this.state === 2 ? '#4CB050' : this.state === 3 ? '#F44336' : '#666666'
        )
      Text('点右键菜单可：打开主界面 / 连接 / 断开')
        .fontSize(11)
        .fontColor('#999999')
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .padding(16)
    .alignItems(HorizontalAlign.Center)
  }
}
