/*
 * Deskflow -- HarmonyOS Deskflow client shared connection-status store
 * 跨页面/扩展共享的连接状态（同进程模块级单例）。
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

const DOMAIN = 0x0000;

type StateListener = (state: number, statusText: string) => void;

class DeskflowStatusStore {
  private state: number = 0;
  private statusText: string = '未连接';
  private listeners: StateListener[] = [];

  getState(): number {
    return this.state;
  }

  getStatusText(): string {
    return this.statusText;
  }

  setState(state: number, statusText: string): void {
    this.state = state;
    this.statusText = statusText;
    for (const fn of this.listeners) {
      fn(state, statusText);
    }
  }

  registerStateListener(fn: StateListener): void {
    this.listeners.push(fn);
  }
}

export const desktopManager = new DeskflowStatusStore();
