/*
 * Deskflow -- HarmonyOS Deskflow client status-bar (tray) manager
 * 管理系统托盘图标（三态）+ 右键菜单 + 菜单点击。用 Desktop Extension Kit。
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import { statusBarManager } from '@kit.DeskTopExtensionKit';
import { common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';
import { emitter } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { resourceManager } from '@kit.LocalizationKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN = 0x0000;
const TAG = 'DeskflowStatusBar';

// 连接状态（与 Index.ets 的 ConnState 保持一致）
export enum ConnState {
  DISCONNECTED = 0,
  CONNECTING = 1,
  CONNECTED = 2,
  ERROR = 3
}

// 右键菜单项 code
const MENU_SHOW_MAIN = 'show_main';
const MENU_CONNECT = 'connect';
const MENU_DISCONNECT = 'disconnect';
const MENU_EXIT = 'exit';

/**
 * 管理状态栏托盘图标与菜单。
 * - add()：把图标加入系统状态栏
 * - updateState(state)：根据连接状态切换三态图标与右键菜单
 */
export class StatusBarManager {
  private static instance: StatusBarManager | null = null;
  private context: common.Context | null = null;
  private added: boolean = false;
  private state: number = ConnState.DISCONNECTED;

  // 菜单点击回调
  onConnectClick?: () => void;
  onDisconnectClick?: () => void;
  onShowMainClick?: () => void;
  onExitClick?: () => void;

  private constructor() {
  }

  static getInstance(): StatusBarManager {
    if (!StatusBarManager.instance) {
      StatusBarManager.instance = new StatusBarManager();
    }
    return StatusBarManager.instance;
  }

  /** 初始化：注册菜单点击/图标左键监听。在主 Ability 生命周期调用一次。 */
  init(context: common.Context): void {
    this.context = context;
    statusBarManager.on('rightMenuClick', (event: emitter.EventData) => {
      this.onRightMenuClick(event);
    });
    statusBarManager.on('statusBarIconClick', () => {
      if (this.onShowMainClick) {
        this.onShowMainClick();
      }
    });
    hilog.info(DOMAIN, TAG, 'init OK');
  }

  /** 把图标加入状态栏（幂等） */
  async add(): Promise<void> {
    if (this.added || !this.context) {
      return;
    }
    try {
      const icons = await this.loadIcons(this.state);
      if (!icons) {
        return;
      }
      const item: statusBarManager.StatusBarItem = {
        icons: icons,
        quickOperation: {
          title: 'Deskflow',
          height: 200,
          abilityName: 'StatusBarViewAbility',
          moduleName: 'entry'
        },
        statusBarGroupMenu: this.buildMenu(this.state),
        hoverTips: this.stateHoverTips(this.state)
      };
      statusBarManager.addToStatusBar(this.context, item);
      this.added = true;
      hilog.info(DOMAIN, TAG, 'addToStatusBar OK');
    } catch (err) {
      hilog.error(DOMAIN, TAG, 'addToStatusBar failed: %{public}s', JSON.stringify(err));
    }
  }

  /** 移除图标 */
  async remove(): Promise<void> {
    if (!this.added || !this.context) {
      return;
    }
    try {
      statusBarManager.removeFromStatusBar(this.context);
      this.added = false;
    } catch (err) {
      hilog.error(DOMAIN, TAG, 'removeFromStatusBar failed: %{public}s', JSON.stringify(err));
    }
  }

  /** 串行化更新通道：避免快速状态切换时并发 updateState 触发 1010710003（更新过于频繁） */
  private updateChain: Promise<void> = Promise.resolve();
  private debounceTimer: number = -1;
  private readonly debounceMs = 500; // 状态稳定后再真正落盘，把"连接中→已连接"合并为一次更新

  /** 依据连接状态更新托盘图标 + 右键菜单 + hover 提示（去抖+串行，最终态生效） */
  updateState(state: number): void {
    this.state = state;
    if (!this.added || !this.context) {
      return;
    }
    // 去抖：状态快速变化时只保留最后一次，避免频繁调用触发 1010710003
    if (this.debounceTimer !== -1) {
      clearTimeout(this.debounceTimer);
    }
    this.debounceTimer = setTimeout(() => {
      this.debounceTimer = -1;
      this.updateChain = this.updateChain.then(() => this.doUpdateState()).catch((e: Error) => {
        hilog.error(DOMAIN, TAG, 'doUpdateState unhandled: %{public}s', e.message);
      });
    }, this.debounceMs);
  }

  private async doUpdateState(): Promise<void> {
    const state = this.state; // 只应用最新目标状态
    const icons = await this.loadIcons(state);
    // 期间状态又变了：放弃本次，稍后队列里的最新状态会生效
    if (state !== this.state) {
      return;
    }
    if (icons) {
      try {
        statusBarManager.updateStatusBarIcon(this.context, icons);
      } catch (eIcon) {
        hilog.error(DOMAIN, TAG, 'updateStatusBarIcon failed: %{public}s', JSON.stringify(eIcon));
      }
    }
    try {
      statusBarManager.updateStatusBarMenu(this.context, this.buildMenu(state));
    } catch (eMenu) {
      hilog.error(DOMAIN, TAG, 'updateStatusBarMenu failed: %{public}s', JSON.stringify(eMenu));
    }
    hilog.info(DOMAIN, TAG, 'updateState applied state=%{public}d', state);
  }

  /** 生成三态图标（white/black 用同一状态色图标） */
  private async loadIcons(state: number): Promise<statusBarManager.StatusBarIcon | null> {
    const shapeName = this.iconShapeName(state);
    const ctx = this.context;
    if (!ctx) {
      return null;
    }
    try {
      const resMgr = ctx.resourceManager;
      // 官方样例同款 SVG 结构：读 rawfile 的 SVG -> createImageSource -> createPixelMap
      const white = await this.loadPixelMap(resMgr, `tray/${shapeName}_white.svg`);
      const black = await this.loadPixelMap(resMgr, `tray/${shapeName}_black.svg`);
      if (!white || !black) {
        return null;
      }
      return { white: white, black: black };
    } catch (err) {
      hilog.error(DOMAIN, TAG, 'loadIcons %{public}d failed: code=%{public}d msg=%{public}s',
        state, (err as BusinessError).code, (err as BusinessError).message);
      return null;
    }
  }

  private async loadPixelMap(resMgr: resourceManager.ResourceManager,
    rawName: string): Promise<image.PixelMap | null> {
    try {
      // 官方样例方式：getRawFileContentSync + createImageSource(buffer)（无 options）-> createPixelMap
      const fileData = resMgr.getRawFileContentSync(rawName);
      const buffer = fileData.buffer;
      const imageSource = image.createImageSource(buffer);
      const pixMap = await imageSource.createPixelMap();
      return pixMap;
    } catch (err) {
      hilog.error(DOMAIN, TAG, 'loadPixelMap %{public}s failed: %{public}s', rawName, JSON.stringify(err));
      return null;
    }
  }

  private iconShapeName(state: number): string {
    switch (state) {
      case ConnState.CONNECTED: return 'connected';
      case ConnState.ERROR: return 'error';
      default: return 'disconnected';
    }
  }

  private stateHoverTips(state: number): string {
    switch (state) {
      case ConnState.CONNECTED: return 'Deskflow：已连接';
      case ConnState.CONNECTING: return 'Deskflow：连接中';
      case ConnState.ERROR: return 'Deskflow：异常断开';
      default: return 'Deskflow：未连接';
    }
  }

  /** 依据状态构建右键菜单 */
  private buildMenu(state: number): statusBarManager.StatusBarGroupMenu[] {
    const group: statusBarManager.StatusBarGroupMenu = [];
    const statusItem: statusBarManager.StatusBarMenuItem = {
      title: this.stateHoverTips(state),
      menuAction: { abilityName: 'EntryAbility', notifyOnly: true }
    };
    group.push(statusItem);
    const showMain: statusBarManager.StatusBarMenuItem = {
      title: '打开主界面',
      menuCode: MENU_SHOW_MAIN,
      menuAction: { abilityName: 'EntryAbility', notifyOnly: true }
    };
    group.push(showMain);
    if (state === ConnState.CONNECTED) {
      const disconnect: statusBarManager.StatusBarMenuItem = {
        title: '断开连接',
        menuCode: MENU_DISCONNECT,
        menuAction: { abilityName: 'EntryAbility', notifyOnly: true }
      };
      group.push(disconnect);
    } else {
      const connect: statusBarManager.StatusBarMenuItem = {
        title: '连接',
        menuCode: MENU_CONNECT,
        menuAction: { abilityName: 'EntryAbility', notifyOnly: true }
      };
      group.push(connect);
    }
    const exit: statusBarManager.StatusBarMenuItem = {
      title: '退出',
      menuCode: MENU_EXIT,
      menuAction: { abilityName: 'EntryAbility', notifyOnly: true }
    };
    group.push(exit);
    return [group];
  }

  private onRightMenuClick(event: emitter.EventData): void {
    let menuCode = '';
    const data = event.data;
    if (data && data['menuCode'] !== undefined) {
      menuCode = `${data['menuCode']}`;
    }
    hilog.info(DOMAIN, TAG, 'rightMenuClick code=%{public}s', menuCode);
    switch (menuCode) {
      case MENU_CONNECT:
        if (this.onConnectClick) { this.onConnectClick(); }
        break;
      case MENU_DISCONNECT:
        if (this.onDisconnectClick) { this.onDisconnectClick(); }
        break;
      case MENU_SHOW_MAIN:
        if (this.onShowMainClick) { this.onShowMainClick(); }
        break;
      case MENU_EXIT:
        if (this.onExitClick) { this.onExitClick(); }
        break;
      default:
        break;
    }
  }
}
