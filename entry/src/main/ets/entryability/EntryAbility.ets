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

import { AbilityConstant, ConfigurationConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import { StatusBarManager, ConnState } from '../statusbar/StatusBarManager';

const DOMAIN = 0x0000;

export default class EntryAbility extends UIAbility {
  private mainWindow: window.Window | null = null;

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    try {
      this.context.getApplicationContext().setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_NOT_SET);
    } catch (err) {
      hilog.error(DOMAIN, 'testTag', 'Failed to set colorMode. Cause: %{public}s', JSON.stringify(err));
    }
    // 初始化状态栏托盘（桌面扩展，2in1 生效）
    try {
      const sb = StatusBarManager.getInstance();
      sb.init(this.context);
      // 托盘菜单「打开主界面」：恢复主窗口（若隐藏到托盘则重新显示）
      sb.onShowMainClick = () => {
        hilog.info(DOMAIN, 'DeskflowStatusBar', 'show main window');
        this.showMainWindow().catch((e: BusinessError) => {
          hilog.error(DOMAIN, 'DeskflowStatusBar', 'show main failed: %{public}s', JSON.stringify(e));
        });
      };
      // 系统右键菜单自带「退出」，无需自定义 onExitClick。
      // 延迟到窗口就绪后加入
      sb.add();
    } catch (err) {
      hilog.error(DOMAIN, 'DeskflowStatusBar', 'tray init failed: %{public}s', JSON.stringify(err));
    }
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onCreate');
  }

  onDestroy(): void {
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onDestroy');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    // Main window is created, set main page for this ability
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onWindowStageCreate');

    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(DOMAIN, 'testTag', 'Failed to load the content. Cause: %{public}s', JSON.stringify(err));
        return;
      }
      hilog.info(DOMAIN, 'testTag', 'Succeeded in loading the content.');
    });

    // 记录主窗口，并点击系统标题栏「关闭」按钮 → 隐藏到托盘（不退出），
    // 直到托盘菜单「退出」才真正终止。
    try {
      this.mainWindow = windowStage.getMainWindowSync();
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowTray', 'get main window fail: %{public}s', JSON.stringify(e));
      return;
    }

    // windowWillClose @since 15：resolve(true)=阻止关闭(保持窗口)，resolve(false)=继续关闭。
    // 用 hideAbility() 真正隐藏窗口到托盘（PC/2in1/Tablet），应用进程与托盘保持存活。
    // 注意：不要同时注册 windowStageClose（会被忽略）。
    try {
      this.mainWindow.on('windowWillClose', (): Promise<boolean> => {
        hilog.info(DOMAIN, 'DeskflowTray', 'windowWillClose -> hide to tray');
        try {
          this.context.hideAbility();
        } catch (e) {
          hilog.warn(DOMAIN, 'DeskflowTray', 'hideAbility fail: %{public}s', JSON.stringify(e));
          // 回退：最小化到任务栏，避免关闭
          try { this.mainWindow?.minimize(); } catch (e2) { /* ignore */ }
        }
        return Promise.resolve(true); // 阻止关闭，窗口隐藏到托盘
      });
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowTray', 'windowWillClose register fail: %{public}s', JSON.stringify(e));
    }
  }

  /** 从托盘/后台恢复主窗口 */
  private async showMainWindow(): Promise<void> {
    try {
      await this.context.showAbility();
      return;
    } catch (e) {
      hilog.warn(DOMAIN, 'DeskflowTray', 'showAbility fail: %{public}s', JSON.stringify(e));
    }
    // 兜底：拉起新的 Ability 实例
    await this.context.startAbility({
      bundleName: 'com.deskflow.ohospoc',
      abilityName: 'EntryAbility'
    });
  }

  onWindowStageDestroy(): void {
    // Main window is destroyed, release UI related resources
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onWindowStageDestroy');
  }

  onForeground(): void {
    // Ability has brought to foreground
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onForeground');
  }

  onBackground(): void {
    // Ability has back to background
    hilog.info(DOMAIN, 'testTag', '%{public}s', 'Ability onBackground');
  }
}