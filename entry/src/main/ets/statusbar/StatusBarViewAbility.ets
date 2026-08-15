/*
 * Deskflow -- HarmonyOS Deskflow client status-bar (tray) quick-operation view
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import { StatusBarViewExtensionAbility } from '@kit.DeskTopExtensionKit';
import { UIExtensionContentSession, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN = 0x0000;
const TAG = 'DeskflowStatusBar';

/**
 * 状态栏托盘图标「左键点击」弹出的快速操作窗。
 * 此处加载一个轻量的状态页（连接状态快捷操作）。
 */
export default class StatusBarViewAbility extends StatusBarViewExtensionAbility {
  onSessionCreate(want: Want, session: UIExtensionContentSession): void {
    hilog.info(DOMAIN, TAG, 'StatusBarViewAbility onSessionCreate');
    try {
      session.loadContent('pages/StatusBarQuickView');
    } catch (err) {
      hilog.error(DOMAIN, TAG, 'loadContent error: %{public}s', JSON.stringify(err));
    }
  }

  onSessionDestroy(session: UIExtensionContentSession): void {
    hilog.info(DOMAIN, TAG, 'StatusBarViewAbility onSessionDestroy');
  }
}
